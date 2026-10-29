#include <cstdio>
#include <cstdlib>
#include "../sysmodule/source/runtime/stop_policy.hpp"
using namespace potluck::runtime;
struct Backend {
    bool active = false;
    bool game_check_ok = true;
    bool enabled = true;
    bool workers = false;
    bool stop_ok = true;
    int fail_detach = -1;
    int fail_verify = -1;
    int detached = 0;
    bool BlockNewClients() { bool old = enabled; enabled = false; return old; }
    void RestoreEnabled(bool old) { enabled = old; }
    StopResult CheckGame() {
        return !game_check_ok ? StopResult::RebootRequired : active ? StopResult::GameActive : StopResult::Prepared;
    }
    bool StopWorkers() { workers = stop_ok; return stop_ok; }
    bool Detach(int index) {
        if (!workers) std::abort();
        if (index == fail_detach) return false;
        ++detached;
        return true;
    }
    bool IsDetached(int index) { return index != fail_verify; }
};
static void require(bool value) { if (!value) std::abort(); }
int main() {
    for (int phase = 0; phase < 6; ++phase) {
        require(!CanIntercept(false, static_cast<Phase>(phase)));
        require(CanIntercept(true, static_cast<Phase>(phase)) == (phase < 2));
    }
    for (int scenario = 0; scenario < 9; ++scenario) {
        std::atomic<Phase> phase{scenario == 1 ? Phase::Initializing :
                                 scenario == 6 ? Phase::Detached :
                                 scenario == 8 ? Phase::Yielded : Phase::Ready};
        Backend backend;
        backend.active = scenario == 2;
        backend.game_check_ok = scenario != 7;
        backend.stop_ok = scenario != 3;
        backend.fail_detach = scenario == 4 ? 1 : -1;
        backend.fail_verify = scenario == 5 ? 0 : -1;
        const auto result = PrepareStop(phase, backend);
        if (scenario == 0) {
            require(result == StopResult::Prepared && phase == Phase::Detached && backend.detached == 2 && !backend.enabled);
        } else if (scenario == 2) {
            require(result == StopResult::GameActive && phase == Phase::Ready && backend.enabled && !backend.workers);
        } else if (scenario == 7) {
            require(result == StopResult::RebootRequired && phase == Phase::Ready && backend.enabled && !backend.workers);
        } else if (scenario == 6 || scenario == 8) {
            // Nothing is intercepted, so the module can be replaced at once
            require(result == StopResult::Prepared && !backend.workers && backend.detached == 0);
        } else {
            require(result == StopResult::RebootRequired);
            require(scenario == 1 ? phase == Phase::Initializing && backend.enabled : phase == Phase::RebootRequired);
            if (scenario == 3) require(backend.detached == 0);
        }
        std::printf("PASS runtime stop scenario %d\n", scenario);
    }
}
