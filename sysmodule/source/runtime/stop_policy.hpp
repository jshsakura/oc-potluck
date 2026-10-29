#pragma once
#include <atomic>
#include <cstdint>

namespace potluck::runtime {
/// Yielded: another module held ldn:u at boot, so PotLuck never intercepted anything
/// Disabled: the mode was off at boot, so the MITMs were never registered
enum class Phase : uint32_t { Initializing, Ready, Stopping, Detached, RebootRequired, Yielded, Disabled };
enum class StopResult : uint32_t { Prepared, GameActive, RebootRequired };

/** @brief Keep interception disabled after quiescing even if configuration is reloaded. */
constexpr bool CanIntercept(bool enabled, Phase phase) { return enabled && phase < Phase::Stopping; }

/** @brief Quiesce the worker threads before detaching their SM registrations. */
template<class Backend>
StopResult PrepareStop(std::atomic<Phase>& phase, Backend& backend) {
    if (phase.load() == Phase::Detached || phase.load() == Phase::Yielded || phase.load() == Phase::Disabled) {
        return StopResult::Prepared;
    }
    if (phase.load() != Phase::Ready) return StopResult::RebootRequired;
    const bool enabled = backend.BlockNewClients();
    const auto game_status = backend.CheckGame();
    if (game_status != StopResult::Prepared) {
        backend.RestoreEnabled(enabled);
        return game_status;
    }
    phase.store(Phase::Stopping);
    if (!backend.StopWorkers() || !backend.Detach(0) || !backend.Detach(1) ||
        !backend.IsDetached(0) || !backend.IsDetached(1)) {
        phase.store(Phase::RebootRequired);
        return StopResult::RebootRequired;
    }
    phase.store(Phase::Detached);
    return StopResult::Prepared;
}
}
