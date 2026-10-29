/**
 * @file lan_communication.hpp
 * @brief ldn:u communication service for local mode
 *
 * Handed to the game instead of the relay ICommunicationService when the
 * mode is local. Every request goes to LanDiscovery; the private-network
 * commands ldn_mitm never implemented succeed without doing anything, as
 * they do in ldn_mitm.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original implementation)
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "lan_discovery.hpp"
#include "../ldn/interfaces/icommunication.hpp"

namespace ams::mitm::ldn {

class LanCommunicationService {
public:
    LanCommunicationService();
    ~LanCommunicationService();

    Result GetState(ams::sf::Out<u32> state);
    Result GetNetworkInfo(ams::sf::Out<NetworkInfo> buffer);
    Result GetIpv4Address(ams::sf::Out<u32> address, ams::sf::Out<u32> mask);
    Result GetDisconnectReason(ams::sf::Out<u32> reason);
    Result GetSecurityParameter(ams::sf::Out<SecurityParameter> out);
    Result GetNetworkConfig(ams::sf::Out<NetworkConfig> out);
    Result AttachStateChangeEvent(ams::sf::Out<ams::sf::CopyHandle> handle);
    Result GetNetworkInfoLatestUpdate(ams::sf::Out<NetworkInfo> buffer, ams::sf::OutArray<NodeLatestUpdate> updates);
    Result Scan(ams::sf::Out<u32> count, ams::sf::OutAutoSelectArray<NetworkInfo> buffer, const ScanRequest& request);
    Result OpenAccessPoint();
    Result CloseAccessPoint();
    Result CreateNetwork(const CreateNetworkConfig& data);
    Result DestroyNetwork();
    Result SetAdvertiseData(ams::sf::InAutoSelectBuffer data);
    Result SetStationAcceptPolicy(u8 policy);
    Result OpenStation();
    Result CloseStation();
    Result Connect(const ConnectNetworkData& dat, const NetworkInfo& data);
    Result Disconnect();
    Result Initialize(const ams::sf::ClientProcessId& client_process_id);
    Result Finalize();
    Result InitializeSystem2(u64 unk, const ams::sf::ClientProcessId& client_process_id);
    Result ScanPrivate(ams::sf::Out<u32> count, ams::sf::OutAutoSelectArray<NetworkInfo> buffer, const ScanRequest& request);
    Result CreateNetworkPrivate(const CreateNetworkPrivateConfig& data, ams::sf::InPointerBuffer address_list);
    Result ConnectPrivate(const ConnectPrivateData& data);
    Result SetWirelessControllerRestriction();
    Result Reject(u32 node_id);
    Result AddAcceptFilterEntry();
    Result ClearAcceptFilter();

private:
    static void OnLanEvent(void* context);

    // Declared before m_discovery so it outlives the callbacks fired while
    // the discovery shuts down
    os::SystemEvent m_state_event;
    LanDiscovery m_discovery;
    u64 m_client_process_id = 0;
    u64 m_error_state = 0;
};
static_assert(IsICommunicationInterface<LanCommunicationService>);

} // namespace ams::mitm::ldn
