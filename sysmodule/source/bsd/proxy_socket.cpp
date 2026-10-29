/**
 * @file proxy_socket.cpp
 * @brief Implementation of the Proxy Socket for LDN Traffic Routing
 *
 * This file implements the ProxySocket class. Data transfer operations
 * (Send/Recv) interact with the ProxySocketManager to encode/decode
 * ProxyData packets.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "proxy_socket.hpp"
#include "proxy_activity.hpp"
#include "proxy_socket_manager.hpp"
#include "../debug/log.hpp"
#include "../ipc_thread_budget.hpp"

#include <algorithm>

// forward_wait.cpp; declared here so host tests can stand in for pm
namespace ams::mitm::bsd::forward {
bool IsGameAlive(u64 pid);
}

namespace {
    /// Blocking waits wake this often to notice Close()
    constexpr u64 BlockSliceMs = 100;
    /// Owner liveness is checked every this many slices (one pm call)
    constexpr u64 OwnerCheckSlices = 5;
    /// Waits at least this long are logged when they end
    constexpr u64 LongWaitLogMs = 5000;
    /// Blocking connect gives up after this long, like the reference client
    constexpr u64 ConnectTimeoutMs = 4000;

    bool IsProcessAlive(u64 pid) {
        // Not svcGetProcessList: a dead game stays listed while the socket
        // service still maps its transfer memory
        return pid == 0 || ams::mitm::bsd::forward::IsGameAlive(pid);
    }
}

namespace ams::mitm::bsd {

// =============================================================================
// BSD Error Codes (from bsd_types.hpp)
// =============================================================================

using Errno = potluck::bsd::BsdErrno;

// =============================================================================
// Construction / Destruction
// =============================================================================

ProxySocket::ProxySocket(potluck::bsd::SocketType type, potluck::bsd::ProtocolType protocol,
                         u64 owner_pid)
    : m_owner_pid(owner_pid)
    , m_type(type)
    , m_protocol(protocol)
    , m_state(ProxySocketState::Created)
    , m_non_blocking(false)
    , m_shutdown_read(false)
    , m_shutdown_write(false)
{
    // Initialize addresses to zero
    std::memset(&m_local_addr, 0, sizeof(m_local_addr));
    std::memset(&m_remote_addr, 0, sizeof(m_remote_addr));

    // Set address family
    m_local_addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
    m_local_addr.sin_len = sizeof(potluck::bsd::SockAddrIn);
    m_remote_addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
    m_remote_addr.sin_len = sizeof(potluck::bsd::SockAddrIn);
}

ProxySocket::~ProxySocket() {
    // Just cleanup, don't send disconnect (should have called Close())
    m_state = ProxySocketState::Closed;
}

// =============================================================================
// Address Management
// =============================================================================

Result ProxySocket::Bind(const potluck::bsd::SockAddrIn& addr) {
    // Check state
    if (m_state != ProxySocketState::Created) {
        // Already bound or in invalid state
        R_THROW(static_cast<s32>(Errno::Inval));
    }

    // Validate address family
    if (addr.sin_family != static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet)) {
        R_THROW(static_cast<s32>(Errno::AfNoSupport));
    }

    // Store local address
    m_local_addr = addr;
    m_state = ProxySocketState::Bound;

    R_SUCCEED();
}

Result ProxySocket::Connect(const potluck::bsd::SockAddrIn& addr) {
    // Check state - must be at least created (can connect unbound socket)
    if (m_state == ProxySocketState::Closed) {
        R_THROW(static_cast<s32>(Errno::BadF));
    }

    // If not bound yet, auto-bind will happen with ephemeral port
    // (caller should have done this before calling Connect)

    // Validate address family
    if (addr.sin_family != static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet)) {
        R_THROW(static_cast<s32>(Errno::AfNoSupport));
    }

    // For TCP, perform connect handshake
    if (m_type == potluck::bsd::SocketType::Stream) {
        if (m_state == ProxySocketState::Connected) {
            R_THROW(static_cast<s32>(Errno::IsConn));
        }
        if (m_state == ProxySocketState::Connecting) {
            R_THROW(static_cast<s32>(Errno::Already));
        }
        if (m_state == ProxySocketState::Listening) {
            R_THROW(static_cast<s32>(Errno::OpNotSupp));
        }

        {
            std::scoped_lock lock(m_queue_mutex);
            // Set under the lock: the receive thread matches segments against it
            m_remote_addr = addr;
            m_state = ProxySocketState::Connecting;
            m_connect_started_tick = armGetSystemTick();
            m_connect_response_received = false;
            m_socket_error = 0;
            m_peer_closed = false;
            m_connect_event.Clear();
        }

        const uint32_t local_ip = LocalWireIp();
        LOG_INFO("TCP connect: 0x%08X:%u -> 0x%08X:%u (%s)", local_ip, m_local_addr.GetPort(),
                 addr.GetAddr(), addr.GetPort(), m_non_blocking ? "non-blocking" : "blocking");

        auto& manager = ProxySocketManager::GetInstance();
        if (!manager.SendProxyConnect(local_ip, m_local_addr.GetPort(), addr.GetAddr(), addr.GetPort(),
                                      m_protocol)) {
            std::scoped_lock lock(m_queue_mutex);
            m_state = ProxySocketState::Bound;
            R_THROW(static_cast<s32>(Errno::NetUnreach));
        }

        if (m_non_blocking) {
            // HandleConnectResponse finishes the connect; poll reports POLLOUT
            // and SO_ERROR carries the outcome, as with a real socket.
            R_THROW(static_cast<s32>(Errno::InProgress));
        }

        BlockOn(m_connect_event, ConnectTimeoutMs);

        std::scoped_lock lock(m_queue_mutex);
        if (m_state == ProxySocketState::Closed) {
            R_THROW(static_cast<s32>(Errno::BadF));
        }
        if (m_state == ProxySocketState::Connected) {
            LOG_INFO("TCP connect: port %u connected", m_local_addr.GetPort());
            R_SUCCEED();
        }
        const s32 error = m_connect_response_received ? m_socket_error : static_cast<s32>(Errno::TimedOut);
        m_state = ProxySocketState::Bound;
        m_socket_error = 0;
        LOG_WARN("TCP connect: port %u failed, errno %d (%s)", m_local_addr.GetPort(), error,
                 m_connect_response_received ? "peer refused" : "no reply within 4 s");
        R_THROW(error);
    } else {
        // For UDP, just store the default destination
        m_remote_addr = addr;
        m_state = ProxySocketState::Connected;
    }

    R_SUCCEED();
}

Result ProxySocket::GetSockName(potluck::bsd::SockAddrIn* out_addr) const {
    if (out_addr == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    if (m_state == ProxySocketState::Created) {
        // Not bound yet, return zeroed address
        std::memset(out_addr, 0, sizeof(*out_addr));
        out_addr->sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
        out_addr->sin_len = sizeof(potluck::bsd::SockAddrIn);
    } else {
        *out_addr = m_local_addr;
    }

    R_SUCCEED();
}

Result ProxySocket::GetPeerName(potluck::bsd::SockAddrIn* out_addr) const {
    if (out_addr == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    if (m_state != ProxySocketState::Connected) {
        R_THROW(static_cast<s32>(Errno::NotConn));
    }

    *out_addr = m_remote_addr;
    R_SUCCEED();
}

// =============================================================================
// Data Transfer
// =============================================================================

s32 ProxySocket::Send(const void* data, size_t len, s32 flags) {
    // Must be connected for Send()
    if (m_state != ProxySocketState::Connected) {
        return -static_cast<s32>(Errno::NotConn);
    }

    // Use SendTo with connected remote address
    return SendTo(data, len, flags, m_remote_addr);
}

s32 ProxySocket::SendTo(const void* data, size_t len, s32 flags, const potluck::bsd::SockAddrIn& dest) {
    // MSG_DONTWAIT = 0x80 (Nintendo Switch). The relay send honors it like a kernel socket would.
    const bool may_wait = (flags & 0x80) == 0 && !m_non_blocking;

    // Check write shutdown
    if (m_shutdown_write) {
        return -static_cast<s32>(Errno::Inval);
    }

    // Check socket state
    if (m_state == ProxySocketState::Closed) {
        return -static_cast<s32>(Errno::BadF);
    }

    // Must be at least bound to send
    if (m_state == ProxySocketState::Created) {
        return -static_cast<s32>(Errno::DestAddrReq);
    }

    // Validate data
    if (data == nullptr && len > 0) {
        return -static_cast<s32>(Errno::Fault);
    }

    const bool is_stream = m_type == potluck::bsd::SocketType::Stream;
    if (is_stream) {
        if (m_state != ProxySocketState::Connected) {
            return -static_cast<s32>(Errno::NotConn);
        }
        if (m_peer_closed) {
            return -static_cast<s32>(Errno::ConnReset);
        }
        if (len == 0) {
            return 0;
        }
    } else if (len > PROXY_SOCKET_MAX_PAYLOAD) {
        // A datagram cannot be split
        return -static_cast<s32>(Errno::MsgSize);
    }

    // Send via ProxySocketManager which routes to LDN server.
    // m_local_addr.sin_addr is stored in wire format (no bswap was applied in Bind),
    // so read it directly. dest comes from the game in network byte order, so GetAddr()
    // bswaps it into wire format — which is what the server expects on the wire.
    uint32_t source_ip = is_stream ? LocalWireIp() : m_local_addr.sin_addr;
    uint16_t source_port = m_local_addr.GetPort();
    uint32_t dest_ip = is_stream ? RemoteWireIp() : dest.GetAddr();
    uint16_t dest_port = is_stream ? m_remote_addr.GetPort() : dest.GetPort();

    auto& manager = ProxySocketManager::GetInstance();

    // TCP is a byte stream, so a large write goes out as several ProxyData
    // frames; the relay keeps them in order over its own TCP connection.
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t sent_total = 0;
    do {
        const size_t chunk = std::min(len - sent_total, PROXY_SOCKET_MAX_PAYLOAD);
        const ProxySendResult result = manager.SendProxyData(source_ip, source_port, dest_ip, dest_port,
                                                             m_protocol, bytes + sent_total, chunk, may_wait);
        if (result != ProxySendResult::Sent) {
            // Partial stream writes report what went out, like a real socket
            if (sent_total > 0) {
                return static_cast<s32>(sent_total);
            }
            // A nonblocking send the relay cannot take now: nothing went out, try again
            if (result == ProxySendResult::WouldBlock) {
                return -static_cast<s32>(Errno::Again);
            }
            // No send callback registered or send failed
            return -static_cast<s32>(Errno::NetUnreach);
        }
        sent_total += chunk;
    } while (sent_total < len);

    return static_cast<s32>(len);
}

s32 ProxySocket::Recv(void* buffer, size_t len, s32 flags) {
    // For connected sockets, use RecvFrom and discard the address
    if (m_state == ProxySocketState::Connected) {
        return RecvFrom(buffer, len, flags, nullptr);
    }

    // For unconnected sockets, Recv requires a default peer (connected state)
    return -static_cast<s32>(Errno::NotConn);
}

s32 ProxySocket::RecvFrom(void* buffer, size_t len, s32 flags, potluck::bsd::SockAddrIn* from) {
    // Check read shutdown
    if (m_shutdown_read) {
        return 0; // EOF
    }

    // Check socket state
    if (m_state == ProxySocketState::Closed) {
        return -static_cast<s32>(Errno::BadF);
    }

    // Validate buffer
    if (buffer == nullptr && len > 0) {
        return -static_cast<s32>(Errno::Fault);
    }

    const bool is_stream = m_type == potluck::bsd::SocketType::Stream;
    if (is_stream && m_state != ProxySocketState::Connected) {
        return -static_cast<s32>(Errno::NotConn);
    }

    bool peek = (flags & 0x2) != 0; // MSG_PEEK = 0x2
    bool dontwait = (flags & 0x80) != 0 || m_non_blocking; // MSG_DONTWAIT = 0x80 (Nintendo Switch)

    // Helper: copy front-of-ring into caller's buffer, pop unless peeking.
    // Caller must hold m_queue_mutex.
    // A datagram is read whole (the rest is dropped, as UDP does); a stream
    // keeps what did not fit for the next read.
    auto consume_front = [&](void* out_buf, size_t out_len) -> s32 {
        ReceivedPacket& slot = m_rx_ring[m_rx_head];
        const size_t available = static_cast<size_t>(slot.len) - slot.offset;
        size_t copy_len = std::min(out_len, available);
        if (copy_len > 0) {
            std::memcpy(out_buf, slot.data + slot.offset, copy_len);
        }
        if (from != nullptr) {
            *from = slot.from;
        }
        if (!peek) {
            if (is_stream && copy_len < available) {
                slot.offset = static_cast<uint16_t>(slot.offset + copy_len);
            } else {
                m_rx_head = (m_rx_head + 1) % PROXY_SOCKET_MAX_QUEUE_SIZE;
                m_rx_count--;
            }
        }
        if (m_rx_count == 0 && !m_peer_closed) {
            m_receive_event.Clear();
        }
        return static_cast<s32>(copy_len);
    };

    // Empty queue: a closed stream reads EOF, a reset stream reports it
    auto empty_result = [&]() -> s32 {
        if (is_stream && m_peer_closed) {
            if (m_socket_error != 0) {
                const s32 error = m_socket_error;
                m_socket_error = 0;
                return -error;
            }
            return 0;
        }
        return -static_cast<s32>(Errno::Again);
    };

    // Try to get data from queue
    {
        std::scoped_lock lock(m_queue_mutex);

        if (m_rx_count > 0) {
            return consume_front(buffer, len);
        }
        if (dontwait || (is_stream && m_peer_closed)) {
            return empty_result();
        }
    }

    // Blocking wait for data
    BlockOn(m_receive_event, 0);

    std::scoped_lock lock(m_queue_mutex);
    if (m_rx_count > 0) {
        return consume_front(buffer, len);
    }
    return empty_result();
}

void ProxySocket::IncomingData(const void* data, size_t len, const potluck::bsd::SockAddrIn& from) {
    // Hex-dump first 32 bytes — only format and log at VERBOSE level, since this
    // fires on every incoming packet and the snprintf loop is non-trivial under
    // load (~100+ packets/sec during gameplay).
    if (len > 0 && data != nullptr &&
        potluck::debug::g_logger.should_log(potluck::debug::LogLevel::Verbose)) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        size_t log_len = std::min(len, size_t(32));
        char hex_buf[128];
        char* p = hex_buf;
        for (size_t i = 0; i < log_len && (p - hex_buf) < 120; i++) {
            p += std::snprintf(p, 4, "%02X ", bytes[i]);
        }
        LOG_VERBOSE("ProxySocket IncomingData: %zu bytes, first %zu: %s", len, log_len, hex_buf);
    }

    // NOTE: Broadcast filtering is intentionally incomplete. When this socket
    // is bound to INADDR_ANY (local_ip == 0), we cannot determine the
    // original destination IP of an incoming packet, so filtering based on
    // SO_BROADCAST is not feasible. Since ProxySocketManager::RouteIncomingData
    // already delivers broadcast UDP to all matching sockets, and PIA mesh
    // discovery requires that broadcast reach every listener, skipping the
    // filter here is correct behaviour. The if-block is kept as a structural
    // anchor for a future destination-aware implementation if the proxy
    // protocol ever carries the destination address.

    // Clamp payload to fixed buffer size. UDP datagrams larger than our MTU
    // would already have been rejected by SendTo's MsgSize check on the
    // sender side; clamping here is a defensive guard.
    size_t copy_len = std::min(len, static_cast<size_t>(PROXY_SOCKET_MAX_PAYLOAD));

    std::scoped_lock lock(m_queue_mutex);

    if (m_type == potluck::bsd::SocketType::Stream) {
        if (m_state != ProxySocketState::Connected || m_peer_closed) {
            return;
        }
        // Small writes from the peer share a slot, so 32 slots hold up to
        // 32 full frames rather than 32 tiny messages.
        if (m_rx_count > 0 && len <= PROXY_SOCKET_MAX_PAYLOAD) {
            ReceivedPacket& tail = m_rx_ring[(m_rx_tail + PROXY_SOCKET_MAX_QUEUE_SIZE - 1) % PROXY_SOCKET_MAX_QUEUE_SIZE];
            if (static_cast<size_t>(tail.len) + len <= PROXY_SOCKET_MAX_PAYLOAD) {
                if (len > 0 && data != nullptr) {
                    std::memcpy(tail.data + tail.len, data, len);
                }
                tail.len = static_cast<uint16_t>(tail.len + len);
                m_receive_event.Signal();
                activity::Notify();
                return;
            }
        }
        if (m_rx_count >= PROXY_SOCKET_MAX_QUEUE_SIZE || len > PROXY_SOCKET_MAX_PAYLOAD) {
            // Dropping a stream segment would corrupt the stream, so reset it
            LOG_WARN("TCP port %u: receive queue full (%zu segments), resetting connection",
                     m_local_addr.GetPort(), m_rx_count);
            m_peer_closed = true;
            m_socket_error = static_cast<s32>(Errno::ConnReset);
            m_receive_event.Signal();
            activity::Notify();
            return;
        }
    } else if (m_rx_count >= PROXY_SOCKET_MAX_QUEUE_SIZE) {
        // Drop oldest if ring is full (UDP behavior)
        m_rx_head = (m_rx_head + 1) % PROXY_SOCKET_MAX_QUEUE_SIZE;
        m_rx_count--;
    }

    // Write into tail slot in-place — no heap allocation per packet.
    ReceivedPacket& slot = m_rx_ring[m_rx_tail];
    slot.from = from;
    slot.len = static_cast<uint16_t>(copy_len);
    slot.offset = 0;
    if (copy_len > 0 && data != nullptr) {
        std::memcpy(slot.data, data, copy_len);
    }

    m_rx_tail = (m_rx_tail + 1) % PROXY_SOCKET_MAX_QUEUE_SIZE;
    m_rx_count++;

    m_receive_event.Signal();
    activity::Notify();
}

// =============================================================================
// Socket Options
// =============================================================================

Result ProxySocket::SetSockOpt(s32 level, s32 optname, const void* optval, size_t optlen) {
    // Handle SOL_SOCKET level options
    if (level == static_cast<s32>(potluck::bsd::SocketOptionLevel::Socket)) {
        switch (static_cast<potluck::bsd::SocketOption>(optname)) {
            case potluck::bsd::SocketOption::Broadcast:
                // SO_BROADCAST - enable/disable broadcast reception
                if (optval != nullptr && optlen >= sizeof(s32)) {
                    s32 value = *reinterpret_cast<const s32*>(optval);
                    m_broadcast = (value != 0);
                    R_SUCCEED();
                }
                R_THROW(static_cast<s32>(Errno::Inval));

            case potluck::bsd::SocketOption::ReuseAddr:
            case potluck::bsd::SocketOption::KeepAlive:
            case potluck::bsd::SocketOption::DontRoute:
            case potluck::bsd::SocketOption::Linger:
            case potluck::bsd::SocketOption::OobInline:
            case potluck::bsd::SocketOption::ReusePort:
            case potluck::bsd::SocketOption::SndBuf:
            case potluck::bsd::SocketOption::RcvBuf:
            case potluck::bsd::SocketOption::SndLoWat:
            case potluck::bsd::SocketOption::RcvLoWat:
            case potluck::bsd::SocketOption::SndTimeo:
            case potluck::bsd::SocketOption::RcvTimeo:
                // Accept but ignore these common options
                R_SUCCEED();

            default:
                break;
        }
    }

    // Accept but ignore unknown options (compatibility)
    AMS_UNUSED(level, optname, optval, optlen);
    R_SUCCEED();
}

Result ProxySocket::GetSockOpt(s32 level, s32 optname, void* optval, size_t* optlen) const {
    if (optval == nullptr || optlen == nullptr) {
        R_THROW(static_cast<s32>(Errno::Fault));
    }

    // Handle specific options
    if (level == static_cast<s32>(potluck::bsd::SocketOptionLevel::Socket)) {
        switch (static_cast<potluck::bsd::SocketOption>(optname)) {
            case potluck::bsd::SocketOption::Type:
                // SO_TYPE - return socket type
                if (*optlen >= sizeof(s32)) {
                    *reinterpret_cast<s32*>(optval) = static_cast<s32>(m_type);
                    *optlen = sizeof(s32);
                    R_SUCCEED();
                }
                break;

            case potluck::bsd::SocketOption::Error:
                // SO_ERROR - pending error (non-blocking connect outcome), then cleared
                if (*optlen >= sizeof(s32)) {
                    std::scoped_lock lock(m_queue_mutex);
                    ExpireConnectLocked();
                    *reinterpret_cast<s32*>(optval) = m_socket_error;
                    if (!m_peer_closed) {
                        m_socket_error = 0;
                    }
                    *optlen = sizeof(s32);
                    R_SUCCEED();
                }
                break;

            case potluck::bsd::SocketOption::Broadcast:
                // SO_BROADCAST - return broadcast flag
                if (*optlen >= sizeof(s32)) {
                    *reinterpret_cast<s32*>(optval) = m_broadcast ? 1 : 0;
                    *optlen = sizeof(s32);
                    R_SUCCEED();
                }
                break;

            default:
                break;
        }
    }

    // Unknown option - return not supported
    R_THROW(static_cast<s32>(Errno::NoProtoOpt));
}

// =============================================================================
// TCP-specific Operations
// =============================================================================

Result ProxySocket::Listen(s32 backlog) {
    // Must be TCP
    if (m_type != potluck::bsd::SocketType::Stream) {
        R_THROW(static_cast<s32>(Errno::OpNotSupp));
    }

    // Must be bound, and not already connected
    if (m_state == ProxySocketState::Created) {
        R_THROW(static_cast<s32>(Errno::Inval));
    }
    if (m_state == ProxySocketState::Connected || m_state == ProxySocketState::Connecting) {
        R_THROW(static_cast<s32>(Errno::IsConn));
    }

    m_backlog = static_cast<size_t>(std::clamp<s32>(backlog, 1, static_cast<s32>(MaxAcceptBacklog)));
    m_state = ProxySocketState::Listening;
    LOG_INFO("TCP listen: 0x%08X:%u backlog %zu", LocalWireIp(), m_local_addr.GetPort(), m_backlog);
    R_SUCCEED();
}

bool ProxySocket::WaitForConnection(s32* out_errno) {
    while (true) {
        {
            std::scoped_lock lock(m_queue_mutex);
            if (m_state != ProxySocketState::Listening) {
                *out_errno = static_cast<s32>(m_state == ProxySocketState::Closed ? Errno::BadF : Errno::Inval);
                return false;
            }
            if (!m_accept_queue.empty()) {
                *out_errno = 0;
                return true;
            }
            if (m_non_blocking) {
                *out_errno = static_cast<s32>(Errno::Again);
                return false;
            }
        }

        // Ends on a new connection, Close() or the owner exiting
        if (!BlockOn(m_accept_event, 0)) {
            std::scoped_lock lock(m_queue_mutex);
            if (m_state == ProxySocketState::Listening && !m_accept_queue.empty()) {
                *out_errno = 0;
                return true;
            }
            *out_errno = static_cast<s32>(m_state == ProxySocketState::Closed ? Errno::BadF : Errno::ConnAborted);
            return false;
        }
    }
}

std::unique_ptr<ProxySocket> ProxySocket::PopAccepted() {
    std::scoped_lock lock(m_queue_mutex);
    if (m_state != ProxySocketState::Listening || m_accept_queue.empty()) {
        return nullptr;
    }
    auto accepted = std::move(m_accept_queue.front());
    m_accept_queue.pop_front();
    if (m_accept_queue.empty()) {
        m_accept_event.Clear();
    }
    return accepted;
}

bool ProxySocket::RouteToQueued(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port,
                                const void* data, size_t len, bool disconnect) {
    std::scoped_lock lock(m_queue_mutex);
    for (auto& pending : m_accept_queue) {
        if (!pending->MatchesStream(remote_ip, remote_port, local_port)) {
            continue;
        }
        if (disconnect) {
            pending->HandlePeerDisconnect();
        } else {
            pending->IncomingData(data, len, pending->GetRemoteAddr());
        }
        return true;
    }
    return false;
}

bool ProxySocket::IncomingConnection(const potluck::protocol::ProxyConnectRequest& request) {
    std::scoped_lock lock(m_queue_mutex);

    // Only accept on listening sockets
    if (m_state != ProxySocketState::Listening) {
        return false;
    }
    if (m_accept_queue.size() >= m_backlog) {
        LOG_WARN("TCP listen port %u: backlog %zu full, refusing 0x%08X:%u", m_local_addr.GetPort(),
                 m_backlog, request.info.source_ipv4, request.info.source_port);
        return false;
    }
    // Each socket carries a 64 KB receive ring; refuse rather than exhaust the heap
    if (!ProxySocketManager::HasHeapRoomForSocket()) {
        LOG_WARN("TCP listen port %u: not enough heap for another connection, refusing 0x%08X:%u",
                 m_local_addr.GetPort(), request.info.source_ipv4, request.info.source_port);
        return false;
    }

    std::unique_ptr<ProxySocket> accepted(new (std::nothrow) ProxySocket(m_type, m_protocol, m_owner_pid));
    if (accepted == nullptr) {
        LOG_WARN("TCP listen port %u: allocation failed, refusing connection", m_local_addr.GetPort());
        return false;
    }

    // Same local endpoint as the listener; the peer addresses us by it.
    // m_local_addr keeps the listener's format; the wire IP comes from the
    // request, which the peer sent to us.
    accepted->m_local_addr = m_local_addr;
    accepted->m_local_addr.sin_addr = request.info.dest_ipv4;  // wire format, as Bind stores it

    // Remote address in network byte order, like every address the game passes in
    accepted->m_remote_addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
    accepted->m_remote_addr.sin_len = sizeof(potluck::bsd::SockAddrIn);
    accepted->m_remote_addr.sin_addr = __builtin_bswap32(request.info.source_ipv4);
    accepted->m_remote_addr.sin_port = __builtin_bswap16(request.info.source_port);
    accepted->m_state = ProxySocketState::Connected;
    accepted->m_owns_port = false;  // The listener keeps the port reservation

    m_accept_queue.push_back(std::move(accepted));
    m_accept_event.Signal();
    activity::Notify();
    LOG_INFO("TCP listen port %u: queued connection from 0x%08X:%u (%zu pending)", m_local_addr.GetPort(),
             request.info.source_ipv4, request.info.source_port, m_accept_queue.size());
    return true;
}

void ProxySocket::HandleConnectResponse(const potluck::protocol::ProxyConnectResponse& response) {
    std::scoped_lock lock(m_queue_mutex);
    if (m_state != ProxySocketState::Connecting) {
        return;
    }

    // The relay rewrites an empty source IP to the sender's, so the reference client's
    // "source 0 = refused" never survives the trip; a reply is an accept.
    // Refusals arrive as ProxyDisconnect (HandleConnectRefused).
    m_connect_response = response;
    m_connect_response_received = true;
    m_state = ProxySocketState::Connected;
    m_socket_error = 0;
    LOG_INFO("TCP connect: port %u accepted by 0x%08X:%u", m_local_addr.GetPort(),
             response.info.source_ipv4, response.info.source_port);

    m_connect_event.Signal();
    activity::Notify();
}

void ProxySocket::HandlePeerDisconnect() {
    std::scoped_lock lock(m_queue_mutex);
    if (m_type != potluck::bsd::SocketType::Stream || m_peer_closed) {
        return;
    }
    m_peer_closed = true;
    LOG_INFO("TCP port %u: peer 0x%08X:%u closed the connection (%zu segments unread)",
             m_local_addr.GetPort(), RemoteWireIp(), m_remote_addr.GetPort(), m_rx_count);
    // Readers drain the queue, then see EOF
    m_receive_event.Signal();
    activity::Notify();
}

void ProxySocket::HandleConnectRefused() {
    std::scoped_lock lock(m_queue_mutex);
    if (m_state != ProxySocketState::Connecting) {
        return;
    }
    m_connect_response_received = true;
    m_state = ProxySocketState::Bound;
    m_socket_error = static_cast<s32>(Errno::ConnRefused);
    LOG_INFO("TCP connect: port %u refused by 0x%08X:%u", m_local_addr.GetPort(), RemoteWireIp(),
             m_remote_addr.GetPort());
    m_connect_event.Signal();
    activity::Notify();
}

bool ProxySocket::MatchesStream(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port) const {
    return m_type == potluck::bsd::SocketType::Stream &&
           m_state == ProxySocketState::Connected &&
           m_local_addr.GetPort() == local_port &&
           m_remote_addr.GetPort() == remote_port &&
           RemoteWireIp() == remote_ip;
}

uint32_t ProxySocket::LocalWireIp() const {
    // Bind stores INADDR_ANY binds in wire format and explicit LDN binds
    // as the game passed them (network order); accept either.
    const uint32_t stored = m_local_addr.sin_addr;
    if (potluck::bsd::IsLdnAddress(stored)) {
        return stored;
    }
    const uint32_t swapped = __builtin_bswap32(stored);
    if (potluck::bsd::IsLdnAddress(swapped)) {
        return swapped;
    }
    return ProxySocketManager::GetInstance().GetLocalIp();
}

void ProxySocket::ExpireConnectLocked() const {
    // A non-blocking connect nobody answers must still end, as it would on a
    // real socket; the blocking path uses the same 4 s.
    if (m_state != ProxySocketState::Connecting) {
        return;
    }
    const u64 elapsed_ms = armTicksToNs(armGetSystemTick() - m_connect_started_tick) / 1000000;
    if (elapsed_ms >= ConnectTimeoutMs) {
        m_state = ProxySocketState::Bound;
        m_socket_error = static_cast<s32>(Errno::TimedOut);
        activity::Notify();
        LOG_WARN("TCP connect: port %u got no reply within %llu ms", m_local_addr.GetPort(),
                 static_cast<unsigned long long>(ConnectTimeoutMs));
    }
}

bool ProxySocket::IsReadable() const {
    std::scoped_lock lock(m_queue_mutex);
    return m_rx_count > 0 || !m_accept_queue.empty() || m_peer_closed ||
           m_state == ProxySocketState::Closed;
}

bool ProxySocket::IsWritable() const {
    if (m_type != potluck::bsd::SocketType::Stream) {
        return true;  // Datagrams go straight to the relay
    }
    std::scoped_lock lock(m_queue_mutex);
    ExpireConnectLocked();
    // A failed non-blocking connect is also "writable" so the game reads SO_ERROR
    return (m_state == ProxySocketState::Connected && !m_peer_closed) ||
           (m_state == ProxySocketState::Bound && m_socket_error != 0);
}

bool ProxySocket::HasError() const {
    std::scoped_lock lock(m_queue_mutex);
    ExpireConnectLocked();
    return m_state == ProxySocketState::Closed || m_socket_error != 0;
}

bool ProxySocket::HasPendingConnections() const {
    std::scoped_lock lock(m_queue_mutex);
    return !m_accept_queue.empty();
}

// =============================================================================
// Shutdown and Close
// =============================================================================

Result ProxySocket::Shutdown(potluck::bsd::ShutdownHow how) {
    switch (how) {
        case potluck::bsd::ShutdownHow::Read:
            m_shutdown_read = true;
            break;
        case potluck::bsd::ShutdownHow::Write:
            m_shutdown_write = true;
            break;
        case potluck::bsd::ShutdownHow::Both:
            m_shutdown_read = true;
            m_shutdown_write = true;
            break;
        default:
            R_THROW(static_cast<s32>(Errno::Inval));
    }

    // Signal any blocked receivers
    m_receive_event.Signal();
    activity::Notify();

    R_SUCCEED();
}

Result ProxySocket::Close() {
    bool was_connected_stream = false;
    bool peer_closed = false;
    std::deque<std::unique_ptr<ProxySocket>> pending_accepts;
    {
        std::scoped_lock lock(m_queue_mutex);
        if (m_state == ProxySocketState::Closed) {
            R_SUCCEED();  // Idempotent: the game and a session reset may both close
        }
        was_connected_stream = m_type == potluck::bsd::SocketType::Stream &&
                               m_state == ProxySocketState::Connected;
        peer_closed = m_peer_closed;
        pending_accepts.swap(m_accept_queue);
        m_state = ProxySocketState::Closed;
    }
    m_shutdown_read = true;
    m_shutdown_write = true;

    // Wake every thread blocked on this socket
    m_receive_event.Signal();
    m_accept_event.Signal();
    m_connect_event.Signal();
    activity::Notify();

    // Clear ring queue (no deallocation — just reset indices)
    {
        std::scoped_lock lock(m_queue_mutex);
        m_rx_head = 0;
        m_rx_tail = 0;
        m_rx_count = 0;
    }

    // For a connected TCP stream the peer still believes in, send ProxyDisconnect
    if (was_connected_stream && !peer_closed) {
        LOG_INFO("TCP port %u: closing connection to 0x%08X:%u", m_local_addr.GetPort(),
                 RemoteWireIp(), m_remote_addr.GetPort());
        ProxySocketManager::GetInstance().SendProxyDisconnect(
            LocalWireIp(), m_local_addr.GetPort(), RemoteWireIp(), m_remote_addr.GetPort(),
            potluck::bsd::ProtocolType::Tcp);
    }

    // Queued but never accepted connections were promised to their peers; end them too
    for (auto& pending : pending_accepts) {
        Result close_result = pending->Close();
        AMS_UNUSED(close_result);
    }

    R_SUCCEED();
}

// =============================================================================
// Event Handling
// =============================================================================

bool ProxySocket::HasPendingData() const {
    std::scoped_lock lock(m_queue_mutex);
    return m_rx_count > 0;
}

size_t ProxySocket::GetPendingDataSize() const {
    std::scoped_lock lock(m_queue_mutex);
    size_t total = 0;
    for (size_t i = 0, idx = m_rx_head; i < m_rx_count; i++) {
        total += m_rx_ring[idx].len;
        idx = (idx + 1) % PROXY_SOCKET_MAX_QUEUE_SIZE;
    }
    return total;
}

bool ProxySocket::BlockOn(os::Event& event, u64 limit_ms) {
    const bool unbounded = limit_ms == 0;
    if (unbounded) {
        // Keeps a thread free for ldn:u and other calls; never cuts this wait short
        potluck::ipc_threads::OnUnboundedWaitBegin(m_local_addr.GetPort());
    }

    const char* reason = "timeout";
    bool signaled = false;
    u64 waited_ms = 0;
    for (u64 slice = 0;; slice++, waited_ms += BlockSliceMs) {
        if (m_state == ProxySocketState::Closed) {
            reason = "socket closed";
            break;
        }
        if (limit_ms != 0 && waited_ms >= limit_ms) {
            break;
        }
        if (slice != 0 && slice % OwnerCheckSlices == 0 && !IsProcessAlive(m_owner_pid)) {
            reason = "owner process exited";
            break;
        }
        if (event.TimedWait(TimeSpan::FromMilliSeconds(BlockSliceMs))) {
            reason = "signaled";
            signaled = true;
            break;
        }
    }

    if (unbounded) {
        potluck::ipc_threads::OnUnboundedWaitEnd();
    }
    if (waited_ms >= LongWaitLogMs || (unbounded && !signaled)) {
        LOG_INFO("ProxySocket: blocking wait ended after %llu ms: %s (local port %u, owner pid %llu)",
                 static_cast<unsigned long long>(waited_ms), reason, m_local_addr.GetPort(),
                 static_cast<unsigned long long>(m_owner_pid));
    }
    return signaled;
}

u64 ProxySocket::WaitUntilIdle() const {
    u64 waited_ms = 0;
    while (m_users.load() != 0) {
        os::SleepThread(TimeSpan::FromMilliSeconds(1));
        waited_ms++;
    }
    return waited_ms;
}

} // namespace ams::mitm::bsd
