/**
 * @file ldn_mitm_service.cpp
 * @brief LDN MITM Service implementation
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_mitm_service.hpp"
#include "ldn_shared_state.hpp"
#include "../debug/log.hpp"
#include "../config/config_ipc_service.hpp"
#include "../lan/lan_communication.hpp"

namespace ams::mitm::ldn {

namespace {

/// The mode this game process plays in; the first ask from bsd:u or ldn:u fixes it
potluck::config::LdnMode LatchedMode(u64 pid) {
    const u32 now = static_cast<u32>(potluck::ipc::GetLdnMode());
    return static_cast<potluck::config::LdnMode>(SharedState::GetInstance().LatchMode(pid, now));
}

}  // namespace

LdnMitMService::LdnMitMService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c)
    : MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c)
    , m_program_id(c.program_id)
    , m_client_pid(c.process_id.value)
    , m_local(LatchedMode(c.process_id.value) == potluck::config::LdnMode::Local)
{
    LOG_INFO("LDN MITM service created for program_id=0x%016lx, pid=%lu, %s mode",
             c.program_id.value, m_client_pid, m_local ? "local" : "remote");

    // Register PID immediately so BSD MITM can intercept this process
    // This happens BEFORE Initialize() is called. Local mode uses the real
    // network, so the bsd MITM stays out of it.
    if (!m_local) SharedState::GetInstance().SetLdnPid(m_client_pid);
}

LdnMitMService::~LdnMitMService() {
    LOG_INFO("LDN MITM service destroyed for program_id=0x%016lx, pid=%lu",
             m_program_id.value, m_client_pid);

    // NOTE: Do NOT clear LDN PID here!
    // LdnMitMService is a factory that creates ICommunicationService, and is destroyed
    // right after. The ICommunicationService continues to live and the game will
    // open BSD sockets later. The PID must remain set so BSD MITM can intercept.
    // The PID will be cleared when ICommunicationService is destroyed.
}

bool LdnMitMService::ShouldMitm(const sm::MitmProcessInfo& client_info) {
    LOG_INFO("LDN ShouldMitm called for pid=%lu, program_id=0x%016lx",
             client_info.process_id.value, client_info.program_id.value);
    if (!potluck::ipc::IsInterceptAllowed()) return false;
    // Off: the game keeps Nintendo's own ldn:u
    return LatchedMode(client_info.process_id.value) != potluck::config::LdnMode::Off;
}

Result LdnMitMService::CreateUserLocalCommunicationService(
    sf::Out<sf::SharedPointer<ICommunicationInterface>> out)
{
    LOG_INFO("Creating UserLocalCommunicationService for program_id=0x%016lx", m_program_id.value);
    if (m_local) {
        auto lan = sf::CreateSharedObjectEmplaced<ICommunicationInterface, LanCommunicationService>();
        out.SetValue(std::move(lan));
        R_SUCCEED();
    }
    // Create our custom communication service with the client's program ID
    // The program_id is used to replace LocalCommunicationId=-1 with the real title ID
    auto service = sf::CreateSharedObjectEmplaced<ICommunicationInterface, ICommunicationService>(m_program_id);
    out.SetValue(std::move(service));
    R_SUCCEED();
}

Result LdnMitMService::CreateClientProcessMonitor(
    sf::Out<sf::SharedPointer<IClientProcessMonitorInterface>> out)
{
    LOG_INFO("Creating ClientProcessMonitor");
    // Required for firmware 18.0.0+ compatibility
    auto monitor = sf::CreateSharedObjectEmplaced<IClientProcessMonitorInterface, IClientProcessMonitor>();
    out.SetValue(std::move(monitor));
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
