/**
 * @file forward_wait.cpp
 * @brief Sliced waits for forwarded socket calls
 */

#include "forward_wait.hpp"

#include <stratosphere/sf/sf_mitm_dispatch.h>

#include "bsd_types.hpp"
#include "../debug/log.hpp"

#include <atomic>

namespace ams::mitm::bsd::forward {

namespace {

/// Real bsd:u commands
constexpr u32 CmdPoll = 6;
constexpr u32 CmdGetSockOpt = 17;
constexpr u32 CmdFcntl = 20;

/// O_NONBLOCK as bsd:u reports it (libnx O_NONBLOCK_NX), plus the BSD value in case
constexpr s32 NonBlockMask = 0x800 | 0x4;
/// MSG_DONTWAIT (BSD numbering; 0x40 is MSG_WAITALL, which does block)
constexpr s32 DontWaitMask = 0x80;
constexpr s32 FGetFl = static_cast<s32>(potluck::bsd::FcntlCommand::GetFl);
constexpr s32 SolSocket = 0xFFFF;
constexpr s32 SoRcvTimeo = 0x1006;
constexpr s16 PollIn = static_cast<s16>(potluck::bsd::PollEvents::In);

/// The application pm runs; valid once g_app_known is set
constinit std::atomic<u64> g_app_pid{0};
constinit std::atomic<bool> g_app_known{false};
/// A gate that could not read the socket state is logged once
constinit std::atomic<bool> g_bypass_logged{false};

void LogBypassOnce(const char* what, s32 fd) {
    if (!g_bypass_logged.exchange(true)) {
        LOG_WARN("BSD: %s failed on fd=%d; such calls are forwarded without the exit check", what, fd);
    }
}

/// struct timeval as bsd:u returns it
struct TimeVal {
    s64 sec;
    s64 usec;
};

/// The socket's flags, or -1 when they cannot be read
s32 GetFlags(::Service* service, s32 fd) {
    const struct { s32 fd; s32 cmd; s32 arg; } in = {fd, FGetFl, 0};
    struct { s32 ret; s32 err; } out = {};
    if (R_FAILED(serviceMitmDispatchInOut(service, CmdFcntl, in, out)) || out.ret < 0) return -1;
    return out.ret;
}

/// SO_RCVTIMEO in milliseconds, -1 for none, or -2 when it cannot be read
s32 GetRcvTimeoMs(::Service* service, s32 fd) {
    const struct { s32 fd; s32 level; s32 optname; } in = {fd, SolSocket, SoRcvTimeo};
    struct { s32 ret; s32 err; u32 optlen; } out = {};
    TimeVal tv{};
    const Result rc = serviceMitmDispatchInOut(service, CmdGetSockOpt, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect },
        .buffers = { { &tv, sizeof(tv) } });
    if (R_FAILED(rc) || out.ret < 0 || out.optlen != sizeof(tv)) return -2;
    if (tv.sec <= 0 && tv.usec <= 0) return -1;
    const s64 ms = tv.sec * 1000 + tv.usec / 1000;
    return ms > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<s32>(ms < 1 ? 1 : ms);
}

/// One poll for readability on `fd`, at most `slice_ms`
Reply PollReadable(::Service* service, s32 fd, s32 slice_ms) {
    potluck::bsd::PollFd pfd = {fd, PollIn, 0};
    const struct { s32 nfds; s32 timeout; } in = {1, slice_ms};
    struct { s32 ret; s32 err; } out = {};
    const Result rc = serviceMitmDispatchInOut(service, CmdPoll, in, out,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { &pfd, sizeof(pfd) },
            { &pfd, sizeof(pfd) },
        });
    return Reply{rc, out.ret, out.err};
}

}  // namespace

void RefreshApplicationPid() {
    // pm answers this once the game has exited, even while the kernel still
    // keeps its process object for the socket service's transfer memory
    constexpr u32 ProcessNotFound = 0x20F;
    u64 app_pid = 0;
    const Result rc = ::pmdmntGetApplicationProcessId(&app_pid);
    if (R_FAILED(rc) && rc.GetValue() != ProcessNotFound) {
        g_app_known = false;  // cannot tell: waits go on as before
        return;
    }
    g_app_pid = R_SUCCEEDED(rc) ? app_pid : 0;
    g_app_known = true;
}

bool IsGameAlive(u64 pid) {
    return !g_app_known.load() || g_app_pid.load() == pid;
}

s32 WaitReadable(::Service* service, u64 pid, s32 fd, s32 msg_flags, bool honor_rcvtimeo) {
    if (msg_flags & DontWaitMask) return 0;
    // Data already there: forward at once, the common case costs one poll
    const Reply now = PollReadable(service, fd, 0);
    if (R_FAILED(now.rc) || now.ret != 0 || now.err != 0) return 0;
    // A non-blocking socket answers EAGAIN by itself; anything unreadable is forwarded as before
    const s32 flags = GetFlags(service, fd);
    if (flags < 0) {
        LogBypassOnce("F_GETFL", fd);
        return 0;
    }
    if (flags & NonBlockMask) return 0;
    s32 timeout_ms = -1;
    if (honor_rcvtimeo) {
        timeout_ms = GetRcvTimeoMs(service, fd);
        if (timeout_ms == -2) {
            LogBypassOnce("SO_RCVTIMEO", fd);
            return 0;
        }
    }
    const Reply r = Sliced(pid, timeout_ms, [&](s32 slice) { return PollReadable(service, fd, slice); });
    if (R_SUCCEEDED(r.rc) && r.ret < 0 && r.err == ErrnoGameGone) return ErrnoGameGone;
    if (R_SUCCEEDED(r.rc) && r.ret == 0 && r.err == 0) return ErrnoTimedOut;
    return 0;  // readable, or an error the real call reports itself
}

}  // namespace ams::mitm::bsd::forward
