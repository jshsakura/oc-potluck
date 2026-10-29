#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
struct Result {
    int value;
    constexpr Result(int code = 0) : value(code) {}
    constexpr operator int() const { return value; }
    constexpr int GetValue() const { return value; }
};
#define R_FAILED(r) ((r) != 0)
#define R_ABORT_UNLESS(r) do { if (R_FAILED(r)) std::abort(); } while (0)
inline uint64_t armGetSystemTick() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline uint64_t armTicksToNs(uint64_t ticks) { return ticks; }
namespace ams {
using Result = ::Result;
struct TimeSpan {
    int64_t ns;
    static TimeSpan FromMilliSeconds(int64_t ms) { return {ms * 1000000}; }
    int64_t GetNanoSeconds() const { return ns; }
};
namespace os {
constexpr size_t ThreadStackAlignment = 4096;
constexpr int LowestThreadPriority = 31;
struct Mutex {
    std::mutex native;
    constexpr explicit Mutex(bool = false) {}
    void Lock() { native.lock(); }
    void Unlock() { native.unlock(); }
    void lock() { Lock(); }
    void unlock() { Unlock(); }
};
using SdkMutex = Mutex;
struct ConditionVariable;
using SdkConditionVariable = ConditionVariable;
struct ConditionVariable {
    std::condition_variable_any cv;
    void Broadcast() { cv.notify_all(); }
    bool TimedWait(Mutex& mutex, TimeSpan duration) {
        return cv.wait_for(mutex, std::chrono::nanoseconds(duration.ns)) != std::cv_status::timeout;
    }
};
struct ThreadType {
    std::thread native;
    void (*entry)(void*) = nullptr;
    void* argument = nullptr;
};
inline std::atomic<int> live_threads{0};
inline std::atomic<int> fail_thread_create{0};
inline Result CreateThread(ThreadType* thread, void (*entry)(void*), void* arg,
                           void*, size_t, int) {
    if (fail_thread_create.exchange(0)) return 1;
    thread->entry = entry;
    thread->argument = arg;
    return 0;
}
inline void StartThread(ThreadType* thread) {
    ++live_threads;
    thread->native = std::thread([thread] {
        thread->entry(thread->argument);
        --live_threads;
    });
}
inline void WaitThread(ThreadType* thread) { thread->native.join(); }
inline void DestroyThread(ThreadType*) {}
inline void SetThreadNamePointer(ThreadType*, const char*) {}
inline void SleepThread(TimeSpan duration) { std::this_thread::sleep_for(std::chrono::nanoseconds(duration.ns)); }
}
namespace svc {
inline void SleepThread(int64_t ns) { std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }
}
}
