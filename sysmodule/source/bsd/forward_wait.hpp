/**
 * @file forward_wait.hpp
 * @brief Keeps forwarded blocking socket calls from pinning the IPC threads
 *
 * A game's calls on real sockets are forwarded on one of our IPC threads.
 * Forwarded as they are, a blocking recv or poll stays inside the real
 * socket service until data comes, and after the game exits that never
 * happens: the thread is pinned for good, and once every thread is pinned
 * nobody answers sm's MITM queries and the whole console stops at the next
 * launch. Nothing can wake such a call from outside, because a session
 * serves one request at a time.
 *
 * So a call that would block is never sent as one. Poll is sent in slices
 * of at most SliceMs, and recv, recvfrom, read and accept first wait the
 * same way until their socket is readable. Between slices the game is
 * checked against the application pid the maintenance thread reads from pm
 * (IPC threads never call pm themselves, so they cannot wait on it while
 * sm waits on them); once the game is gone the wait ends with EINTR. A wait that
 * lasts beyond one slice also counts as unbounded, so a spare IPC thread
 * starts when the base ones are all waiting.
 */

#pragma once

#include <stratosphere.hpp>

#include "../ipc_thread_budget.hpp"

namespace ams::mitm::bsd::forward {

/// Longest single wait sent to the real socket service
constexpr s32 SliceMs = 100;
/// errno a wait ends with when its game has exited
constexpr s32 ErrnoGameGone = 4;   // EINTR
/// errno of a blocking read whose SO_RCVTIMEO passed
constexpr s32 ErrnoTimedOut = 11;  // EAGAIN

/// A bsd:u reply: every command answers {ret, errno} first
struct Reply {
    Result rc;
    s32 ret;
    s32 err;
    bool ready_elsewhere = false;  ///< Something the real service does not see is ready
};

/// False once pm no longer runs `pid` as the application; reads only the cached pid
bool IsGameAlive(u64 pid);

/// Reads the application pid from pm; called by the maintenance thread every SliceMs
void RefreshApplicationPid();

/**
 * @brief Calls `once(slice)` until it reports something, `timeout_ms` passes or the game exits
 *
 * `once` waits at most `slice` ms and returns its reply; ret 0 with errno 0
 * means nothing happened yet. A negative timeout waits until something does.
 * Time is read from the clock, so a slice that ends early never shortens
 * the timeout.
 */
template<class F>
Reply Sliced(u64 pid, s32 timeout_ms, F&& once, s32 slice_ms = SliceMs) {
    bool counted = false;
    const os::Tick start = os::GetSystemTick();
    Reply reply{};
    while (true) {
        const s64 waited = (os::GetSystemTick() - start).ToTimeSpan().GetMilliSeconds();
        const s64 left = timeout_ms < 0 ? slice_ms : (timeout_ms > waited ? timeout_ms - waited : 0);
        const s32 slice = static_cast<s32>(left < slice_ms ? left : slice_ms);
        reply = once(slice);
        if (R_FAILED(reply.rc) || reply.ret != 0 || reply.err != 0 || reply.ready_elsewhere) break;
        if (timeout_ms >= 0 && (os::GetSystemTick() - start).ToTimeSpan().GetMilliSeconds() >= timeout_ms) break;
        if (!IsGameAlive(pid)) {
            reply = Reply{ResultSuccess(), -1, ErrnoGameGone, false};
            break;
        }
        if (!counted) {
            potluck::ipc_threads::OnUnboundedWaitBegin(0);
            counted = true;
        }
    }
    if (counted) potluck::ipc_threads::OnUnboundedWaitEnd();
    return reply;
}

/**
 * @brief Before a read-type call on a real socket: waits until it would not block
 *
 * @param msg_flags      The call's flags; MSG_DONTWAIT skips the wait
 * @param honor_rcvtimeo Ends the wait at the socket's SO_RCVTIMEO (recv, read; not accept)
 * @return 0 to forward the call now, or the errno to answer with instead
 *         (ErrnoTimedOut or ErrnoGameGone)
 */
s32 WaitReadable(::Service* service, u64 pid, s32 fd, s32 msg_flags, bool honor_rcvtimeo);

}  // namespace ams::mitm::bsd::forward
