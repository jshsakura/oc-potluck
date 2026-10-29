/**
 * @file proxy_tcp_tests.cpp
 * @brief TCP over LDN: connect, accept, stream data and teardown through a loopback relay
 *
 * The send callbacks stand in for the reference client relay: each control or data
 * packet is routed straight back into the same ProxySocketManager, so both
 * ends of a connection live in one process.
 */

#include "bsd/proxy_socket.hpp"
#include "bsd/proxy_socket_manager.hpp"
#include "ipc_thread_budget.hpp"
#include "bsd/ephemeral_port_pool.hpp"
#include "bsd/proxy_activity.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace ams::mitm::bsd;
using potluck::bsd::BsdErrno;
using potluck::bsd::ProtocolType;
using potluck::bsd::SockAddrIn;
using potluck::bsd::SocketType;

namespace ams::mitm {
size_t GetHeapAllocatableSize() { return 64 * 1024 * 1024; }
}

// The game never exits in these tests; pm is not there to ask
namespace ams::mitm::bsd::forward {
bool IsGameAlive(u64) { return true; }
}

// main.cpp owns the IPC threads; here we only check the waits are paired
namespace potluck::ipc_threads {
std::atomic<int> g_open_waits{0};
void OnUnboundedWaitBegin(uint16_t) { g_open_waits++; }
void OnUnboundedWaitEnd() { g_open_waits--; }
}

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

constexpr uint32_t HostIp = 0x0A720001;    // 10.114.0.1
constexpr uint32_t GuestIp = 0x0A720002;   // 10.114.0.2
constexpr uint16_t ListenPort = 5000;
constexpr uint16_t GuestPort = 40000;
constexpr u64 Owner = 1;

potluck::protocol::ProxyInfo Info(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport) {
    potluck::protocol::ProxyInfo info{};
    info.source_ipv4 = sip;
    info.source_port = sport;
    info.dest_ipv4 = dip;
    info.dest_port = dport;
    info.protocol = potluck::protocol::ProtocolType::Tcp;
    return info;
}

ProxySendResult RelayData(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, ProtocolType protocol,
                          const void* data, size_t len, bool) {
    ProxySocketManager::GetInstance().RouteIncomingData(sip, sport, dip, dport, protocol, data, len);
    return ProxySendResult::Sent;
}

bool RelayConnect(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, ProtocolType) {
    potluck::protocol::ProxyConnectRequest request{};
    request.info = Info(sip, sport, dip, dport);
    ProxySocketManager::GetInstance().RouteConnectRequest(request);
    return true;
}

bool RelayConnectReply(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, ProtocolType) {
    potluck::protocol::ProxyConnectResponse response{};
    response.info = Info(sip, sport, dip, dport);
    ProxySocketManager::GetInstance().RouteConnectResponse(response);
    return true;
}

bool RelayDisconnect(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport, ProtocolType) {
    potluck::protocol::ProxyDisconnectMessage message{};
    message.info = Info(sip, sport, dip, dport);
    ProxySocketManager::GetInstance().RouteDisconnect(message);
    return true;
}

/// Address as the game passes it: network byte order
SockAddrIn GameAddr(uint32_t ip, uint16_t port) {
    SockAddrIn addr{};
    addr.sin_len = sizeof(addr);
    addr.sin_family = static_cast<uint8_t>(potluck::bsd::AddressFamily::Inet);
    addr.sin_port = __builtin_bswap16(port);
    addr.sin_addr = __builtin_bswap32(ip);
    return addr;
}

/// Address as the Bind handler stores an INADDR_ANY bind: wire format
SockAddrIn AnyBindAddr(uint32_t ip, uint16_t port) {
    SockAddrIn addr = GameAddr(ip, port);
    addr.sin_addr = ip;
    return addr;
}

void FreshManager() {
    auto& manager = ProxySocketManager::GetInstance();
    manager.Reset();
    manager.SetLocalIp(HostIp);
    manager.SetSendCallback(RelayData);
    manager.SetProxyConnectCallback(RelayConnect);
    manager.SetProxyConnectReplyCallback(RelayConnectReply);
    manager.SetProxyDisconnectCallback(RelayDisconnect);
}

ProxySocketRef MakeListener(s32 fd, uint16_t port = ListenPort) {
    auto socket = ProxySocketManager::GetInstance().CreateProxySocket(fd, SocketType::Stream, ProtocolType::Tcp, Owner);
    CHECK(socket != nullptr);
    CHECK(socket->Bind(AnyBindAddr(HostIp, port)) == 0);
    CHECK(socket->Listen(2) == 0);
    return socket;
}

ProxySocketRef MakeGuest(s32 fd, uint16_t port = GuestPort) {
    auto socket = ProxySocketManager::GetInstance().CreateProxySocket(fd, SocketType::Stream, ProtocolType::Tcp, Owner);
    CHECK(socket != nullptr);
    CHECK(socket->Bind(GameAddr(GuestIp, port)) == 0);
    return socket;
}

/// Accept the listener's oldest queued connection as fd
ProxySocketRef Accept(ProxySocketRef& listener, s32 fd, uint16_t expected_port = GuestPort) {
    s32 err = -1;
    CHECK(listener->WaitForConnection(&err));
    CHECK(err == 0);
    SockAddrIn peer{};
    auto accepted = ProxySocketManager::GetInstance().AdoptFromListener(*listener, fd, &peer, &err);
    CHECK(accepted != nullptr);
    CHECK(err == 0);
    CHECK(peer.GetAddr() == GuestIp);
    CHECK(peer.GetPort() == expected_port);
    return accepted;
}

/// Connect a guest to the listener and register the accepted end as fd 12
ProxySocketRef Establish(ProxySocketRef& guest, ProxySocketRef& listener) {
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == 0);
    CHECK(guest->GetState() == ProxySocketState::Connected);
    CHECK(listener->IsReadable());

    return Accept(listener, 12);
}

std::string RecvAll(ProxySocketRef& socket, size_t total, size_t chunk) {
    std::string out;
    std::vector<char> buffer(chunk);
    while (out.size() < total) {
        const s32 got = socket->Recv(buffer.data(), buffer.size(), 0x80 /* MSG_DONTWAIT */);
        if (got <= 0) {
            break;
        }
        out.append(buffer.data(), static_cast<size_t>(got));
    }
    return out;
}

void TestConnectAcceptAndExchange() {
    std::printf("connect, accept and exchange data both ways\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    CHECK(host_side != nullptr);
    if (host_side == nullptr) {
        return;
    }

    CHECK(guest->Send("hello", 5, 0) == 5);
    char buffer[16] = {};
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 5);
    CHECK(std::memcmp(buffer, "hello", 5) == 0);

    CHECK(host_side->Send("pong", 4, 0) == 4);
    std::memset(buffer, 0, sizeof(buffer));
    CHECK(guest->Recv(buffer, sizeof(buffer), 0) == 4);
    CHECK(std::memcmp(buffer, "pong", 4) == 0);

    // The listener itself never receives connection data
    CHECK(!listener->HasPendingData());
}

void TestStreamReadsKeepTheRemainder() {
    std::printf("a short read keeps the rest of the segment\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    if (host_side == nullptr) {
        return;
    }

    CHECK(guest->Send("0123456789", 10, 0) == 10);
    CHECK(RecvAll(host_side, 10, 4) == "0123456789");
}

void TestLargeWriteIsSplitAndReassembled() {
    std::printf("a write above one frame is split and arrives intact\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    if (host_side == nullptr) {
        return;
    }

    std::string payload(PROXY_SOCKET_MAX_PAYLOAD * 2 + 123, '\0');
    for (size_t i = 0; i < payload.size(); i++) {
        payload[i] = static_cast<char>('a' + i % 26);
    }
    CHECK(guest->Send(payload.data(), payload.size(), 0) == static_cast<s32>(payload.size()));
    CHECK(RecvAll(host_side, payload.size(), 1500) == payload);
}

void TestPeerCloseDrainsThenEof() {
    std::printf("peer close: unread data first, then EOF\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    if (host_side == nullptr) {
        return;
    }

    CHECK(guest->Send("bye", 3, 0) == 3);
    guest.Reset();
    CHECK(ProxySocketManager::GetInstance().CloseProxySocket(11));

    char buffer[8] = {};
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 3);
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 0);
    CHECK(host_side->IsReadable());
    CHECK(host_side->Send("x", 1, 0) == -static_cast<s32>(BsdErrno::ConnReset));
}

void TestConnectWithoutListenerIsRefused() {
    std::printf("connect to a closed port is refused, not timed out\n");
    FreshManager();
    auto guest = MakeGuest(11);
    const Result rc = guest->Connect(GameAddr(HostIp, 6000));
    CHECK(rc.GetValue() == static_cast<s32>(BsdErrno::ConnRefused));
    CHECK(guest->GetState() == ProxySocketState::Bound);
}

void TestNonBlockingConnectReportsThroughSoError() {
    std::printf("non-blocking connect: EINPROGRESS, then writable with SO_ERROR 0\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    guest->SetNonBlocking(true);
    const Result rc = guest->Connect(GameAddr(HostIp, ListenPort));
    CHECK(rc.GetValue() == static_cast<s32>(BsdErrno::InProgress));
    CHECK(guest->IsWritable());

    s32 error = -1;
    size_t len = sizeof(error);
    CHECK(guest->GetSockOpt(0xFFFF, static_cast<s32>(potluck::bsd::SocketOption::Error), &error, &len) == 0);
    CHECK(error == 0);
}

void TestBacklogFullTurnsConnectsAway() {
    std::printf("a full backlog refuses further connects\n");
    FreshManager();
    auto listener = MakeListener(10);  // backlog 2
    auto first = MakeGuest(11, 40001);
    auto second = MakeGuest(13, 40002);
    auto third = MakeGuest(14, 40003);
    CHECK(first->Connect(GameAddr(HostIp, ListenPort)) == 0);
    CHECK(second->Connect(GameAddr(HostIp, ListenPort)) == 0);
    CHECK(third->Connect(GameAddr(HostIp, ListenPort)).GetValue() == static_cast<s32>(BsdErrno::ConnRefused));
}

void TestClosingListenerEndsQueuedConnections() {
    std::printf("closing a listener ends connections it never accepted\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == 0);
    listener.Reset();
    CHECK(ProxySocketManager::GetInstance().CloseProxySocket(10));

    char buffer[4];
    CHECK(guest->Recv(buffer, sizeof(buffer), 0) == 0);
}

void TestUnknownSegmentIsDroppedNotParked() {
    std::printf("a TCP segment for no connection is dropped, not parked for UDP\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    CHECK(!manager.RouteIncomingData(GuestIp, 1234, HostIp, 7777, ProtocolType::Tcp, "zz", 2));
    auto udp = manager.CreateProxySocket(20, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(udp->Bind(AnyBindAddr(HostIp, 7777)) == 0);
    manager.DeliverPendingPackets(udp.get(), 7777, ProtocolType::Udp);
    CHECK(!udp->HasPendingData());
}

void TestUdpShortReadStillDropsTheRest() {
    std::printf("UDP keeps datagram semantics\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto udp = manager.CreateProxySocket(20, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(udp->Bind(AnyBindAddr(HostIp, 7777)) == 0);
    CHECK(manager.RouteIncomingData(GuestIp, 1234, HostIp, 7777, ProtocolType::Udp, "abcdef", 6));
    CHECK(manager.RouteIncomingData(GuestIp, 1234, HostIp, 7777, ProtocolType::Udp, "XY", 2));

    char buffer[3] = {};
    CHECK(udp->RecvFrom(buffer, sizeof(buffer), 0, nullptr) == 3);
    CHECK(std::memcmp(buffer, "abc", 3) == 0);
    CHECK(udp->RecvFrom(buffer, sizeof(buffer), 0, nullptr) == 2);
    CHECK(std::memcmp(buffer, "XY", 2) == 0);
}

void TestDataBeforeAcceptIsKept() {
    std::printf("data and close sent before accept() reach the accepted socket\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == 0);
    CHECK(guest->Send("early", 5, 0) == 5);
    guest.Reset();
    CHECK(ProxySocketManager::GetInstance().CloseProxySocket(11));

    auto host_side = Accept(listener, 12);
    if (host_side == nullptr) {
        return;
    }
    char buffer[8] = {};
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 5);
    CHECK(std::memcmp(buffer, "early", 5) == 0);
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 0);
}

void TestAcceptedCloseKeepsListenerPort() {
    std::printf("closing an accepted socket leaves the listener's port reserved\n");
    FreshManager();
    // Only the ephemeral range is tracked, so listen inside it
    constexpr uint16_t TrackedPort = 50000;
    auto& manager = ProxySocketManager::GetInstance();
    CHECK(manager.ReservePort(TrackedPort, ProtocolType::Tcp));
    auto listener = MakeListener(10, TrackedPort);
    auto guest = MakeGuest(11);
    CHECK(guest->Connect(GameAddr(HostIp, TrackedPort)) == 0);
    auto host_side = Accept(listener, 12);
    host_side.Reset();
    CHECK(manager.CloseProxySocket(12));
    CHECK(!manager.ReservePort(TrackedPort, ProtocolType::Tcp));
}

void TestBurstOfSmallWritesDoesNotReset() {
    std::printf("a burst of small writes is buffered, not reset\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    if (host_side == nullptr) {
        return;
    }
    std::string expected;
    for (int i = 0; i < 200; i++) {
        const char message[] = "msg;";
        CHECK(guest->Send(message, 4, 0) == 4);
        expected += message;
    }
    CHECK(RecvAll(host_side, expected.size(), 64) == expected);
}

bool DropConnect(uint32_t, uint16_t, uint32_t, uint16_t, ProtocolType) { return true; }

void TestUnansweredNonBlockingConnectTimesOut() {
    std::printf("an unanswered non-blocking connect ends with ETIMEDOUT\n");
    FreshManager();
    ProxySocketManager::GetInstance().SetProxyConnectCallback(DropConnect);
    auto guest = MakeGuest(11);
    guest->SetNonBlocking(true);
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)).GetValue() == static_cast<s32>(BsdErrno::InProgress));
    CHECK(!guest->IsWritable());
    std::this_thread::sleep_for(std::chrono::milliseconds(4100));
    CHECK(guest->IsWritable());
    s32 error = 0;
    size_t len = sizeof(error);
    CHECK(guest->GetSockOpt(0xFFFF, static_cast<s32>(potluck::bsd::SocketOption::Error), &error, &len) == 0);
    CHECK(error == static_cast<s32>(BsdErrno::TimedOut));
}


// ---------------------------------------------------------------------------
// Socket API edge cases and failure paths
// ---------------------------------------------------------------------------

constexpr s32 SolSocket = 0xFFFF;
s32 Opt(potluck::bsd::SocketOption option) { return static_cast<s32>(option); }
s32 Err(BsdErrno e) { return static_cast<s32>(e); }

void TestPortPool() {
    std::printf("ephemeral port pool: allocate, reserve, release, exhaust\n");
    EphemeralPortPool pool;
    const size_t total = pool.GetAvailableCount(ProtocolType::Udp);
    const uint16_t udp = pool.AllocatePort(ProtocolType::Udp);
    const uint16_t tcp = pool.AllocatePort(ProtocolType::Tcp);
    CHECK(udp >= EPHEMERAL_PORT_MIN);
    CHECK(tcp >= EPHEMERAL_PORT_MIN);
    CHECK(pool.IsPortAllocated(udp, ProtocolType::Udp));
    CHECK(!pool.IsPortAllocated(udp + 1, ProtocolType::Udp));
    CHECK(!pool.IsPortAllocated(80, ProtocolType::Udp));
    CHECK(pool.GetAvailableCount(ProtocolType::Udp) == total - 1);

    CHECK(pool.AllocateSpecificPort(60000, ProtocolType::Tcp));
    CHECK(!pool.AllocateSpecificPort(60000, ProtocolType::Tcp));
    CHECK(pool.AllocateSpecificPort(60000, ProtocolType::Udp));  // protocols are separate
    CHECK(pool.AllocateSpecificPort(80, ProtocolType::Tcp));     // untracked well-known port
    pool.ReleasePort(60000, ProtocolType::Tcp);
    CHECK(pool.AllocateSpecificPort(60000, ProtocolType::Tcp));
    pool.ReleasePort(80, ProtocolType::Tcp);                     // no-op, never tracked
    pool.ReleasePort(udp, ProtocolType::Udp);
    CHECK(!pool.IsPortAllocated(udp, ProtocolType::Udp));

    while (pool.AllocatePort(ProtocolType::Udp) != 0) {
    }
    CHECK(pool.GetAvailableCount(ProtocolType::Udp) == 0);
    pool.ReleaseAll();
    CHECK(pool.GetAvailableCount(ProtocolType::Udp) == total);
    CHECK(pool.GetAvailableCount(ProtocolType::Tcp) == total);
}

void TestStateErrors() {
    std::printf("state errors: bind twice, listen on UDP, connect when connected\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto udp = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    SockAddrIn bad_family = GameAddr(HostIp, 1);
    bad_family.sin_family = 0;
    CHECK(udp->Bind(bad_family) == Err(BsdErrno::AfNoSupport));
    CHECK(udp->Connect(bad_family) == Err(BsdErrno::AfNoSupport));
    CHECK(udp->Listen(1) == Err(BsdErrno::OpNotSupp));
    CHECK(udp->Recv(nullptr, 0, 0) == -Err(BsdErrno::NotConn));
    CHECK(udp->Send("x", 1, 0) == -Err(BsdErrno::NotConn));
    CHECK(udp->SendTo("x", 1, 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::DestAddrReq));
    CHECK(udp->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(udp->Bind(AnyBindAddr(HostIp, 7001)) == Err(BsdErrno::Inval));
    CHECK(udp->SendTo(nullptr, 4, 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::Fault));
    std::vector<char> big(PROXY_SOCKET_MAX_PAYLOAD + 1);
    CHECK(udp->SendTo(big.data(), big.size(), 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::MsgSize));
    CHECK(udp->Connect(GameAddr(GuestIp, 9)) == 0);
    CHECK(udp->Send("x", 1, 0) == 1);
    CHECK(udp->IsWritable());
    CHECK(!udp->HasError());

    auto unbound = manager.CreateProxySocket(31, SocketType::Stream, ProtocolType::Tcp, Owner);
    CHECK(unbound->Listen(1) == Err(BsdErrno::Inval));
    char byte;
    CHECK(unbound->RecvFrom(nullptr, 1, 0, nullptr) == -Err(BsdErrno::Fault));
    CHECK(unbound->RecvFrom(&byte, 1, 0, nullptr) == -Err(BsdErrno::NotConn));
    CHECK(unbound->SendTo("x", 1, 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::DestAddrReq));

    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == Err(BsdErrno::IsConn));
    CHECK(guest->Listen(1) == Err(BsdErrno::IsConn));
    CHECK(listener->Connect(GameAddr(GuestIp, 1)) == Err(BsdErrno::OpNotSupp));
    CHECK(guest->Send("", 0, 0) == 0);
    CHECK(guest->SendTo("ab", 2, 0, GameAddr(GuestIp, 1)) == 2);  // a stream ignores dest
    char buffer[4];
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 2);

    // A second connect while one is pending
    auto pending = MakeGuest(13, 40010);
    manager.SetProxyConnectCallback(DropConnect);
    pending->SetNonBlocking(true);
    CHECK(pending->Connect(GameAddr(HostIp, ListenPort)) == Err(BsdErrno::InProgress));
    CHECK(pending->Connect(GameAddr(HostIp, ListenPort)) == Err(BsdErrno::Already));
    CHECK(pending->Send("x", 1, 0) == -Err(BsdErrno::NotConn));
}

void TestAddressQueries() {
    std::printf("getsockname / getpeername\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto socket = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    SockAddrIn addr{};
    CHECK(socket->GetSockName(nullptr) == Err(BsdErrno::Fault));
    CHECK(socket->GetPeerName(nullptr) == Err(BsdErrno::Fault));
    CHECK(socket->GetSockName(&addr) == 0);
    CHECK(addr.sin_addr == 0 && addr.GetPort() == 0);
    CHECK(socket->GetPeerName(&addr) == Err(BsdErrno::NotConn));
    CHECK(socket->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(socket->GetSockName(&addr) == 0);
    CHECK(addr.GetPort() == 7000);
    CHECK(socket->Connect(GameAddr(GuestIp, 9)) == 0);
    CHECK(socket->GetPeerName(&addr) == 0);
    CHECK(addr.GetAddr() == GuestIp && addr.GetPort() == 9);
}

void TestSocketOptions() {
    std::printf("socket options\n");
    FreshManager();
    auto socket = ProxySocketManager::GetInstance().CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    s32 on = 1;
    s32 value = -1;
    size_t len = sizeof(value);
    CHECK(socket->SetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Broadcast), &on, sizeof(on)) == 0);
    CHECK(socket->IsBroadcastEnabled());
    CHECK(socket->SetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Broadcast), nullptr, 0) == Err(BsdErrno::Inval));
    CHECK(socket->SetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::ReuseAddr), &on, sizeof(on)) == 0);
    CHECK(socket->SetSockOpt(SolSocket, 0x7777, &on, sizeof(on)) == 0);
    CHECK(socket->SetSockOpt(6 /* IPPROTO_TCP */, 1, &on, sizeof(on)) == 0);
    CHECK(socket->GetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Broadcast), &value, &len) == 0);
    CHECK(value == 1);
    CHECK(socket->GetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Type), &value, &len) == 0);
    CHECK(value == static_cast<s32>(SocketType::Dgram));
    CHECK(socket->GetSockOpt(SolSocket, 0x7777, &value, &len) == Err(BsdErrno::NoProtoOpt));
    CHECK(socket->GetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Type), nullptr, &len) == Err(BsdErrno::Fault));
    size_t small = 1;
    CHECK(socket->GetSockOpt(SolSocket, Opt(potluck::bsd::SocketOption::Type), &value, &small) == Err(BsdErrno::NoProtoOpt));
    socket->SetBroadcastAddress(0x0A72FFFF);
    socket->SetBroadcastEnabled(false);
    CHECK(!socket->IsBroadcastEnabled());
}

void TestShutdownAndClose() {
    std::printf("shutdown and idempotent close\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto socket = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(socket->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(socket->Shutdown(static_cast<potluck::bsd::ShutdownHow>(9)) == Err(BsdErrno::Inval));
    CHECK(socket->Shutdown(potluck::bsd::ShutdownHow::Write) == 0);
    CHECK(socket->SendTo("x", 1, 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::Inval));
    CHECK(socket->Shutdown(potluck::bsd::ShutdownHow::Read) == 0);
    char byte;
    CHECK(socket->RecvFrom(&byte, 1, 0, nullptr) == 0);
    CHECK(socket->Shutdown(potluck::bsd::ShutdownHow::Both) == 0);
    CHECK(socket->Close() == 0);
    CHECK(socket->Close() == 0);
    CHECK(socket->HasError());
    CHECK(socket->IsReadable());
    CHECK(socket->Connect(GameAddr(GuestIp, 9)) == Err(BsdErrno::BadF));
}

void TestUdpReceivePaths() {
    std::printf("UDP receive: peek, source address, pending size, broadcast fan-out\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto a = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    auto b = manager.CreateProxySocket(31, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(a->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(b->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(manager.GetActiveSocketCount() == 2);

    // Broadcast reaches both listeners on the port
    CHECK(manager.RouteIncomingData(GuestIp, 1234, 0x0A72FFFF, 7000, ProtocolType::Udp, "hey", 3));
    CHECK(a->HasPendingData() && b->HasPendingData());
    CHECK(a->GetPendingDataSize() == 3);

    char buffer[8] = {};
    SockAddrIn from{};
    CHECK(a->RecvFrom(buffer, sizeof(buffer), 0x2 /* MSG_PEEK */, &from) == 3);
    CHECK(a->HasPendingData());
    CHECK(from.GetAddr() == GuestIp && from.GetPort() == 1234);
    CHECK(a->RecvFrom(buffer, sizeof(buffer), 0x80, &from) == 3);
    CHECK(a->RecvFrom(buffer, sizeof(buffer), 0x80, &from) == -Err(BsdErrno::Again));
    a->SetNonBlocking(true);
    CHECK(a->IsNonBlocking());
    CHECK(a->RecvFrom(buffer, sizeof(buffer), 0, &from) == -Err(BsdErrno::Again));

    // A full ring drops the oldest datagram
    for (int i = 0; i < static_cast<int>(PROXY_SOCKET_MAX_QUEUE_SIZE) + 3; i++) {
        const char tag = static_cast<char>('A' + i % 26);
        CHECK(manager.RouteIncomingData(GuestIp, 1234, HostIp, 7000, ProtocolType::Udp, &tag, 1));
    }
    CHECK(b->RecvFrom(buffer, sizeof(buffer), 0x80, nullptr) == 1);
    CHECK(buffer[0] == 'C' || buffer[0] == 'D' || buffer[0] == 'E');
}

void TestPendingUdpPackets() {
    std::printf("UDP packets before bind are delivered once a socket binds\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    for (int i = 0; i < 20; i++) {  // more than the 16-slot ring: oldest dropped
        const char tag = static_cast<char>('a' + i);
        CHECK(!manager.RouteIncomingData(GuestIp, 1234, HostIp, 8000 + (i % 2), ProtocolType::Udp, &tag, 1));
    }
    auto socket = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(socket->Bind(AnyBindAddr(HostIp, 8000)) == 0);
    manager.DeliverPendingPackets(nullptr, 8000, ProtocolType::Udp);
    manager.DeliverPendingPackets(socket.get(), 8000, ProtocolType::Udp);
    CHECK(socket->HasPendingData());
    size_t delivered = 0;
    char byte;
    while (socket->RecvFrom(&byte, 1, 0x80, nullptr) == 1) {
        delivered++;
    }
    CHECK(delivered > 0 && delivered < 10);
}

void TestPendingKeepsWholeDatagramsForTheirAddress() {
    std::printf("UDP before bind: a datagram too big to park is dropped whole, other addresses stay parked\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    std::vector<char> fits(1400, 'f'), big(1401, 'b');
    CHECK(!manager.RouteIncomingData(GuestIp, 1234, HostIp, 8100, ProtocolType::Udp, big.data(), big.size()));
    CHECK(!manager.RouteIncomingData(GuestIp, 1234, HostIp, 8100, ProtocolType::Udp, fits.data(), fits.size()));
    CHECK(!manager.RouteIncomingData(GuestIp, 1234, 0x0A720099, 8100, ProtocolType::Udp, "z", 1));
    auto socket = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(socket->Bind(AnyBindAddr(HostIp, 8100)) == 0);  // bound to 10.114.0.1 itself
    manager.DeliverPendingPackets(socket.get(), 8100, ProtocolType::Udp);
    std::vector<char> got(2000);
    CHECK(socket->RecvFrom(got.data(), got.size(), 0x80, nullptr) == 1400);
    CHECK(got[0] == 'f' && got[1399] == 'f');
    CHECK(socket->RecvFrom(got.data(), got.size(), 0x80, nullptr) < 0);  // neither the cut one nor 10.114.0.153's
}

void TestActivityWakesWaiters() {
    std::printf("proxy activity: a waiter wakes on the next change, not on an old one\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    const u64 before = activity::Generation();
    CHECK(!activity::WaitChange(before, 0));
    auto socket = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(socket->Bind(AnyBindAddr(HostIp, 8200)) == 0);
    CHECK(manager.RouteIncomingData(GuestIp, 1234, HostIp, 8200, ProtocolType::Udp, "x", 1));
    CHECK(activity::WaitChange(before, 10));  // the data arrived after `before` was read

    const u64 seen = activity::Generation();
    const auto start = std::chrono::steady_clock::now();
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        manager.RouteIncomingData(GuestIp, 1234, HostIp, 8200, ProtocolType::Udp, "y", 1);
    });
    const bool woke = activity::WaitChange(seen, 2000);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    sender.join();
    CHECK(woke && waited < 1000);
}

void TestManagerRegistry() {
    std::printf("manager registry: reuse, limits, misses, close all\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto first = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    auto again = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(first.get() == again.get());
    CHECK(manager.IsProxySocket(30));
    CHECK(!manager.IsProxySocket(99));
    CHECK(manager.GetProxySocket(99) == nullptr);
    CHECK(!manager.CloseProxySocket(99));
    first.Reset();
    again.Reset();
    CHECK(manager.CloseProxySocket(30));

    std::vector<ProxySocketRef> held;
    for (s32 fd = 100; fd < 100 + static_cast<s32>(MAX_PROXY_SOCKETS) + 1; fd++) {
        auto socket = manager.CreateProxySocket(fd, SocketType::Dgram, ProtocolType::Udp, Owner);
        if (socket == nullptr) {
            break;
        }
        held.push_back(std::move(socket));
    }
    CHECK(held.size() == MAX_PROXY_SOCKETS);
    held.clear();
    manager.CloseAllProxySockets();
    CHECK(manager.GetActiveSocketCount() == 0);

    const uint16_t port = manager.AllocatePort(ProtocolType::Udp);
    CHECK(port != 0);
    CHECK(manager.GetAvailablePortCount(ProtocolType::Udp) + 1 == EPHEMERAL_PORT_COUNT);
    manager.ReleasePort(port, ProtocolType::Udp);
    CHECK(manager.GetLocalIp() == HostIp);
}

void TestNoRelaySession() {
    std::printf("without a relay session sends fail cleanly\n");
    auto& manager = ProxySocketManager::GetInstance();
    manager.Reset();  // clears every callback
    auto udp = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(udp->Bind(AnyBindAddr(HostIp, 7000)) == 0);
    CHECK(udp->SendTo("x", 1, 0, GameAddr(GuestIp, 9)) == -Err(BsdErrno::NetUnreach));
    auto tcp = MakeGuest(31);
    CHECK(tcp->Connect(GameAddr(HostIp, ListenPort)) == Err(BsdErrno::NetUnreach));
    CHECK(!manager.SendProxyConnectReply(HostIp, 1, GuestIp, 2, ProtocolType::Tcp));
    CHECK(!manager.SendProxyDisconnect(HostIp, 1, GuestIp, 2, ProtocolType::Tcp));
}

ProxySendResult FailData(uint32_t, uint16_t, uint32_t, uint16_t, ProtocolType, const void*, size_t, bool) {
    return ProxySendResult::Failed;
}

void TestRelaySendFailure() {
    std::printf("a failed relay send reports what went out\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    ProxySocketManager::GetInstance().SetSendCallback(FailData);
    CHECK(guest->Send("x", 1, 0) == -Err(BsdErrno::NetUnreach));
}

bool g_busy_saw_may_wait = true;
ProxySendResult BusyData(uint32_t, uint16_t, uint32_t, uint16_t, ProtocolType, const void*, size_t, bool may_wait) {
    g_busy_saw_may_wait = may_wait;
    return may_wait ? ProxySendResult::Sent : ProxySendResult::WouldBlock;
}

void TestNonblockingSendNeverWaits() {
    std::printf("a nonblocking send gets EAGAIN from a busy relay and a blocking one waits\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    ProxySocketManager::GetInstance().SetSendCallback(BusyData);

    constexpr s32 DontWait = 0x80;  // MSG_DONTWAIT on the Switch
    CHECK(guest->Send("x", 1, DontWait) == -Err(BsdErrno::Again));
    CHECK(!g_busy_saw_may_wait);
    CHECK(guest->Send("x", 1, 0) == 1);
    CHECK(g_busy_saw_may_wait);

    guest->SetNonBlocking(true);
    CHECK(guest->Send("x", 1, 0) == -Err(BsdErrno::Again));
    CHECK(!g_busy_saw_may_wait);
}

void TestStrayControlPackets() {
    std::printf("stray control packets are ignored or answered with a close\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    potluck::protocol::ProxyConnectResponse reply{};
    reply.info = Info(HostIp, ListenPort, GuestIp, 40999);
    CHECK(!manager.RouteConnectResponse(reply));
    potluck::protocol::ProxyDisconnectMessage bye{};
    bye.info = Info(HostIp, ListenPort, GuestIp, 40999);
    CHECK(!manager.RouteDisconnect(bye));

    // A listener bound to another LDN IP does not take the connection
    auto other = manager.CreateProxySocket(10, SocketType::Stream, ProtocolType::Tcp, Owner);
    CHECK(other->Bind(AnyBindAddr(0x0A720009, ListenPort)) == 0);
    CHECK(other->Listen(1) == 0);
    potluck::protocol::ProxyConnectRequest request{};
    request.info = Info(GuestIp, 40000, HostIp, ListenPort);
    CHECK(!manager.RouteConnectRequest(request));
    CHECK(!other->HasPendingConnections());

    // Late notifications for sockets in the wrong state change nothing
    auto udp = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    udp->HandlePeerDisconnect();
    udp->HandleConnectRefused();
    udp->HandleConnectResponse(reply);
    CHECK(udp->GetState() == ProxySocketState::Created);
    CHECK(!udp->IncomingConnection(request));
}

void TestAcceptErrors() {
    std::printf("accept: non-blocking, not listening, fd taken, closed listener\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto listener = MakeListener(10);
    s32 err = 0;
    listener->SetNonBlocking(true);
    CHECK(!listener->WaitForConnection(&err));
    CHECK(err == Err(BsdErrno::Again));
    listener->SetNonBlocking(false);

    auto guest = MakeGuest(11);
    CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == 0);
    SockAddrIn peer{};
    CHECK(manager.AdoptFromListener(*listener, 11, &peer, &err) == nullptr);  // fd 11 is taken
    CHECK(err == Err(BsdErrno::MFile));
    CHECK(listener->HasPendingConnections());  // still queued for the next accept

    auto udp = manager.CreateProxySocket(30, SocketType::Dgram, ProtocolType::Udp, Owner);
    CHECK(!udp->WaitForConnection(&err));
    CHECK(err == Err(BsdErrno::Inval));
    CHECK(manager.AdoptFromListener(*udp, 31, &peer, &err) == nullptr);
    CHECK(err == 0);

    listener.Reset();
    auto closing = manager.GetProxySocket(10);
    CHECK(closing->Close() == 0);
    CHECK(!closing->WaitForConnection(&err));
    CHECK(err == Err(BsdErrno::BadF));
}

void TestBlockingCallsWake() {
    std::printf("blocking recv/accept wake on data, connections and close\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto listener = MakeListener(10);

    // Blocking accept woken by a connection from another thread
    std::thread connector([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        auto guest = ProxySocketManager::GetInstance().GetProxySocket(11);
        CHECK(guest->Connect(GameAddr(HostIp, ListenPort)) == 0);
    });
    auto guest = MakeGuest(11);
    s32 err = -1;
    CHECK(listener->WaitForConnection(&err));
    connector.join();
    auto host_side = Accept(listener, 12);
    if (host_side == nullptr) {
        return;
    }

    // Blocking recv woken by data
    std::thread sender([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        auto peer = ProxySocketManager::GetInstance().GetProxySocket(11);
        CHECK(peer->Send("late", 4, 0) == 4);
    });
    char buffer[8] = {};
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == 4);
    sender.join();

    // Blocking recv woken by Close() from another thread; close waits for this ref
    std::thread closer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(ProxySocketManager::GetInstance().CloseProxySocket(12));
    });
    CHECK(host_side->Recv(buffer, sizeof(buffer), 0) == -Err(BsdErrno::BadF) ||
          host_side->GetState() == ProxySocketState::Closed);
    host_side.Reset();
    closer.join();
    CHECK(manager.GetProxySocket(12) == nullptr);
}

void TestStreamOverflowResets() {
    std::printf("a stream that overflows its queue resets instead of losing data\n");
    FreshManager();
    auto listener = MakeListener(10);
    auto guest = MakeGuest(11);
    auto host_side = Establish(guest, listener);
    if (host_side == nullptr) {
        return;
    }
    std::vector<char> frame(PROXY_SOCKET_MAX_PAYLOAD, 'z');
    for (size_t i = 0; i < PROXY_SOCKET_MAX_QUEUE_SIZE + 1; i++) {
        guest->Send(frame.data(), frame.size(), 0);
    }
    // Everything queued before the reset is still readable, then the reset
    std::vector<char> buffer(PROXY_SOCKET_MAX_PAYLOAD);
    size_t frames = 0;
    s32 got = 0;
    while ((got = host_side->Recv(buffer.data(), buffer.size(), 0x80)) > 0) {
        frames++;
    }
    CHECK(frames == PROXY_SOCKET_MAX_QUEUE_SIZE);
    CHECK(got == -Err(BsdErrno::ConnReset));
    CHECK(host_side->Recv(buffer.data(), buffer.size(), 0x80) == 0);
}

void TestLocalIpFallback() {
    std::printf("an unbound-IP TCP socket uses the session's LDN IP\n");
    FreshManager();
    auto& manager = ProxySocketManager::GetInstance();
    auto socket = manager.CreateProxySocket(30, SocketType::Stream, ProtocolType::Tcp, Owner);
    CHECK(socket->Bind(GameAddr(0, 41000)) == 0);
    CHECK(socket->LocalWireIp() == HostIp);
    CHECK(manager.HasHeapRoomForSocket());
}

}  // namespace

int main() {
    TestConnectAcceptAndExchange();
    TestPendingKeepsWholeDatagramsForTheirAddress();
    TestActivityWakesWaiters();
    TestStreamReadsKeepTheRemainder();
    TestLargeWriteIsSplitAndReassembled();
    TestPeerCloseDrainsThenEof();
    TestConnectWithoutListenerIsRefused();
    TestNonBlockingConnectReportsThroughSoError();
    TestBacklogFullTurnsConnectsAway();
    TestClosingListenerEndsQueuedConnections();
    TestUnknownSegmentIsDroppedNotParked();
    TestUdpShortReadStillDropsTheRest();
    TestDataBeforeAcceptIsKept();
    TestAcceptedCloseKeepsListenerPort();
    TestBurstOfSmallWritesDoesNotReset();
    TestUnansweredNonBlockingConnectTimesOut();
    TestPortPool();
    TestStateErrors();
    TestAddressQueries();
    TestSocketOptions();
    TestShutdownAndClose();
    TestUdpReceivePaths();
    TestPendingUdpPackets();
    TestManagerRegistry();
    TestNoRelaySession();
    TestRelaySendFailure();
    TestNonblockingSendNeverWaits();
    TestStrayControlPackets();
    TestAcceptErrors();
    TestBlockingCallsWake();
    TestStreamOverflowResets();
    TestLocalIpFallback();
    ProxySocketManager::GetInstance().Reset();
    CHECK(potluck::ipc_threads::g_open_waits.load() == 0);

    if (g_failures != 0) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All proxy TCP tests passed!\n");
    return 0;
}
