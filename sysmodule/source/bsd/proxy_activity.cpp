/**
 * @file proxy_activity.cpp
 * @brief One wake-up for every proxy socket
 */

#include "proxy_activity.hpp"

namespace ams::mitm::bsd::activity {

namespace {

/// A leaf lock: nothing else is ever taken while it is held
os::SdkMutex g_mutex;
os::SdkConditionVariable g_changed;
u64 g_generation = 0;

}  // namespace

u64 Generation() {
    std::scoped_lock lk(g_mutex);
    return g_generation;
}

void Notify() {
    std::scoped_lock lk(g_mutex);
    ++g_generation;
    g_changed.Broadcast();
}

bool WaitChange(u64 seen, s32 ms) {
    std::scoped_lock lk(g_mutex);
    if (g_generation == seen && ms > 0) {
        // One wait: a spurious or early wake only makes the caller check again
        g_changed.TimedWait(g_mutex, TimeSpan::FromMilliSeconds(ms));
    }
    return g_generation != seen;
}

}  // namespace ams::mitm::bsd::activity
