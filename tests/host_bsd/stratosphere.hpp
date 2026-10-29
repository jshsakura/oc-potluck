#pragma once
// Host stand-in for the parts of stratosphere the BSD proxy sockets use, so
// proxy_socket.cpp and proxy_socket_manager.cpp build as native test code.
#include "../host_switch/stratosphere.hpp"

#include <condition_variable>
#include <mutex>

using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

#define R_SUCCEED() return ::Result(0)
#define R_THROW(code) return ::Result(code)
#define R_RETURN(expr) return (expr)
#define R_SUCCEEDED(r) ((r) == 0)
#define AMS_UNUSED(...) (void)sizeof(__VA_ARGS__)

namespace ams::os {

enum EventClearMode {
    EventClearMode_ManualClear,
    EventClearMode_AutoClear,
};

class Event {
public:
    explicit Event(EventClearMode mode) : m_auto_clear(mode == EventClearMode_AutoClear) {}

    void Signal() {
        std::lock_guard lock(m_mutex);
        m_signaled = true;
        m_cv.notify_all();
    }

    void Clear() {
        std::lock_guard lock(m_mutex);
        m_signaled = false;
    }

    void Wait() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_signaled; });
        Consume();
    }

    bool TimedWait(TimeSpan timeout) {
        std::unique_lock lock(m_mutex);
        if (!m_cv.wait_for(lock, std::chrono::nanoseconds(timeout.ns), [this] { return m_signaled; })) {
            return false;
        }
        Consume();
        return true;
    }

private:
    void Consume() {
        if (m_auto_clear) {
            m_signaled = false;
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_signaled = false;
    bool m_auto_clear;
};

}  // namespace ams::os

/// Every test process is "alive"; tests that need a dead owner use pid 0 rules
inline ::Result svcGetProcessList(int32_t* out_count, uint64_t* out_pids, int32_t max_count) {
    if (max_count > 0) {
        out_pids[0] = 1;
        *out_count = 1;
    } else {
        *out_count = 0;
    }
    return 0;
}
