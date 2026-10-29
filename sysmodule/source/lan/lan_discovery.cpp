/**
 * @file lan_discovery.cpp
 * @brief Local mode: host and join LDN rooms on the same Wi-Fi network
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original implementation)
 * @license GPL-2.0-or-later
 */

#include "lan_discovery.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <new>

#include "../debug/log.hpp"
#include "../ldn/ldn_shared_state.hpp"

namespace ams::mitm::ldn {

namespace {

using potluck::lan::PacketType;
using potluck::lan::PacketReader;

/// Error module ldn_mitm reports for its own failures
constexpr u32 LAN_MODULE = 0xFD;
/// nn::ldn module; description 32 is "wrong state"
constexpr u32 LDN_MODULE = 0xCB;
constexpr u32 LDN_WRONG_STATE = 32;

constexpr u16 LAN_MTU = 1500;
constexpr s64 WORKER_IDLE_NS = 10'000'000;      // 10 ms between polls, slept without the lock
constexpr int CONNECT_TIMEOUT_MS = 3000;
constexpr s64 SCAN_WAIT_NS = 1'000'000'000;     // replies collected for 1 s
constexpr s64 CONNECT_WAIT_NS = 1'000'000'000;  // the host's first SyncNetwork
constexpr s32 WORKER_PRIORITY = 0x15;
constexpr s32 WORKER_CORE = 2;
constexpr int LISTEN_BACKLOG = 10;
/// ldn_mitm's room name; games only compare it, never show it
constexpr const char* FAKE_SSID = "12345678123456781234567812345678";

Result LanError(u32 description) { return MAKERESULT(LAN_MODULE, description); }

// The game shows a failed setup step as its own error code (for example 2110-0106
// when the console has no usable network), so name the step and that code here.
Result LogFailure(Result rc, const char* step) {
    if (R_FAILED(rc)) {
        LOG_WARN("LAN: %s failed with %04u-%04u", step, 2000 + rc.GetModule(), rc.GetDescription());
    }
    return rc;
}
Result WrongState() { return MAKERESULT(LDN_MODULE, LDN_WRONG_STATE); }

/// nifm IRequest command 11; ldn_mitm's ipinfo.cpp. 2 asks for a local-only link.
Result SetLocalNetworkMode(NifmRequest* request, bool local) {
    const u8 option = local ? 2 : 4;
    serviceAssumeDomain(&request->s);
    return serviceDispatchIn(&request->s, 11, option);
}

bool SetNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/// connect() that gives up after CONNECT_TIMEOUT_MS; the host address came from a peer
bool ConnectWithTimeout(int fd, const sockaddr_in& host) {
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&host), sizeof(host)) == 0) return true;
    if (errno != EINPROGRESS) return false;
    pollfd pfd{fd, POLLOUT, 0};
    if (::poll(&pfd, 1, CONNECT_TIMEOUT_MS) != 1) return false;
    int error = 0;
    socklen_t length = sizeof(error);
    return ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
}

void CloseFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

Result SetSocketOptions(int fd) {
    int yes = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) != 0) return LanError(4);
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        LOG_WARN("LAN: SO_REUSEADDR failed");
    }
    return ResultSuccess();
}

sockaddr_in AnyAddress(u16 port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    return addr;
}

bool MatchesFilter(const NetworkInfo& info, const ScanFilter& filter) {
    const auto& want = filter.networkId.intentId;
    const auto& have = info.networkId.intentId;
    if ((filter.flag & ScanFilterFlag_LocalCommunicationId) && want.localCommunicationId != have.localCommunicationId) return false;
    if ((filter.flag & ScanFilterFlag_SessionId) && !(filter.networkId.sessionId == info.networkId.sessionId)) return false;
    if ((filter.flag & ScanFilterFlag_NetworkType) && filter.networkType != info.common.networkType) return false;
    if ((filter.flag & ScanFilterFlag_Ssid) && !(filter.ssid == info.common.ssid)) return false;
    if ((filter.flag & ScanFilterFlag_SceneId) && want.sceneId != have.sceneId) return false;
    return true;
}

} // namespace

LanDiscovery::~LanDiscovery() {
    // Also undoes a half-finished Initialize (open request, changed MTU, socket)
    static_cast<void>(Finalize());
    delete[] m_stack_memory;
}

// ============================================================================
// Lifecycle
// ============================================================================

Result LanDiscovery::Initialize(EventCallback callback, void* context) {
    std::scoped_lock lock(m_mutex);
    if (m_initialized) R_SUCCEED();

    const Result rc = SetUpLocked(callback, context);
    if (R_FAILED(rc)) {
        // Leave nothing half set up: a retry must not leak a submitted request
        static_cast<void>(TearDownLocked());
        return rc;
    }
    m_initialized = true;
    SetStateLocked(CommState::Initialized);
    LOG_INFO("LAN: initialized");
    R_SUCCEED();
}

Result LanDiscovery::SetUpLocked(EventCallback callback, void* context) {
    NifmNetworkProfileData profile;
    R_TRY(LogFailure(nifmGetCurrentNetworkProfile(&profile), "reading the network profile (is the console on Wi-Fi?)"));
    if (profile.ip_setting_data.mtu != LAN_MTU) {
        const u16 original = profile.ip_setting_data.mtu;
        profile.ip_setting_data.mtu = LAN_MTU;
        R_TRY(LogFailure(nifmSetNetworkProfile(&profile, &profile.uuid), "setting the network MTU"));
        m_original_mtu = original;
        m_mtu_changed = true;
    }

    R_TRY(LogFailure(nifmCreateRequest(&m_request, true), "creating the network request"));
    m_request_open = true;
    R_TRY(LogFailure(SetLocalNetworkMode(&m_request, true), "switching to local network mode"));
    R_TRY(LogFailure(nifmRequestSubmitAndWait(&m_request), "bringing up the network"));

    m_callback = callback;
    m_context = context;
    R_TRY(LogFailure(OpenUdp(), "opening the discovery socket"));

    if (m_stack_memory == nullptr) {
        m_stack_memory = new (std::nothrow) u8[STACK_SIZE + os::ThreadStackAlignment];
        R_UNLESS(m_stack_memory != nullptr, LanError(9));
    }
    void* stack = reinterpret_cast<void*>(util::AlignUp(reinterpret_cast<uintptr_t>(m_stack_memory), os::ThreadStackAlignment));
    m_stop = false;
    R_TRY(os::CreateThread(&m_thread, WorkerEntry, this, stack, STACK_SIZE, WORKER_PRIORITY, WORKER_CORE));
    os::SetThreadNamePointer(&m_thread, "potluck::Lan");
    os::StartThread(&m_thread);
    m_thread_running = true;
    R_SUCCEED();
}

Result LanDiscovery::TearDownLocked() {
    CloseFd(m_udp);
    CloseTcp();
    ResetStations();

    Result rc = ResultSuccess();
    if (m_request_open) {
        static_cast<void>(nifmRequestCancel(&m_request));
        nifmRequestClose(&m_request);
        m_request_open = false;
    }
    if (m_mtu_changed) {
        NifmNetworkProfileData profile;
        rc = nifmGetCurrentNetworkProfile(&profile);
        if (R_SUCCEEDED(rc)) {
            profile.ip_setting_data.mtu = m_original_mtu;
            rc = nifmSetNetworkProfile(&profile, &profile.uuid);
        }
        if (R_FAILED(rc)) LOG_WARN("LAN: could not restore MTU %u: 0x%x", m_original_mtu, rc.GetValue());
        m_mtu_changed = false;
    }
    m_scan_count = 0;
    return rc;
}

Result LanDiscovery::Finalize() {
    // The worker takes m_mutex, so it is joined before the lock is taken here
    if (m_thread_running) {
        m_stop = true;
        os::WaitThread(&m_thread);
        os::DestroyThread(&m_thread);
        m_thread_running = false;
    }

    std::scoped_lock lock(m_mutex);
    const bool was_active = m_initialized || m_state != CommState::None;
    const Result rc = TearDownLocked();
    m_initialized = false;
    std::memset(&m_network_info, 0, sizeof(m_network_info));
    // A session that never started must not clear the overlay's view of another one
    if (was_active) SetStateLocked(CommState::None);
    return rc;
}

CommState LanDiscovery::GetState() {
    std::scoped_lock lock(m_mutex);
    return m_state;
}

DisconnectReason LanDiscovery::GetDisconnectReason() {
    std::scoped_lock lock(m_mutex);
    return m_reason;
}

void LanDiscovery::SetStateLocked(CommState state) {
    m_state = state;
    SharedState::GetInstance().SetLdnState(state);
    PublishSessionLocked();
    Notify();
}

void LanDiscovery::Notify() {
    if (m_callback != nullptr) m_callback(m_context);
}

void LanDiscovery::PublishSessionLocked() {
    const bool is_host = m_state == CommState::AccessPointCreated;
    const bool in_room = is_host || m_state == CommState::StationConnected;
    if (!in_room) {
        SharedState::GetInstance().SetSessionInfo(0, 0, 0, false);
        return;
    }
    // The host is node 0; a station finds the id the host gave it by its address
    u8 local_id = 0;
    if (!is_host) {
        u32 address = 0;
        if (R_SUCCEEDED(nifmGetCurrentIpAddress(&address))) {
            address = ntohl(address);
            for (int i = 0; i < NodeCountMax; i++) {
                if (m_network_info.ldn.nodes[i].ipv4Address == address) local_id = static_cast<u8>(i);
            }
        }
    }
    SharedState::GetInstance().SetSessionInfo(m_network_info.ldn.nodeCount, m_network_info.ldn.nodeCountMax, local_id, is_host);
}

// ============================================================================
// Sockets
// ============================================================================

Result LanDiscovery::OpenUdp() {
    CloseFd(m_udp);
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    R_UNLESS(fd >= 0, LanError(1));
    const sockaddr_in addr = AnyAddress(potluck::lan::DEFAULT_PORT);
    Result rc = SetSocketOptions(fd);
    if (R_SUCCEEDED(rc) && ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) rc = LanError(2);
    if (R_FAILED(rc)) {
        ::close(fd);
        return rc;
    }
    m_udp = fd;
    R_SUCCEED();
}

Result LanDiscovery::OpenListener() {
    CloseTcp();
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    R_UNLESS(fd >= 0, LanError(6));
    const sockaddr_in addr = AnyAddress(potluck::lan::DEFAULT_PORT);
    Result rc = SetSocketOptions(fd);
    if (R_SUCCEEDED(rc) && ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) rc = LanError(7);
    if (R_SUCCEEDED(rc) && ::listen(fd, LISTEN_BACKLOG) != 0) rc = LanError(8);
    if (R_SUCCEEDED(rc) && !SetNonBlocking(fd)) rc = LanError(11);
    if (R_FAILED(rc)) {
        ::close(fd);
        return rc;
    }
    m_tcp = fd;
    R_SUCCEED();
}

void LanDiscovery::CloseTcp() {
    CloseFd(m_tcp);
    m_tcp_reader.Reset();
}

void LanDiscovery::CloseStationLocked(Station& station) {
    CloseFd(station.fd);
    station.status = NodeStatus::Disconnected;
    station.reader.Reset();
}

void LanDiscovery::ResetStations() {
    for (auto& station : m_stations) CloseStationLocked(station);
}

int LanDiscovery::SendTo(int fd, PacketType type, const void* body, size_t size, const void* addr) {
    u8 packet[potluck::lan::BUFFER_SIZE];
    const size_t length = potluck::lan::encode_packet(type, body, size, packet, sizeof(packet));
    if (length == 0 || fd < 0) return -1;
    // Never block while holding m_mutex: a peer that stops reading is dropped instead.
    // A short write leaves half a frame on the stream, so it fails too and the caller drops the peer.
    if (addr == nullptr) {
        const ssize_t sent = ::send(fd, packet, length, MSG_DONTWAIT);
        return sent == static_cast<ssize_t>(length) ? static_cast<int>(sent) : -1;
    }
    return static_cast<int>(::sendto(fd, packet, length, MSG_DONTWAIT, static_cast<const sockaddr*>(addr), sizeof(sockaddr_in)));
}

// ============================================================================
// Worker
// ============================================================================

void LanDiscovery::WorkerEntry(void* arg) {
    static_cast<LanDiscovery*>(arg)->Worker();
}

void LanDiscovery::Worker() {
    while (!m_stop) {
        PollOnce();
        os::SleepThread(TimeSpan::FromNanoSeconds(WORKER_IDLE_NS));
    }
    LOG_INFO("LAN: worker exit");
}

void LanDiscovery::PollOnce() {
    std::scoped_lock lock(m_mutex);
    constexpr int SLOTS = 2 + StationCountMax;
    pollfd fds[SLOTS];
    fds[0] = {m_udp, POLLIN, 0};
    fds[1] = {m_tcp, POLLIN, 0};
    for (int i = 0; i < StationCountMax; i++) fds[2 + i] = {m_stations[i].fd, POLLIN, 0};

    if (::poll(fds, SLOTS, 0) <= 0) return;  // the worker sleeps outside the lock instead

    constexpr short CLOSED = POLLERR | POLLHUP | POLLNVAL;
    if (fds[0].fd >= 0 && fds[0].revents != 0) {
        if (fds[0].revents & CLOSED) {
            CloseFd(m_udp);
            m_reason = DisconnectReason::SignalLost;
            SetStateLocked(CommState::Error);
        } else {
            OnUdpReadable();
        }
    }
    if (fds[1].fd >= 0 && fds[1].fd == m_tcp && fds[1].revents != 0) {
        if (fds[1].revents & CLOSED) OnHostLost();
        else OnTcpReadable();
    }
    for (int i = 0; i < StationCountMax; i++) {
        Station& station = m_stations[i];
        if (fds[2 + i].fd < 0 || fds[2 + i].fd != station.fd || fds[2 + i].revents == 0) continue;
        if (fds[2 + i].revents & CLOSED) {
            CloseStationLocked(station);
            UpdateNodesLocked();
        } else {
            OnStationReadable(station, i + 1);
        }
    }
}

void LanDiscovery::OnUdpReadable() {
    u8 datagram[potluck::lan::BUFFER_SIZE];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const ssize_t received = ::recvfrom(m_udp, datagram, sizeof(datagram), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received <= 0) return;

    PacketReader reader;
    if (!reader.Append(datagram, static_cast<size_t>(received))) return;
    PacketType type;
    u8 body[potluck::lan::MAX_BODY];
    size_t size = 0;
    bool error = false;
    if (!reader.Next(&type, body, &size, &error)) return;

    if (type == PacketType::Scan) {
        if (m_state == CommState::AccessPointCreated) {
            SendTo(m_udp, PacketType::ScanResp, &m_network_info, sizeof(m_network_info), &from);
        }
        return;
    }
    if (type != PacketType::ScanResp || size != sizeof(NetworkInfo)) return;

    NetworkInfo info;
    std::memcpy(&info, body, sizeof(info));
    for (size_t i = 0; i < m_scan_count; i++) {
        if (m_scan_results[i].common.bssid == info.common.bssid) {
            m_scan_results[i] = info;
            return;
        }
    }
    if (m_scan_count < MAX_SCAN_RESULTS) m_scan_results[m_scan_count++] = info;
}

void LanDiscovery::OnTcpReadable() {
    if (m_state == CommState::AccessPointCreated) {
        AcceptStation();
        return;
    }
    if (m_state != CommState::Station && m_state != CommState::StationConnected) {
        OnHostLost();
        return;
    }

    u8 chunk[potluck::lan::BUFFER_SIZE];
    // Read only what the reader can still hold; the rest waits in the socket
    const size_t room = m_tcp_reader.Free();
    const ssize_t received = room == 0 ? -1 : ::recv(m_tcp, chunk, room, MSG_DONTWAIT);
    if (received < 0 && room != 0 && errno == EAGAIN) return;
    if (received <= 0 || !m_tcp_reader.Append(chunk, static_cast<size_t>(received))) {
        OnHostLost();
        return;
    }

    PacketType type;
    u8 body[potluck::lan::MAX_BODY];
    size_t size = 0;
    bool error = false;
    while (m_tcp_reader.Next(&type, body, &size, &error) || error) {
        if (error || type != PacketType::SyncNetwork || size != sizeof(NetworkInfo)) {
            OnHostLost();
            return;
        }
        std::memcpy(&m_network_info, body, sizeof(m_network_info));
        if (m_state == CommState::Station) SetStateLocked(CommState::StationConnected);
        if (CollectNodeChangesLocked()) Notify();
        PublishSessionLocked();
    }
}

void LanDiscovery::OnHostLost() {
    LOG_INFO("LAN: host connection closed (state %u)", static_cast<u32>(m_state));
    CloseTcp();
    if (m_state == CommState::StationConnected) SetStateLocked(CommState::Station);
}

void LanDiscovery::AcceptStation() {
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    const int fd = ::accept(m_tcp, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if (fd < 0) return;
    if (!SetNonBlocking(fd)) {
        ::close(fd);
        return;
    }
    for (auto& station : m_stations) {
        if (station.status == NodeStatus::Disconnected) {
            station.fd = fd;
            station.status = NodeStatus::Accepted;
            station.reader.Reset();
            return;
        }
    }
    LOG_INFO("LAN: room full, refusing a station");
    ::close(fd);
}

void LanDiscovery::OnStationReadable(Station& station, int node_id) {
    u8 chunk[potluck::lan::BUFFER_SIZE];
    const size_t room = station.reader.Free();
    const ssize_t received = room == 0 ? -1 : ::recv(station.fd, chunk, room, MSG_DONTWAIT);
    if (received < 0 && room != 0 && errno == EAGAIN) return;
    if (received <= 0 || !station.reader.Append(chunk, static_cast<size_t>(received))) {
        CloseStationLocked(station);
        UpdateNodesLocked();
        return;
    }

    PacketType type;
    u8 body[potluck::lan::MAX_BODY];
    size_t size = 0;
    bool error = false;
    while (station.reader.Next(&type, body, &size, &error) || error) {
        if (error || type != PacketType::Connect || size != sizeof(NodeInfo)) {
            CloseStationLocked(station);
            UpdateNodesLocked();
            return;
        }
        std::memcpy(&m_network_info.ldn.nodes[node_id], body, sizeof(NodeInfo));
        m_network_info.ldn.nodes[node_id].userName[UserNameBytesMax] = '\0';
        station.status = NodeStatus::Connected;
        UpdateNodesLocked();
    }
}

// ============================================================================
// Room state
// ============================================================================

void LanDiscovery::UpdateNodesLocked() {
    if (m_state != CommState::AccessPointCreated) return;

    u8 connected = 0;
    for (int i = 0; i < StationCountMax; i++) {
        NodeInfo& node = m_network_info.ldn.nodes[i + 1];
        const bool is_connected = m_stations[i].status == NodeStatus::Connected;
        node.nodeId = static_cast<s8>(i + 1);
        node.isConnected = is_connected ? 1 : 0;
        if (is_connected) connected++;
    }
    m_network_info.ldn.nodeCount = static_cast<u8>(connected + 1);

    bool dropped = false;
    for (auto& station : m_stations) {
        if (station.status != NodeStatus::Connected) continue;
        if (SendTo(station.fd, PacketType::SyncNetwork, &m_network_info, sizeof(m_network_info), nullptr) < 0) {
            LOG_WARN("LAN: a station stopped reading; dropping it");
            CloseStationLocked(station);
            dropped = true;
        }
    }
    if (dropped) {
        UpdateNodesLocked();  // each pass closes a station, so this ends
        return;
    }

    if (CollectNodeChangesLocked()) Notify();
    PublishSessionLocked();
}

bool LanDiscovery::CollectNodeChangesLocked() {
    bool changed = false;
    for (int i = 0; i < NodeCountMax; i++) {
        const s8 now = m_network_info.ldn.nodes[i].isConnected;
        if (now == m_node_last[i]) continue;
        m_node_changes[i] |= static_cast<u8>(now ? NodeStateChange::Connect : NodeStateChange::Disconnect);
        m_node_last[i] = now;
        changed = true;
    }
    return changed;
}

void LanDiscovery::ResetNodeChangesLocked() {
    std::memset(m_node_changes, 0, sizeof(m_node_changes));
    std::memset(m_node_last, 0, sizeof(m_node_last));
}

Result LanDiscovery::FillOwnNode(NodeInfo* node, const UserConfig& user, u16 version) {
    u32 address = 0;
    R_TRY(nifmGetCurrentIpAddress(&address));
    address = ntohl(address);

    // ldn_mitm's fake MAC: 02:00 followed by the IPv4 address in host order
    node->macAddress.raw[0] = 0x02;
    node->macAddress.raw[1] = 0x00;
    std::memcpy(node->macAddress.raw + 2, &address, sizeof(address));
    node->ipv4Address = address;
    node->isConnected = 1;
    std::memcpy(node->userName, user.userName, UserNameBytesMax);
    node->userName[UserNameBytesMax] = '\0';
    node->localCommunicationVersion = static_cast<s16>(version);
    R_SUCCEED();
}

Result LanDiscovery::FillNetworkHeader() {
    NodeInfo own{};
    UserConfig nobody{};
    R_TRY(FillOwnNode(&own, nobody, 0));
    m_network_info.common.bssid = own.macAddress;
    m_network_info.common.channel = 6;
    m_network_info.common.linkLevel = 3;
    m_network_info.common.networkType = 2;
    m_network_info.common.ssid = FAKE_SSID;
    for (int i = 0; i < NodeCountMax; i++) {
        m_network_info.ldn.nodes[i].nodeId = static_cast<s8>(i);
        m_network_info.ldn.nodes[i].isConnected = 0;
    }
    R_SUCCEED();
}

// ============================================================================
// Game requests
// ============================================================================

Result LanDiscovery::OpenAccessPoint() {
    std::scoped_lock lock(m_mutex);
    m_reason = DisconnectReason::None;
    R_UNLESS(m_state != CommState::None, WrongState());
    CloseTcp();
    ResetStations();
    SetStateLocked(CommState::AccessPoint);
    R_SUCCEED();
}

Result LanDiscovery::CloseAccessPoint() {
    std::scoped_lock lock(m_mutex);
    R_UNLESS(m_state != CommState::None, WrongState());
    CloseTcp();
    ResetStations();
    SetStateLocked(CommState::Initialized);
    R_SUCCEED();
}

Result LanDiscovery::OpenStation() {
    std::scoped_lock lock(m_mutex);
    m_reason = DisconnectReason::None;
    R_UNLESS(m_state != CommState::None, WrongState());
    CloseTcp();
    ResetStations();
    SetStateLocked(CommState::Station);
    R_SUCCEED();
}

Result LanDiscovery::CloseStation() {
    std::scoped_lock lock(m_mutex);
    R_UNLESS(m_state != CommState::None, WrongState());
    CloseTcp();
    ResetStations();
    SetStateLocked(CommState::Initialized);
    R_SUCCEED();
}

Result LanDiscovery::CreateNetwork(const SecurityConfig& security, const UserConfig& user, const NetworkConfig& network) {
    std::scoped_lock lock(m_mutex);
    R_UNLESS(m_state == CommState::AccessPoint, WrongState());

    R_TRY(OpenListener());
    // SetAdvertiseData may come before CreateNetwork; keep what the game set
    const u16 advertise_size = m_network_info.ldn.advertiseDataSize;
    u8 advertise[AdvertiseDataSizeMax];
    std::memcpy(advertise, m_network_info.ldn.advertiseData, sizeof(advertise));
    std::memset(&m_network_info, 0, sizeof(m_network_info));
    m_network_info.ldn.advertiseDataSize = advertise_size;
    std::memcpy(m_network_info.ldn.advertiseData, advertise, sizeof(advertise));
    R_TRY(FillNetworkHeader());
    m_network_info.ldn.nodeCountMax = network.nodeCountMax;
    m_network_info.ldn.securityMode = security.securityMode;
    m_network_info.common.channel = network.channel == 0 ? 6 : static_cast<s16>(network.channel);
    os::GenerateRandomBytes(&m_network_info.networkId.sessionId, sizeof(SessionId));
    m_network_info.networkId.intentId = network.intentId;
    R_TRY(FillOwnNode(&m_network_info.ldn.nodes[0], user, network.localCommunicationVersion));

    ResetNodeChangesLocked();
    SetStateLocked(CommState::AccessPointCreated);
    UpdateNodesLocked();
    LOG_INFO("LAN: room created for 0x%016lx", network.intentId.localCommunicationId);
    R_SUCCEED();
}

Result LanDiscovery::DestroyNetwork() {
    std::scoped_lock lock(m_mutex);
    CloseTcp();
    ResetStations();
    SetStateLocked(CommState::AccessPoint);
    R_SUCCEED();
}

Result LanDiscovery::Connect(const NetworkInfo& info, const UserConfig& user, u16 local_communication_version) {
    R_UNLESS(info.ldn.nodeCount != 0, LanError(30));

    NodeInfo own{};
    R_TRY(FillOwnNode(&own, user, local_communication_version));

    // Connect outside the lock: the worker must keep serving while the host answers
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    R_UNLESS(fd >= 0, LanError(6));
    sockaddr_in host{};
    host.sin_family = AF_INET;
    host.sin_addr.s_addr = htonl(info.ldn.nodes[0].ipv4Address);
    host.sin_port = htons(potluck::lan::DEFAULT_PORT);
    if (!SetNonBlocking(fd) || !ConnectWithTimeout(fd, host)) {
        ::close(fd);
        LOG_INFO("LAN: connect to 0x%08x failed", info.ldn.nodes[0].ipv4Address);
        return LanError(31);
    }

    {
        std::scoped_lock lock(m_mutex);
        if (m_state != CommState::Station) {
            ::close(fd);
            return WrongState();
        }
        CloseTcp();
        m_tcp = fd;
        if (SendTo(m_tcp, PacketType::Connect, &own, sizeof(own), nullptr) < 0) {
            CloseTcp();
            return LanError(32);
        }
        ResetNodeChangesLocked();
    }

    os::SleepThread(TimeSpan::FromNanoSeconds(CONNECT_WAIT_NS));
    R_SUCCEED();
}

Result LanDiscovery::Disconnect() {
    std::scoped_lock lock(m_mutex);
    CloseTcp();
    SetStateLocked(CommState::Station);
    R_SUCCEED();
}

Result LanDiscovery::SetAdvertiseData(const u8* data, size_t size) {
    R_UNLESS(size <= AdvertiseDataSizeMax, LanError(10));
    std::scoped_lock lock(m_mutex);
    if (size > 0 && data != nullptr) std::memcpy(m_network_info.ldn.advertiseData, data, size);
    m_network_info.ldn.advertiseDataSize = static_cast<u16>(size);
    UpdateNodesLocked();
    R_SUCCEED();
}

Result LanDiscovery::Scan(NetworkInfo* out, u16* count, const ScanFilter& filter) {
    {
        std::scoped_lock lock(m_mutex);
        R_UNLESS(m_udp >= 0, LanError(20));
        m_scan_count = 0;
        u32 address = 0, netmask = 0, gateway = 0, dns1 = 0, dns2 = 0;
        R_TRY(nifmGetCurrentIpConfigInfo(&address, &netmask, &gateway, &dns1, &dns2));
        sockaddr_in broadcast{};
        broadcast.sin_family = AF_INET;
        broadcast.sin_addr.s_addr = address | ~netmask;  // both already in network order
        broadcast.sin_port = htons(potluck::lan::DEFAULT_PORT);
        R_UNLESS(SendTo(m_udp, PacketType::Scan, nullptr, 0, &broadcast) >= 0, LanError(20));
    }

    os::SleepThread(TimeSpan::FromNanoSeconds(SCAN_WAIT_NS));

    std::scoped_lock lock(m_mutex);
    u16 found = 0;
    for (size_t i = 0; i < m_scan_count && found < *count; i++) {
        if (MatchesFilter(m_scan_results[i], filter)) out[found++] = m_scan_results[i];
    }
    *count = found;
    R_SUCCEED();
}

Result LanDiscovery::GetNetworkInfo(NetworkInfo* out) {
    std::scoped_lock lock(m_mutex);
    R_UNLESS(m_state == CommState::AccessPointCreated || m_state == CommState::StationConnected, WrongState());
    *out = m_network_info;
    R_SUCCEED();
}

Result LanDiscovery::GetNetworkInfo(NetworkInfo* out, NodeLatestUpdate* updates, size_t update_count) {
    R_UNLESS(update_count <= NodeCountMax, LanError(50));
    std::scoped_lock lock(m_mutex);
    R_UNLESS(m_state == CommState::AccessPointCreated || m_state == CommState::StationConnected, WrongState());
    *out = m_network_info;
    for (size_t i = 0; i < update_count; i++) {
        updates[i].stateChange = m_node_changes[i];
        m_node_changes[i] = 0;
    }
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
