#pragma once

#include <cstddef>
#include <cstdint>

namespace potluck::ipc_threads {

/**
 * @brief IPC threads that always serve ldn:u and bsd:u together (upstream's count)
 */
inline constexpr size_t BaseThreadCount = 2;

/**
 * @brief Extra threads started only while games block on proxy sockets
 *
 * The real socket service gives a game one worker per concurrent call. When
 * blocked calls would leave no free thread for ldn:u and the game's other
 * socket calls, a spare thread starts with a stack borrowed from the heap,
 * and it exits and returns the stack once the base threads suffice again.
 * Nothing is reserved while no game needs it.
 */
inline constexpr size_t MaxSpareThreadCount = 2;

/// Called before an unbounded wait (proxy socket or forwarded call); may start a spare thread
void OnUnboundedWaitBegin(uint16_t port);

/// Called when that wait ends
void OnUnboundedWaitEnd();

/// Marks an IPC handler that may hold its thread for seconds, such as a relay connect
class ScopedLongCall {
public:
    ScopedLongCall() { OnUnboundedWaitBegin(0); }
    ~ScopedLongCall() { OnUnboundedWaitEnd(); }
    ScopedLongCall(const ScopedLongCall&) = delete;
    ScopedLongCall& operator=(const ScopedLongCall&) = delete;
};

}  // namespace potluck::ipc_threads
