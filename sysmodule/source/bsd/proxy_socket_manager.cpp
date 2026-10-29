/**
 * @file proxy_socket_manager.cpp
 * @brief Implementation of the Proxy Socket Manager
 *
 * This file implements the central registry for proxy sockets.
 * See proxy_socket_manager.hpp for design documentation.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "proxy_socket_manager.hpp"
#include "../debug/log.hpp"

namespace ams::mitm {
    /// Bytes the sysmodule heap can still hand out in one block (main.cpp)
    size_t GetHeapAllocatableSize();
}

namespace ams::mitm::bsd {

// =============================================================================
// Singleton
// =============================================================================

ProxySocketManager& ProxySocketManager::GetInstance() {
    static ProxySocketManager instance;
    return instance;
}

// =============================================================================
// Socket Management
// =============================================================================

ProxySocketRef ProxySocketManager::CreateProxySocket(s32 fd, potluck::bsd::SocketType type,
                                                      potluck::bsd::ProtocolType protocol,
                                                      u64 owner_pid) {
    std::scoped_lock lock(m_mutex);

    // Check if fd already has a proxy socket
    if (auto it = m_sockets.find(fd); it != m_sockets.end()) {
        it->second->Retain();
        return ProxySocketRef::Adopt(it->second.get());
    }

    // Check limit
    if (m_sockets.size() >= MAX_PROXY_SOCKETS) {
        LOG_WARN("CreateProxySocket fd=%d refused: %zu proxy sockets already open", fd, m_sockets.size());
        return nullptr;
    }
    if (!HasHeapRoomForSocket()) {
        LOG_WARN("CreateProxySocket fd=%d refused: heap too low for another socket (%zu open)",
                 fd, m_sockets.size());
        return nullptr;
    }

    std::unique_ptr<ProxySocket> socket(new (std::nothrow) ProxySocket(type, protocol, owner_pid));
    if (socket == nullptr) {
        LOG_WARN("CreateProxySocket fd=%d refused: allocation failed", fd);
        return nullptr;
    }
    socket->Retain();
    ProxySocket* result = socket.get();
    m_sockets[fd] = std::move(socket);
    LOG_INFO("Proxy socket created: fd=%d %s owner pid %llu (%zu open)", fd,
             protocol == potluck::bsd::ProtocolType::Tcp ? "TCP" : "UDP",
             static_cast<unsigned long long>(owner_pid), m_sockets.size());

    return ProxySocketRef::Adopt(result);
}

ProxySocketRef ProxySocketManager::GetProxySocket(s32 fd) {
    std::scoped_lock lock(m_mutex);

    auto it = m_sockets.find(fd);
    if (it == m_sockets.end()) {
        return nullptr;
    }
    // Retained under m_mutex, so a concurrent close cannot free it first
    it->second->Retain();
    return ProxySocketRef::Adopt(it->second.get());
}

bool ProxySocketManager::IsProxySocket(s32 fd) const {
    std::scoped_lock lock(m_mutex);
    return m_sockets.find(fd) != m_sockets.end();
}

bool ProxySocketManager::CloseProxySocket(s32 fd) {
    std::unique_ptr<ProxySocket> socket;
    {
        std::scoped_lock lock(m_mutex);

        auto it = m_sockets.find(fd);
        if (it == m_sockets.end()) {
            return false;
        }

        socket = std::move(it->second);
        m_sockets.erase(it);

        if (socket != nullptr && socket->OwnsPort()) {
            const auto& local_addr = socket->GetLocalAddr();
            if (local_addr.GetPort() != 0) {
                m_port_pool.ReleasePort(local_addr.GetPort(), socket->GetProtocol());
            }
        }
    }

    // ProxySocket::Close() calls SendProxyDisconnect(), which takes m_mutex
    // (non-recursive). Closing while holding it deadlocked the BSD MITM thread
    // and every later socket call from the game.
    CloseDetachedSocket(socket);
    return true;
}

void ProxySocketManager::CloseAllProxySockets() {
    SocketMap sockets;
    {
        std::scoped_lock lock(m_mutex);
        sockets = std::move(m_sockets);
        m_sockets.clear();
        m_port_pool.ReleaseAll();
    }

    CloseDetachedSockets(sockets);
}

void ProxySocketManager::Reset() {
    SocketMap sockets;
    {
        std::scoped_lock lock(m_mutex);
        sockets = std::move(m_sockets);
        m_sockets.clear();

        m_port_pool.ReleaseAll();

        // Clear pending packets ring (no dealloc — just reset indices)
        m_pending_head = 0;
        m_pending_tail = 0;
        m_pending_count = 0;

        LOG_INFO("Proxy session summary: %zu sockets open, delivered=%u buffered=%u dropped=%u "
                 "sent=%u send_failed=%u tcp_refused=%u",
                 sockets.size(), m_stats.delivered.exchange(0), m_stats.buffered.exchange(0),
                 m_stats.dropped.exchange(0), m_stats.sent.exchange(0),
                 m_stats.send_failed.exchange(0), m_stats.tcp_refused.exchange(0));
        LOG_INFO("Proxy session TCP: connects out=%u, accepted in=%u, turned away=%u, unmatched segments=%u",
                 m_stats.tcp_out.exchange(0), m_stats.tcp_in.exchange(0),
                 m_stats.tcp_turned_away.exchange(0), m_stats.tcp_unmatched.exchange(0));

        m_local_ip = 0;

        m_send_callback = nullptr;
        m_proxy_connect_callback = nullptr;
        m_proxy_connect_reply_callback = nullptr;
        m_proxy_disconnect_callback = nullptr;
    }

    // Close outside m_mutex for the same reason as CloseProxySocket().
    CloseDetachedSockets(sockets);
}

void ProxySocketManager::CloseDetachedSocket(const std::unique_ptr<ProxySocket>& socket) {
    if (socket == nullptr) {
        return;
    }
    // Socket is destroyed by the caller regardless of the result
    Result close_result = socket->Close();
    AMS_UNUSED(close_result);
    WaitForLastUser(*socket);
}

void ProxySocketManager::CloseDetachedSockets(const SocketMap& sockets) {
    if (sockets.empty()) {
        return;
    }
    // Wake every socket before waiting on any, so a handler holding refs to
    // two of them is never stuck behind the one not yet closed.
    for (const auto& [fd, socket] : sockets) {
        AMS_UNUSED(fd);
        Result close_result = socket->Close();
        AMS_UNUSED(close_result);
    }
    for (const auto& [fd, socket] : sockets) {
        AMS_UNUSED(fd);
        WaitForLastUser(*socket);
    }
    LOG_INFO("Closed %zu proxy sockets", sockets.size());
}

void ProxySocketManager::WaitForLastUser(const ProxySocket& socket) {
    // A handler woken by Close() may still be running on the other IPC thread
    const u64 waited_ms = socket.WaitUntilIdle();
    if (waited_ms > 0) {
        LOG_INFO("Proxy socket port %u freed after waiting %llu ms for its last user",
                 socket.GetLocalAddr().GetPort(), static_cast<unsigned long long>(waited_ms));
    }
}

// =============================================================================
// Port Management
// =============================================================================

uint16_t ProxySocketManager::AllocatePort(potluck::bsd::ProtocolType protocol) {
    return m_port_pool.AllocatePort(protocol);
}

bool ProxySocketManager::ReservePort(uint16_t port, potluck::bsd::ProtocolType protocol) {
    return m_port_pool.AllocateSpecificPort(port, protocol);
}

void ProxySocketManager::ReleasePort(uint16_t port, potluck::bsd::ProtocolType protocol) {
    m_port_pool.ReleasePort(port, protocol);
}

// =============================================================================
// Outgoing Data
// =============================================================================

void ProxySocketManager::SetSendCallback(SendProxyDataCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_send_callback = callback;
}

ProxySendResult ProxySocketManager::SendProxyData(uint32_t source_ip, uint16_t source_port,
                                                   uint32_t dest_ip, uint16_t dest_port,
                                                   potluck::bsd::ProtocolType protocol,
                                                   const void* data, size_t data_len, bool may_wait) {
    SendProxyDataCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_send_callback;
    }

    if (callback == nullptr) {
        m_stats.send_failed.fetch_add(1);
        LOG_WARN("ProxySocketManager::SendProxyData: m_send_callback=nullptr (src=0x%08X:%u dst=0x%08X:%u)",
                 source_ip, source_port, dest_ip, dest_port);
        return ProxySendResult::Failed;
    }

    const ProxySendResult result = callback(source_ip, source_port, dest_ip, dest_port, protocol, data, data_len, may_wait);
    if (result == ProxySendResult::WouldBlock) {
        return result;  // the game asked not to wait and will try again; not a failure
    }
    (result == ProxySendResult::Sent ? m_stats.sent : m_stats.send_failed).fetch_add(1);
    if (result == ProxySendResult::Failed) {
        LOG_WARN("ProxySocketManager::SendProxyData: callback failed (src=0x%08X:%u dst=0x%08X:%u len=%zu)",
                 source_ip, source_port, dest_ip, dest_port, data_len);
    }
    return result;
}

void ProxySocketManager::SetProxyConnectCallback(SendProxyConnectCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_connect_callback = callback;
}

bool ProxySocketManager::SendProxyConnect(uint32_t source_ip, uint16_t source_port,
                                           uint32_t dest_ip, uint16_t dest_port,
                                           potluck::bsd::ProtocolType protocol) {
    SendProxyConnectCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_connect_callback;
    }

    if (callback == nullptr) {
        // Registered by ICommunicationService::ConnectToServer; null means no
        // relay session is up (or it was just reset).
        m_stats.tcp_refused.fetch_add(1);
        LOG_WARN("TCP over LDN unavailable (no relay session): connect 0x%08X:%u -> 0x%08X:%u refused",
                 source_ip, source_port, dest_ip, dest_port);
        return false;
    }
    m_stats.tcp_out.fetch_add(1);

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

void ProxySocketManager::SetProxyConnectReplyCallback(SendProxyConnectReplyCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_connect_reply_callback = callback;
}

bool ProxySocketManager::SendProxyConnectReply(uint32_t source_ip, uint16_t source_port,
                                                uint32_t dest_ip, uint16_t dest_port,
                                                potluck::bsd::ProtocolType protocol) {
    SendProxyConnectReplyCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_connect_reply_callback;
    }

    if (callback == nullptr) {
        return false;
    }

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

void ProxySocketManager::SetProxyDisconnectCallback(SendProxyDisconnectCallback callback) {
    std::scoped_lock lock(m_mutex);
    m_proxy_disconnect_callback = callback;
}

bool ProxySocketManager::SendProxyDisconnect(uint32_t source_ip, uint16_t source_port,
                                              uint32_t dest_ip, uint16_t dest_port,
                                              potluck::bsd::ProtocolType protocol) {
    SendProxyDisconnectCallback callback;
    {
        std::scoped_lock lock(m_mutex);
        callback = m_proxy_disconnect_callback;
    }

    if (callback == nullptr) {
        return false;
    }

    return callback(source_ip, source_port, dest_ip, dest_port, protocol);
}

uint32_t ProxySocketManager::WireIpLocked(const ProxySocket& socket) const {
    // Same rule as ProxySocket::LocalWireIp, without re-taking m_mutex
    const uint32_t stored = socket.GetLocalAddr().sin_addr;
    if (potluck::bsd::IsLdnAddress(stored)) {
        return stored;
    }
    const uint32_t swapped = __builtin_bswap32(stored);
    return potluck::bsd::IsLdnAddress(swapped) ? swapped : m_local_ip;
}

bool ProxySocketManager::RouteConnectResponse(const potluck::protocol::ProxyConnectResponse& response) {
    // The acceptor replies with itself as source and us as destination
    const auto& info = response.info;
    {
        std::scoped_lock lock(m_mutex);
        for (auto& [fd, socket] : m_sockets) {
            AMS_UNUSED(fd);
            if (socket->GetState() == ProxySocketState::Connecting &&
                socket->GetLocalAddr().GetPort() == info.dest_port &&
                socket->GetRemoteAddr().GetPort() == info.source_port &&
                socket->RemoteWireIp() == info.source_ipv4) {
                socket->HandleConnectResponse(response);
                return true;
            }
        }
    }

    // Late (we already timed out) or stray: the acceptor now holds a socket
    // nobody will use, so tell it the connection is gone.
    LOG_WARN("TCP: ProxyConnectReply 0x%08X:%u -> port %u matches no pending connect, closing it",
             info.source_ipv4, info.source_port, info.dest_port);
    SendProxyDisconnect(info.dest_ipv4, info.dest_port, info.source_ipv4, info.source_port,
                        potluck::bsd::ProtocolType::Tcp);
    return false;
}

bool ProxySocketManager::RouteConnectRequest(const potluck::protocol::ProxyConnectRequest& request) {
    const auto& info = request.info;
    bool queued = false;
    bool has_listener = false;
    {
        std::scoped_lock lock(m_mutex);
        for (auto& [fd, socket] : m_sockets) {
            AMS_UNUSED(fd);
            if (socket->GetState() != ProxySocketState::Listening ||
                socket->GetProtocol() != potluck::bsd::ProtocolType::Tcp ||
                socket->GetLocalAddr().GetPort() != info.dest_port) {
                continue;
            }
            const uint32_t local_ip = WireIpLocked(*socket);
            if (local_ip != 0 && local_ip != info.dest_ipv4) {
                continue;
            }
            has_listener = true;
            queued = socket->IncomingConnection(request);
            break;
        }
    }

    // Replies go out after m_mutex is released: the send callbacks take it.
    if (queued) {
        m_stats.tcp_in.fetch_add(1);
        SendProxyConnectReply(info.dest_ipv4, info.dest_port, info.source_ipv4, info.source_port,
                              potluck::bsd::ProtocolType::Tcp);
        return true;
    }

    // The relay turns a zero source IP into the sender's, so a "refused"
    // reply cannot be expressed; a ProxyDisconnect ends the attempt instead.
    m_stats.tcp_turned_away.fetch_add(1);
    LOG_INFO("TCP: refusing connect 0x%08X:%u -> port %u (%s)", info.source_ipv4, info.source_port,
             info.dest_port, has_listener ? "listener cannot take it" : "nothing listening");
    SendProxyDisconnect(info.dest_ipv4, info.dest_port, info.source_ipv4, info.source_port,
                        potluck::bsd::ProtocolType::Tcp);
    return false;
}

bool ProxySocketManager::RouteDisconnect(const potluck::protocol::ProxyDisconnectMessage& message) {
    const auto& info = message.info;
    std::scoped_lock lock(m_mutex);
    for (auto& [fd, socket] : m_sockets) {
        AMS_UNUSED(fd);
        if (socket->GetType() != potluck::bsd::SocketType::Stream ||
            socket->GetLocalAddr().GetPort() != info.dest_port ||
            socket->GetRemoteAddr().GetPort() != info.source_port ||
            socket->RemoteWireIp() != info.source_ipv4) {
            continue;
        }
        if (socket->GetState() == ProxySocketState::Connecting) {
            socket->HandleConnectRefused();
        } else {
            socket->HandlePeerDisconnect();
        }
        return true;
    }
    for (auto& [fd, socket] : m_sockets) {
        AMS_UNUSED(fd);
        if (socket->GetState() == ProxySocketState::Listening &&
            socket->GetLocalAddr().GetPort() == info.dest_port &&
            socket->RouteToQueued(info.source_ipv4, info.source_port, info.dest_port, nullptr, 0, true)) {
            return true;
        }
    }
    LOG_VERBOSE("TCP: ProxyDisconnect 0x%08X:%u -> port %u matches no connection",
                info.source_ipv4, info.source_port, info.dest_port);
    return false;
}

ProxySocketRef ProxySocketManager::AdoptFromListener(ProxySocket& listener, s32 fd,
                                                     potluck::bsd::SockAddrIn* out_remote,
                                                     s32* out_errno) {
    // Pop and register under m_mutex in one step, so a segment routed by the
    // receive thread always finds the connection, queued or registered.
    std::scoped_lock lock(m_mutex);
    if (m_sockets.find(fd) != m_sockets.end() || m_sockets.size() >= MAX_PROXY_SOCKETS) {
        LOG_WARN("TCP: cannot register accepted socket as fd=%d (%zu open)", fd, m_sockets.size());
        *out_errno = static_cast<s32>(potluck::bsd::BsdErrno::MFile);
        return nullptr;
    }
    std::unique_ptr<ProxySocket> accepted = listener.PopAccepted();
    if (accepted == nullptr) {
        *out_errno = 0;  // Another thread took it, or the listener closed: retry
        return nullptr;
    }
    *out_errno = 0;
    if (out_remote != nullptr) {
        *out_remote = accepted->GetRemoteAddr();
    }
    accepted->Retain();
    ProxySocket* result = accepted.get();
    m_sockets[fd] = std::move(accepted);
    LOG_INFO("TCP accepted socket registered as fd=%d (%zu open)", fd, m_sockets.size());
    return ProxySocketRef::Adopt(result);
}

bool ProxySocketManager::HasHeapRoomForSocket() {
    // One socket plus a small reserve for the std containers, log lines and
    // IPC buffers that allocate later and do not check for null. Kept small so
    // a UDP game that worked before is only refused where it would have crashed.
    constexpr size_t ReserveBytes = 16 * 1024;
    return ams::mitm::GetHeapAllocatableSize() >= sizeof(ProxySocket) + ReserveBytes;
}

// =============================================================================
// Data Routing
// =============================================================================

bool ProxySocketManager::RouteIncomingData(uint32_t source_ip, uint16_t source_port,
                                            uint32_t dest_ip, uint16_t dest_port,
                                            potluck::bsd::ProtocolType protocol,
                                            const void* data, size_t data_len) {
    std::scoped_lock lock(m_mutex);

    if (protocol == potluck::bsd::ProtocolType::Tcp) {
        return RouteStreamDataLocked(source_ip, source_port, dest_port, data, data_len);
    }

    // For broadcast/multicast packets, deliver to ALL matching sockets.
    // PIA mesh discovery relies on broadcast UDP reaching every listener
    // on the same port. For unicast, only one socket matches.
    constexpr size_t kMaxSockets = 8;
    ProxySocket* targets[kMaxSockets];
    size_t match_count = FindAllSocketsByDestination(dest_ip, dest_port, protocol,
                                                      targets, kMaxSockets);

    if (match_count == 0) {
        // No matching socket — buffer into fixed-size ring for post-bind delivery.
        if (data_len > PendingPayloadMax) {
            // A cut datagram would read as a whole one; dropped like a full queue drops it
            m_stats.dropped.fetch_add(1);
            return false;
        }
        m_stats.buffered.fetch_add(1);
        if (m_pending_count >= MaxPendingPackets) {
            // Drop oldest
            m_stats.dropped.fetch_add(1);
            m_pending_head = (m_pending_head + 1) % MaxPendingPackets;
            m_pending_count--;
        }
        PendingPacket& slot = m_pending_ring[m_pending_tail];
        slot.source_ip = source_ip;
        slot.source_port = source_port;
        slot.dest_ip = dest_ip;
        slot.dest_port = dest_port;
        slot.protocol = protocol;
        slot.len = static_cast<uint16_t>(data_len);
        if (data_len > 0 && data != nullptr) {
            std::memcpy(slot.data, data, data_len);
        }
        m_pending_tail = (m_pending_tail + 1) % MaxPendingPackets;
        m_pending_count++;
        return false;
    }

    // Build source address for RecvFrom
    // Note: Nintendo Switch uses BSD-style sockaddr with sin_len field
    // sin_len must be sizeof(SockAddrIn) = 16 for the game to accept the address
    //
    // CRITICAL: The wire format stores IPs as uint in "big-endian read" format:
    // For 10.114.0.1, The wire format stores 0x0A720001.
    // BUT in BsdSockAddr, sin_addr must be in NETWORK BYTE ORDER (standard BSD).
    // The reference client does Array.Reverse() when converting ProxyInfo.SourceIpV4 to IPEndPoint,
    // then copies bytes directly to sin_addr, resulting in network byte order.
    // So we must bswap32 to convert wire format -> network byte order.
    // For 10.114.0.1: 0x0A720001 -> bswap32 -> 0x0100720A (network order)
    potluck::bsd::SockAddrIn from_addr{};
    from_addr.sin_len = sizeof(potluck::bsd::SockAddrIn);
    from_addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
    from_addr.sin_port = __builtin_bswap16(source_port);
    from_addr.sin_addr = __builtin_bswap32(source_ip);  // Convert wire format to network byte order

    // Deliver to all matching sockets (critical for broadcast UDP)
    for (size_t i = 0; i < match_count; i++) {
        targets[i]->IncomingData(data, data_len, from_addr);
    }
    m_stats.delivered.fetch_add(static_cast<u32>(match_count));

    return true;
}

bool ProxySocketManager::RouteStreamDataLocked(uint32_t source_ip, uint16_t source_port,
                                               uint16_t dest_port, const void* data, size_t data_len) {
    // Accepted connections share the listener's port, so a segment belongs to
    // the one socket whose peer sent it.
    for (auto& [fd, socket] : m_sockets) {
        AMS_UNUSED(fd);
        if (socket->MatchesStream(source_ip, source_port, dest_port)) {
            potluck::bsd::SockAddrIn from_addr = socket->GetRemoteAddr();
            socket->IncomingData(data, data_len, from_addr);
            m_stats.delivered.fetch_add(1);
            return true;
        }
    }
    // Not accepted yet: the connection still sits in its listener's queue
    for (auto& [fd, socket] : m_sockets) {
        AMS_UNUSED(fd);
        if (socket->GetState() == ProxySocketState::Listening &&
            socket->GetLocalAddr().GetPort() == dest_port &&
            socket->RouteToQueued(source_ip, source_port, dest_port, data, data_len, false)) {
            m_stats.delivered.fetch_add(1);
            return true;
        }
    }
    const u32 unmatched = m_stats.tcp_unmatched.fetch_add(1) + 1;
    if (unmatched == 1 || unmatched % 100 == 0) {
        LOG_WARN("TCP: segment 0x%08X:%u -> port %u matches no connection, dropped (%u so far)",
                 source_ip, source_port, dest_port, unmatched);
    }
    return false;
}

/// Whether a socket bound to `local_ip` receives a datagram sent to `dest_ip`:
/// any address, its own, or a broadcast on its /16
static bool AcceptsDestination(uint32_t local_ip, uint32_t dest_ip) {
    const bool is_broadcast = ((dest_ip & 0x000000FF) == 0x000000FF) ||
                              ((dest_ip & 0x0000FFFF) == 0x0000FFFF);
    return local_ip == 0 || local_ip == dest_ip ||
           (is_broadcast && (local_ip & 0xFFFF0000) == (dest_ip & 0xFFFF0000));
}

size_t ProxySocketManager::FindAllSocketsByDestination(uint32_t dest_ip, uint16_t dest_port,
                                                          potluck::bsd::ProtocolType protocol,
                                                          ProxySocket* out_sockets[], size_t max_sockets) {
    // Caller must hold m_mutex

    size_t count = 0;

    for (auto& [fd, socket] : m_sockets) {
        if (socket == nullptr) {
            continue;
        }
        if (count >= max_sockets) {
            break;
        }

        if (socket->GetProtocol() != protocol) {
            continue;
        }

        const auto& local_addr = socket->GetLocalAddr();

        if (local_addr.GetPort() != dest_port) {
            continue;
        }

        if (AcceptsDestination(local_addr.sin_addr, dest_ip)) {
            out_sockets[count++] = socket.get();
        }
    }

    return count;
}

// =============================================================================
// LDN Network Configuration
// =============================================================================

void ProxySocketManager::SetLocalIp(uint32_t ip) {
    std::scoped_lock lock(m_mutex);
    m_local_ip = ip;
}

uint32_t ProxySocketManager::GetLocalIp() const {
    std::scoped_lock lock(m_mutex);
    return m_local_ip;
}

// =============================================================================
// Statistics
// =============================================================================

size_t ProxySocketManager::GetActiveSocketCount() const {
    std::scoped_lock lock(m_mutex);
    return m_sockets.size();
}

size_t ProxySocketManager::GetAvailablePortCount(potluck::bsd::ProtocolType protocol) const {
    return m_port_pool.GetAvailableCount(protocol);
}

// =============================================================================
// Pending Packet Delivery
// =============================================================================

void ProxySocketManager::DeliverPendingPackets(ProxySocket* socket, uint16_t port,
                                                potluck::bsd::ProtocolType protocol) {
    // Called after bind - deliver any buffered packets matching this socket
    // Caller must NOT hold m_mutex (we take it here)
    std::scoped_lock lock(m_mutex);

    if (socket == nullptr || m_pending_count == 0) {
        return;
    }

    // Walk the ring in arrival order, delivering matches and compacting in place.
    size_t read_idx = m_pending_head;
    size_t write_idx = m_pending_head;
    size_t remaining = m_pending_count;
    size_t kept = 0;

    while (remaining > 0) {
        const PendingPacket& pkt = m_pending_ring[read_idx];

        if (pkt.protocol == protocol && pkt.dest_port == port &&
            AcceptsDestination(socket->GetLocalAddr().sin_addr, pkt.dest_ip)) {
            potluck::bsd::SockAddrIn from_addr{};
            from_addr.sin_len = sizeof(potluck::bsd::SockAddrIn);
            from_addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
            from_addr.sin_port = __builtin_bswap16(pkt.source_port);
            from_addr.sin_addr = __builtin_bswap32(pkt.source_ip);  // wire -> network byte order

            socket->IncomingData(pkt.data, pkt.len, from_addr);
            // Drop this slot by not copying it forward
        } else {
            if (write_idx != read_idx) {
                m_pending_ring[write_idx] = pkt;
            }
            write_idx = (write_idx + 1) % MaxPendingPackets;
            kept++;
        }

        read_idx = (read_idx + 1) % MaxPendingPackets;
        remaining--;
    }

    m_pending_tail = write_idx;
    m_pending_count = kept;
}

} // namespace ams::mitm::bsd
