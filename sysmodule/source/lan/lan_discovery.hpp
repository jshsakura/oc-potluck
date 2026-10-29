/**
 * @file lan_discovery.hpp
 * @brief Local mode: host and join LDN rooms on the same Wi-Fi network
 *
 * A port of ldn_mitm's LANDiscovery (spacemeowx2/ldn_mitm, GPL-2.0). Rooms
 * are found with UDP broadcasts and joined over TCP on lan::DEFAULT_PORT;
 * game traffic then flows between the consoles' real addresses, so the bsd
 * MITM is not involved. The wire format is ldn_mitm's, so PotLuck in local
 * mode and ldn_mitm play together.
 *
 * Changes from ldn_mitm: fixed buffers instead of std::function and an
 * unbounded scan map, one mutex for every field the worker and the IPC
 * threads share, bounded copies of peer data, and the MTU is set to 1500
 * once and kept, instead of being restored on every exit.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original implementation)
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include <atomic>
#include "lan_protocol.hpp"
#include "../ldn/ldn_types.hpp"

namespace ams::mitm::ldn {

class LanDiscovery {
public:
    /// Called whenever the game should re-read the state; must not block
    using EventCallback = void (*)(void* context);

    LanDiscovery() = default;
    ~LanDiscovery();
    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;

    Result Initialize(EventCallback callback, void* context);
    Result Finalize();

    CommState GetState();
    DisconnectReason GetDisconnectReason();

    Result OpenAccessPoint();
    Result CloseAccessPoint();
    Result OpenStation();
    Result CloseStation();
    Result CreateNetwork(const SecurityConfig& security, const UserConfig& user, const NetworkConfig& network);
    Result DestroyNetwork();
    Result Connect(const NetworkInfo& info, const UserConfig& user, u16 local_communication_version);
    Result Disconnect();
    Result SetAdvertiseData(const u8* data, size_t size);
    Result Scan(NetworkInfo* out, u16* count, const ScanFilter& filter);
    Result GetNetworkInfo(NetworkInfo* out);
    Result GetNetworkInfo(NetworkInfo* out, NodeLatestUpdate* updates, size_t update_count);

private:
    enum class NodeStatus : u8 { Disconnected, Accepted, Connected };

    struct Station {
        int fd = -1;
        NodeStatus status = NodeStatus::Disconnected;
        potluck::lan::PacketReader reader;
    };

    /// Rooms remembered from ScanResp replies; ldn_mitm kept every one
    static constexpr size_t MAX_SCAN_RESULTS = 8;
    static constexpr size_t STACK_SIZE = 0x4000;

    static void WorkerEntry(void* arg);
    void Worker();
    void PollOnce();

    Result SetUpLocked(EventCallback callback, void* context);
    Result TearDownLocked();
    void SetStateLocked(CommState state);
    void Notify();

    Result OpenUdp();
    Result OpenListener();
    void CloseTcp();
    void ResetStations();
    void CloseStationLocked(Station& station);

    void OnUdpReadable();
    void OnTcpReadable();
    void OnStationReadable(Station& station, int node_id);
    void OnHostLost();
    void AcceptStation();

    void UpdateNodesLocked();
    bool CollectNodeChangesLocked();
    void ResetNodeChangesLocked();
    void PublishSessionLocked();

    Result FillOwnNode(NodeInfo* node, const UserConfig& user, u16 version);
    Result FillNetworkHeader();
    int SendTo(int fd, potluck::lan::PacketType type, const void* body, size_t size, const void* addr);

    os::SdkMutex m_mutex;
    EventCallback m_callback = nullptr;
    void* m_context = nullptr;

    int m_udp = -1;
    int m_tcp = -1;
    potluck::lan::PacketReader m_tcp_reader;
    Station m_stations[StationCountMax];

    NetworkInfo m_network_info{};
    NetworkInfo m_scan_results[MAX_SCAN_RESULTS];
    size_t m_scan_count = 0;
    u8 m_node_changes[NodeCountMax] = {};
    s8 m_node_last[NodeCountMax] = {};

    CommState m_state = CommState::None;
    DisconnectReason m_reason = DisconnectReason::None;

    NifmRequest m_request{};
    bool m_request_open = false;

    os::ThreadType m_thread{};
    u8* m_stack_memory = nullptr;
    bool m_thread_running = false;
    std::atomic<bool> m_stop{false};
    bool m_initialized = false;
};

} // namespace ams::mitm::ldn
