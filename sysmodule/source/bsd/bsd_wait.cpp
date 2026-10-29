/**
 * @file bsd_wait.cpp
 * @brief select and poll (Commands 5 and 6) over real and LDN proxy sockets
 *
 * Proxy sockets live in this module, so the real service never sees them
 * become ready. Their entries are hidden from the real call (fd -1 in a
 * pollfd, a cleared bit in an fd_set) and answered from the proxy state.
 * A wait that holds proxy sockets sleeps on proxy activity, so proxy data
 * wakes it at once, and checks the real sockets beside them every
 * MixedSliceMs. A wait without proxy sockets goes to the real service in
 * slices. Either way it honours its timeout and ends when its game exits.
 */

#include "bsd_mitm_service.hpp"
#include "proxy_socket_manager.hpp"
#include "proxy_activity.hpp"
#include "bsd_types.hpp"
#include "forward_wait.hpp"
#include "poll_split.hpp"
#include "../debug/log.hpp"

#include <stratosphere/sf/sf_mitm_dispatch.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

namespace ams::mitm::bsd {

namespace {

using potluck::bsd::PollFd;
using potluck::bsd::PollEvents;
using potluck::bsd::SelectArgs;

constexpr u32 CmdSelect = 5;
constexpr u32 CmdPoll = 6;
/// How often real sockets that share a wait with proxy sockets are checked
constexpr s32 MixedSliceMs = 10;
/// pollfd entries copied on the stack (8 bytes each); a longer wait borrows a heap copy
constexpr size_t MaxPollFds = 64;
/// The longest wait split at all (8 KB of entries); beyond it the call goes through as it is
constexpr size_t MaxHeapPollFds = 1024;
/// One fd_set as libnx sends it (FD_SETSIZE 1024)
constexpr size_t MaxFdSetBytes = 128;
constexpr size_t SetCount = 3;  // read, write, error

constexpr s16 Ev(PollEvents e) { return static_cast<s16>(e); }

/// A wait too large for the stack copies is forwarded as it is; logged once
std::atomic<bool> g_oversize_logged{false};

void LogOversizeOnce(const char* what, size_t size) {
    if (!g_oversize_logged.exchange(true)) {
        LOG_WARN("BSD %s: %zu entries is more than this module tracks; some proxy sockets in it are not seen", what, size);
    }
}

/// poll() revents for a proxy socket
s16 ProxyPollEvents(const ProxySocket& proxy, s16 events) {
    s16 revents = 0;
    if ((events & Ev(PollEvents::In)) && proxy.IsReadable()) revents |= Ev(PollEvents::In);
    if ((events & Ev(PollEvents::Out)) && proxy.IsWritable()) revents |= Ev(PollEvents::Out);
    // POLLHUP and POLLERR are reported whether or not they were asked for
    if (proxy.GetState() == ProxySocketState::Closed) {
        revents |= Ev(PollEvents::Hup);
    } else if (proxy.HasError()) {
        revents |= Ev(PollEvents::Err);
    }
    return revents;
}

// -----------------------------------------------------------------------------
// poll
// -----------------------------------------------------------------------------

/// Sets revents on every proxy entry; returns how many have any
s32 ScanProxyPoll(PollFd* fds, const PollFd* in, const u16* proxy_at, size_t proxies) {
    auto& manager = ProxySocketManager::GetInstance();
    s32 ready = 0;
    for (size_t k = 0; k < proxies; k++) {
        const size_t i = proxy_at[k];
        fds[i].fd = in[i].fd;  // the real call saw -1 here
        fds[i].events = in[i].events;
        ProxySocketRef proxy = manager.GetProxySocket(in[i].fd);
        // Gone during the wait (closed, or the LDN session reset): hung up, as select reports it
        fds[i].revents = proxy != nullptr ? ProxyPollEvents(*proxy, in[i].events) : Ev(PollEvents::Hup);
        if (fds[i].revents != 0) ready++;
    }
    return ready;
}

/// One real poll of at most `ms`
forward::Reply RealPoll(::Service* service, const PollFd* in, PollFd* out, size_t n, s32 ms) {
    const struct { s32 nfds; s32 timeout; } args = { static_cast<s32>(n), ms };
    struct { s32 ret; s32 err; } reply = {};
    const Result rc = serviceMitmDispatchInOut(service, CmdPoll, args, reply,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { in, n * sizeof(PollFd) },
            { out, n * sizeof(PollFd) },
        });
    return forward::Reply{rc, reply.ret, reply.err};
}

// -----------------------------------------------------------------------------
// select
// -----------------------------------------------------------------------------

bool BitSet(const u8* bits, size_t size, s32 fd) {
    const size_t byte = static_cast<size_t>(fd) / 8;
    return bits != nullptr && byte < size && (bits[byte] & (1u << (fd % 8))) != 0;
}

void SetBit(u8* bits, size_t size, s32 fd) {
    const size_t byte = static_cast<size_t>(fd) / 8;
    if (bits != nullptr && byte < size) bits[byte] |= static_cast<u8>(1u << (fd % 8));
}

void ClearBit(u8* bits, size_t size, s32 fd) {
    const size_t byte = static_cast<size_t>(fd) / 8;
    if (bits != nullptr && byte < size) bits[byte] &= static_cast<u8>(~(1u << (fd % 8)));
}

/// The three sets of one select call: what the game asked, what the real service is asked, the answer
struct SelectSets {
    const u8* in[SetCount];
    size_t in_size[SetCount];
    u8* out[SetCount];
    size_t out_size[SetCount];
    u8 real[SetCount][MaxFdSetBytes];
    u8 proxy[MaxFdSetBytes];  // fds that are proxy sockets
    s32 nfds;
    bool any_real = false;
    bool any_proxy = false;
};

/// Splits the game's sets: proxy fds out of the real copies. False when they are too large to copy.
bool SplitSelect(SelectSets& s) {
    for (size_t k = 0; k < SetCount; k++) {
        if (s.in_size[k] > MaxFdSetBytes) return false;
        std::memset(s.real[k], 0, MaxFdSetBytes);
        if (s.in[k] != nullptr) std::memcpy(s.real[k], s.in[k], s.in_size[k]);
    }
    std::memset(s.proxy, 0, sizeof(s.proxy));
    auto& manager = ProxySocketManager::GetInstance();
    const s32 limit = std::min<s32>(s.nfds, static_cast<s32>(MaxFdSetBytes * 8));
    for (s32 fd = 0; fd < limit; fd++) {
        bool wanted = false;
        for (size_t k = 0; k < SetCount; k++) wanted |= BitSet(s.in[k], s.in_size[k], fd);
        if (!wanted) continue;
        if (manager.GetProxySocket(fd) == nullptr) {
            s.any_real = true;
            continue;
        }
        s.any_proxy = true;
        SetBit(s.proxy, sizeof(s.proxy), fd);
        for (size_t k = 0; k < SetCount; k++) ClearBit(s.real[k], s.in_size[k], fd);
    }
    return true;
}

/// Sets the answer bits of every proxy fd; returns how many bits it set
s32 ScanProxySelect(SelectSets& s) {
    auto& manager = ProxySocketManager::GetInstance();
    s32 ready = 0;
    const s32 limit = std::min<s32>(s.nfds, static_cast<s32>(MaxFdSetBytes * 8));
    for (s32 fd = 0; fd < limit; fd++) {
        if (!BitSet(s.proxy, sizeof(s.proxy), fd)) continue;
        ProxySocketRef proxy = manager.GetProxySocket(fd);
        // Closed and freed during the wait: report it so the game's next call sees the error
        const bool gone = proxy == nullptr;
        const bool hit[SetCount] = {
            gone || proxy->IsReadable(),
            !gone && proxy->IsWritable(),
            gone || proxy->HasError(),
        };
        for (size_t k = 0; k < SetCount; k++) {
            if (hit[k] && BitSet(s.in[k], s.in_size[k], fd)) {
                SetBit(s.out[k], s.out_size[k], fd);
                ready++;
            }
        }
    }
    return ready;
}

/// One real select of at most `ms` on the real copies (or the game's own sets when `as_is`)
forward::Reply RealSelect(::Service* service, SelectSets& s, s32 ms, bool as_is) {
    const SelectArgs args = { s.nfds, 0, ms / 1000, static_cast<s64>(ms % 1000) * 1000, 0, {} };
    struct { s32 ret; s32 err; } reply = {};
    const auto in = [&](size_t k) -> const void* { return as_is ? s.in[k] : (s.in[k] != nullptr ? s.real[k] : nullptr); };
    const Result rc = serviceMitmDispatchInOut(service, CmdSelect, args, reply,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out | SfBufferAttr_HipcAutoSelect,
        },
        .buffers = {
            { in(0), s.in_size[0] },
            { in(1), s.in_size[1] },
            { in(2), s.in_size[2] },
            { s.out[0], s.out_size[0] },
            { s.out[1], s.out_size[1] },
            { s.out[2], s.out_size[2] },
        });
    return forward::Reply{rc, reply.ret, reply.err};
}

/// select's timeout in ms, -1 for none
s32 SelectTimeoutMs(const SelectArgs& args) {
    constexpr s64 MaxMs = 0x7FFFFFFF;
    if (args.is_null) return -1;
    if (args.sec < 0 || args.usec < 0) return 0;
    if (args.sec >= MaxMs / 1000) return static_cast<s32>(MaxMs);  // before sec * 1000 can overflow
    const s64 usec = args.usec < 1000000 ? args.usec : 999999;
    const s64 ms = args.sec * 1000 + (usec + 999) / 1000;
    return ms > MaxMs ? static_cast<s32>(MaxMs) : static_cast<s32>(ms);
}

}  // namespace

/**
 * @brief Wait for socket activity - select (Command 5)
 *
 * Input: SelectArgs (nfds and the timeout inline), the read, write and
 * error sets as three in buffers. Output: {ret, errno}, the three sets.
 */
Result BsdMitmService::Select(
    sf::Out<s32> out_ret, sf::Out<s32> out_errno,
    const potluck::bsd::SelectArgs& args,
    const sf::InAutoSelectBuffer& readfds_in,
    const sf::InAutoSelectBuffer& writefds_in,
    const sf::InAutoSelectBuffer& errorfds_in,
    sf::OutAutoSelectBuffer readfds_out,
    sf::OutAutoSelectBuffer writefds_out,
    sf::OutAutoSelectBuffer errorfds_out)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Select: cmd_count=%u, nfds=%d", m_session_id, m_command_count, args.nfds);

    SelectSets s;
    const sf::InAutoSelectBuffer* ins[SetCount] = { &readfds_in, &writefds_in, &errorfds_in };
    sf::OutAutoSelectBuffer* outs[SetCount] = { &readfds_out, &writefds_out, &errorfds_out };
    for (size_t k = 0; k < SetCount; k++) {
        s.in[k] = ins[k]->GetSize() > 0 ? static_cast<const u8*>(ins[k]->GetPointer()) : nullptr;
        s.in_size[k] = ins[k]->GetSize();
        s.out[k] = static_cast<u8*>(outs[k]->GetPointer());
        s.out_size[k] = outs[k]->GetSize();
    }
    s.nfds = args.nfds;
    const bool split = SplitSelect(s);
    if (!split) LogOversizeOnce("select", s.in_size[0]);
    // Nothing to watch is still a timed sleep in the real service
    const bool use_real = !split || s.any_real || !s.any_proxy;
    const bool use_proxy = split && s.any_proxy;

    ::Service* service = m_forward_service.get();
    const forward::Reply reply = forward::Sliced(m_client_pid, SelectTimeoutMs(args), [&](s32 slice) {
        const u64 seen = activity::Generation();
        for (size_t k = 0; k < SetCount; k++) {
            if (s.out[k] != nullptr) std::memset(s.out[k], 0, s.out_size[k]);
        }
        s32 ready = 0;
        if (use_real) {
            const forward::Reply real = RealSelect(service, s, use_proxy ? 0 : slice, !split);
            if (R_FAILED(real.rc) || real.ret < 0) return real;
            ready = real.ret;
        }
        if (use_proxy) {
            ready += ScanProxySelect(s);
            if (ready == 0 && slice > 0 && activity::WaitChange(seen, slice)) ready = ScanProxySelect(s);
        }
        return forward::Reply{ResultSuccess(), ready, 0};
    }, use_proxy && use_real ? MixedSliceMs : forward::SliceMs);

    out_ret.SetValue(reply.ret);
    out_errno.SetValue(reply.err);
    R_RETURN(reply.rc);
}

/**
 * @brief Wait for socket activity - poll (Command 6)
 *
 * Input: nfds, timeout in ms, the pollfd array. Output: {ret, errno}, the
 * array with revents.
 */
Result BsdMitmService::Poll(
    sf::Out<s32> out_ret, sf::Out<s32> out_errno,
    const sf::InAutoSelectBuffer& fds_in,
    sf::OutAutoSelectBuffer fds_out,
    s32 nfds, s32 timeout)
{
    m_command_count++;
    LOG_VERBOSE("[BSD#%u] Poll: cmd_count=%u, nfds=%d, timeout=%d", m_session_id, m_command_count, nfds, timeout);

    const size_t room = std::min(fds_in.GetSize(), fds_out.GetSize()) / sizeof(PollFd);
    const size_t n = nfds > 0 ? std::min(static_cast<size_t>(nfds), room) : 0;
    const auto* in = reinterpret_cast<const PollFd*>(fds_in.GetPointer());
    auto* fds = reinterpret_cast<PollFd*>(fds_out.GetPointer());
    if (n > 0) std::memcpy(fds, in, n * sizeof(PollFd));

    // The real call gets a copy with every proxy entry turned off (fd -1); the proxy
    // entries are kept by index, whatever the length of the array
    PollFd stack_real[MaxPollFds];
    std::unique_ptr<PollFd[]> heap_real;
    PollFd* real = stack_real;
    if (n > MaxPollFds) {
        if (n <= MaxHeapPollFds) heap_real.reset(new (std::nothrow) PollFd[n]);
        real = heap_real.get();
    }
    u16 proxy_at[MAX_PROXY_SOCKETS];
    potluck::bsd::PollSplit parts;
    const bool split = real != nullptr;
    if (split) {
        auto& manager = ProxySocketManager::GetInstance();
        parts = potluck::bsd::SplitPoll(in, n, real, proxy_at, MAX_PROXY_SOCKETS,
                                        [&](s32 fd) { return manager.GetProxySocket(fd) != nullptr; });
        if (parts.dropped) LogOversizeOnce("poll", n);
    } else {
        LogOversizeOnce("poll", n);
    }
    const bool any_proxy = parts.proxies > 0;
    // Nothing to watch is still a timed sleep in the real service
    const bool use_real = !split || parts.any_real || !any_proxy;
    const PollFd* real_in = split ? real : in;

    ::Service* service = m_forward_service.get();
    const forward::Reply reply = forward::Sliced(m_client_pid, timeout, [&](s32 slice) {
        const u64 seen = activity::Generation();
        s32 ready = 0;
        if (use_real) {
            const forward::Reply r = RealPoll(service, real_in, fds, n, any_proxy ? 0 : slice);
            if (R_FAILED(r.rc) || r.ret < 0) return r;
            ready = r.ret;
        }
        if (any_proxy) {
            ready += ScanProxyPoll(fds, in, proxy_at, parts.proxies);
            if (ready == 0 && slice > 0 && activity::WaitChange(seen, slice)) ready = ScanProxyPoll(fds, in, proxy_at, parts.proxies);
        }
        return forward::Reply{ResultSuccess(), ready, 0};
    }, any_proxy && use_real ? MixedSliceMs : forward::SliceMs);

    out_ret.SetValue(reply.ret);
    out_errno.SetValue(reply.err);
    R_RETURN(reply.rc);
}

}  // namespace ams::mitm::bsd
