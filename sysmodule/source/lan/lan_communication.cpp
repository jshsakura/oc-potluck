/**
 * @file lan_communication.cpp
 * @brief ldn:u communication service for local mode
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original implementation)
 * @license GPL-2.0-or-later
 */

#include "lan_communication.hpp"

#include <arpa/inet.h>

#include "../debug/log.hpp"
#include "../ldn/ldn_shared_state.hpp"

namespace ams::mitm::ldn {

namespace {

/// nn::ldn module, as the relay service reports a disconnect
constexpr u32 LDN_DISCONNECT_MODULE = 0x10;

} // namespace

LanCommunicationService::LanCommunicationService()
    : m_state_event(os::EventClearMode_AutoClear, true) {
    LOG_INFO("LAN: communication service created");
}

LanCommunicationService::~LanCommunicationService() {
    LOG_INFO("LAN: communication service destroyed");
}

void LanCommunicationService::OnLanEvent(void* context) {
    static_cast<LanCommunicationService*>(context)->m_state_event.Signal();
}

// ============================================================================
// Lifecycle
// ============================================================================

Result LanCommunicationService::Initialize(const ams::sf::ClientProcessId& client_process_id) {
    m_client_process_id = client_process_id.GetValue().value;
    LOG_INFO("LAN: Initialize by process 0x%lx", m_client_process_id);
    R_TRY(m_discovery.Initialize(OnLanEvent, this));
    SharedState::GetInstance().SetGameActive(true, m_client_process_id);
    R_SUCCEED();
}

Result LanCommunicationService::InitializeSystem2(u64 unk, const ams::sf::ClientProcessId& client_process_id) {
    m_error_state = unk;
    return Initialize(client_process_id);
}

Result LanCommunicationService::Finalize() {
    LOG_INFO("LAN: Finalize");
    const Result rc = m_discovery.Finalize();
    SharedState::GetInstance().SetGameActive(false, 0);
    m_client_process_id = 0;
    m_error_state = 0;
    return rc;
}

// ============================================================================
// Queries
// ============================================================================

Result LanCommunicationService::GetState(ams::sf::Out<u32> state) {
    state.SetValue(static_cast<u32>(m_discovery.GetState()));
    const DisconnectReason reason = m_discovery.GetDisconnectReason();
    if (m_error_state != 0 && reason != DisconnectReason::None) {
        R_RETURN(MAKERESULT(LDN_DISCONNECT_MODULE, static_cast<u32>(reason)));
    }
    R_SUCCEED();
}

Result LanCommunicationService::GetNetworkInfo(ams::sf::Out<NetworkInfo> buffer) {
    return m_discovery.GetNetworkInfo(buffer.GetPointer());
}

Result LanCommunicationService::GetIpv4Address(ams::sf::Out<u32> address, ams::sf::Out<u32> mask) {
    u32 addr = 0, netmask = 0, gateway = 0, dns1 = 0, dns2 = 0;
    R_TRY(nifmGetCurrentIpConfigInfo(&addr, &netmask, &gateway, &dns1, &dns2));
    address.SetValue(ntohl(addr));
    mask.SetValue(ntohl(netmask));
    R_SUCCEED();
}

Result LanCommunicationService::GetDisconnectReason(ams::sf::Out<u32> reason) {
    reason.SetValue(static_cast<u32>(m_discovery.GetDisconnectReason()));
    R_SUCCEED();
}

Result LanCommunicationService::GetSecurityParameter(ams::sf::Out<SecurityParameter> out) {
    NetworkInfo info;
    R_TRY(m_discovery.GetNetworkInfo(&info));
    SecurityParameter param;
    NetworkInfo2SecurityParameter(&info, &param);
    out.SetValue(param);
    R_SUCCEED();
}

Result LanCommunicationService::GetNetworkConfig(ams::sf::Out<NetworkConfig> out) {
    NetworkInfo info;
    R_TRY(m_discovery.GetNetworkInfo(&info));
    NetworkConfig config;
    NetworkInfo2NetworkConfig(&info, &config);
    out.SetValue(config);
    R_SUCCEED();
}

Result LanCommunicationService::AttachStateChangeEvent(ams::sf::Out<ams::sf::CopyHandle> handle) {
    handle.SetValue(m_state_event.GetReadableHandle(), false);
    R_SUCCEED();
}

Result LanCommunicationService::GetNetworkInfoLatestUpdate(ams::sf::Out<NetworkInfo> buffer,
                                                           ams::sf::OutArray<NodeLatestUpdate> updates) {
    return m_discovery.GetNetworkInfo(buffer.GetPointer(), updates.GetPointer(), updates.GetSize());
}

Result LanCommunicationService::Scan(ams::sf::Out<u32> count, ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
                                     const ScanRequest& request) {
    const size_t capacity = buffer.GetSize();
    u16 found = capacity > 0xFFFF ? 0xFFFF : static_cast<u16>(capacity);
    const Result rc = m_discovery.Scan(buffer.GetPointer(), &found, request.filter);
    count.SetValue(R_SUCCEEDED(rc) ? found : 0);
    LOG_INFO("LAN: Scan found %u", R_SUCCEEDED(rc) ? found : 0);
    return rc;
}

// ============================================================================
// Access point and station
// ============================================================================

Result LanCommunicationService::OpenAccessPoint() { return m_discovery.OpenAccessPoint(); }
Result LanCommunicationService::CloseAccessPoint() { return m_discovery.CloseAccessPoint(); }
Result LanCommunicationService::DestroyNetwork() { return m_discovery.DestroyNetwork(); }
Result LanCommunicationService::OpenStation() { return m_discovery.OpenStation(); }
Result LanCommunicationService::CloseStation() { return m_discovery.CloseStation(); }
Result LanCommunicationService::Disconnect() { return m_discovery.Disconnect(); }

Result LanCommunicationService::CreateNetwork(const CreateNetworkConfig& data) {
    return m_discovery.CreateNetwork(data.securityConfig, data.userConfig, data.networkConfig);
}

Result LanCommunicationService::SetAdvertiseData(ams::sf::InAutoSelectBuffer data) {
    return m_discovery.SetAdvertiseData(data.GetPointer(), data.GetSize());
}

Result LanCommunicationService::Connect(const ConnectNetworkData& dat, const NetworkInfo& data) {
    return m_discovery.Connect(data, dat.userConfig, static_cast<u16>(dat.localCommunicationVersion));
}

// ============================================================================
// Commands ldn_mitm accepts without acting on them
// ============================================================================

Result LanCommunicationService::SetStationAcceptPolicy(u8 policy) {
    AMS_UNUSED(policy);
    R_SUCCEED();
}

Result LanCommunicationService::ScanPrivate(ams::sf::Out<u32> count, ams::sf::OutAutoSelectArray<NetworkInfo> buffer,
                                            const ScanRequest& request) {
    AMS_UNUSED(buffer, request);
    count.SetValue(0);
    R_SUCCEED();
}

Result LanCommunicationService::CreateNetworkPrivate(const CreateNetworkPrivateConfig& data,
                                                     ams::sf::InPointerBuffer address_list) {
    AMS_UNUSED(data, address_list);
    R_SUCCEED();
}

Result LanCommunicationService::ConnectPrivate(const ConnectPrivateData& data) {
    AMS_UNUSED(data);
    R_SUCCEED();
}

Result LanCommunicationService::SetWirelessControllerRestriction() { R_SUCCEED(); }

Result LanCommunicationService::Reject(u32 node_id) {
    AMS_UNUSED(node_id);
    R_SUCCEED();
}

Result LanCommunicationService::AddAcceptFilterEntry() { R_SUCCEED(); }
Result LanCommunicationService::ClearAcceptFilter() { R_SUCCEED(); }

} // namespace ams::mitm::ldn
