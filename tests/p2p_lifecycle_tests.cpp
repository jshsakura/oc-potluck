#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h>
#include <condition_variable>
#include <vector>
#include "../sysmodule/source/p2p/p2p_proxy_server.hpp"
#include "../sysmodule/source/p2p/p2p_proxy_client.hpp"

using namespace ams::mitm::p2p;
static uint16_t server_port;
static bool upnp_available = false;
static bool upnp_mapping = false;
static std::atomic<int> packets[256]{};
static std::mutex packet_mutex;
static std::condition_variable packet_cv;
static std::atomic<bool> corrupt_payload{false};
static void packet_callback(potluck::protocol::PacketId type, const void* data, size_t size) {
    if (type == potluck::protocol::PacketId::ProxyData) {
        potluck::protocol::ProxyDataHeader header{};
        if (size < sizeof(header)) { corrupt_payload = true; return; }
        std::memcpy(&header, data, sizeof(header));
        if (size != sizeof(header) + header.data_length) corrupt_payload = true;
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = sizeof(header); i < size; ++i) {
            if (bytes[i] != 0x5A) corrupt_payload = true;
        }
    }
    {
        std::scoped_lock lock(packet_mutex);
        ++packets[static_cast<uint8_t>(type)];
    }
    packet_cv.notify_all();
}
static void master_callback(const void*, size_t, void*) {}
namespace ams::mitm::p2p {
UpnpPortMapper::UpnpPortMapper() : m_available(false) {}
UpnpPortMapper::~UpnpPortMapper() = default;
UpnpPortMapper& UpnpPortMapper::GetInstance() { static UpnpPortMapper instance; return instance; }
bool UpnpPortMapper::Discover() { return upnp_available; }
bool UpnpPortMapper::GetExternalIPAddress(char*, size_t) { return false; }
bool UpnpPortMapper::IsAvailable() const { return false; }
bool UpnpPortMapper::AddPortMapping(uint16_t, uint16_t, const char*, int) { return upnp_mapping; }
bool UpnpPortMapper::DeletePortMapping(uint16_t) { return true; }
bool UpnpPortMapper::RefreshPortMapping(uint16_t, uint16_t, const char*) { return false; }
uint32_t UpnpPortMapper::GetLocalIPv4() const { return 0x7F000001; }
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        throw std::runtime_error(message);
    }
}
static int fd_count() {
    DIR* directory = opendir("/proc/self/fd");
    require(directory != nullptr, "cannot count file descriptors");
    int count = 0;
    while (readdir(directory)) ++count;
    closedir(directory);
    return count;
}
static void wait_packets(potluck::protocol::PacketId type, int count) {
    std::unique_lock lock(packet_mutex);
    require(packet_cv.wait_for(lock, std::chrono::seconds(3), [=] {
        return packets[static_cast<uint8_t>(type)] >= count;
    }), "routed packet did not reach production client callback");
}
static int connect_peer() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(server_port);
    require(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "peer connect failed");
    const timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return fd;
}
static uint32_t authenticate(int fd, uint8_t token_id) {
    potluck::protocol::ExternalProxyConfig config{};
    config.token[0] = token_id;
    uint8_t buffer[256];
    size_t size = 0;
    potluck::protocol::encode(buffer, sizeof(buffer), potluck::protocol::PacketId::ExternalProxy, config, size);
    require(send(fd, buffer, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size), "auth send failed");
    pollfd pfd{fd, POLLIN, 0};
    require(poll(&pfd, 1, 2000) > 0, "auth reply timeout");
    const auto received = recv(fd, buffer, 20, MSG_WAITALL);
    require(received >= 20, "auth reply truncated");
    potluck::protocol::ProxyConfig reply{};
    std::memcpy(&reply, buffer + sizeof(potluck::protocol::LdnHeader), sizeof(reply));
    return reply.proxy_ip;
}
int main(int argc, char** argv) {
    try {
        const int before = fd_count();
        const int reservation = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in available{};
        available.sin_family = AF_INET;
        available.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(reservation, reinterpret_cast<sockaddr*>(&available), sizeof(available)) == 0, "reserve port");
        socklen_t available_size = sizeof(available);
        getsockname(reservation, reinterpret_cast<sockaddr*>(&available), &available_size);
        server_port = ntohs(available.sin_port);
        close(reservation);
        P2pProxyServer server(master_callback, nullptr);
        require(server.Start(server_port), "server start failed");
        const char* scenario = argc > 1 ? argv[1] : "tokens";
        if (!std::strcmp(scenario, "tokens")) {
            potluck::protocol::ExternalProxyToken a{}, b{};
            a.token[0] = 1; a.virtual_ip = 0x0A720002;
            b.token[0] = 2; b.virtual_ip = 0x0A720003;
            server.AddWaitingToken(a); server.AddWaitingToken(b);
            int fd = connect_peer();
            require(authenticate(fd, 1) == a.virtual_ip, "A received another pending peer's IP");
            int second = connect_peer();
            require(authenticate(second, 2) == b.virtual_ip, "B received incorrect IP");
            close(fd);
            close(second);
        } else if (!std::strcmp(scenario, "disconnect")) {
            int fd = connect_peer();
            close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        } else if (!std::strcmp(scenario, "stop")) {
            int fd = connect_peer();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            server.Stop();
            close(fd);
        } else if (!std::strcmp(scenario, "silent")) {
            int peers[8];
            for (int& fd : peers) fd = connect_peer();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int rejected = connect_peer();
            pollfd pfd{rejected, POLLIN, 0};
            require(poll(&pfd, 1, 2000) > 0, "ninth unauthenticated connection was not rejected");
            char byte;
            require(recv(rejected, &byte, 1, 0) <= 0, "ninth connection survived admission limit");
            close(rejected);
            for (int fd : peers) {
                pollfd pending{fd, POLLIN, 0};
                require(poll(&pending, 1, 6500) > 0, "silent peer has no authentication deadline");
                require(recv(fd, &byte, 1, 0) <= 0, "silent connection remained open");
                close(fd);
            }
        } else if (!std::strcmp(scenario, "create-failure")) {
            ams::os::fail_thread_create = 1;
            int fd = connect_peer();
            pollfd pending{fd, POLLIN, 0};
            require(poll(&pending, 1, 2000) > 0, "thread creation failure leaked connection");
            char byte;
            require(recv(fd, &byte, 1, 0) <= 0, "failed session was not closed");
            close(fd);
        } else if (!std::strcmp(scenario, "client")) {
            potluck::protocol::ExternalProxyToken token{};
            token.token[0] = 3; token.virtual_ip = 0x0A720004;
            server.AddWaitingToken(token);
            P2pProxyClient client;
            for (int i = 0; i < 3; ++i) {
                if (i) server.AddWaitingToken(token);
                require(client.Connect("127.0.0.1", server_port), "production client connect failed");
                potluck::protocol::ExternalProxyConfig config{};
                config.token[0] = 3;
                require(client.PerformAuth(config), "production client auth failed");
                require(client.EnsureProxyReady(2000), "production client ready failed");
                client.Disconnect();
            }
        } else if (!std::strcmp(scenario, "client-invalid")) {
            P2pProxyClient client;
            potluck::protocol::ExternalProxyConfig config{};
            potluck::protocol::ProxyDataHeader header{};
            require(!client.Connect("invalid", server_port), "invalid address accepted");
            require(!client.Connect(nullptr, 0, server_port), "null address accepted");
            require(!client.EnsureProxyReady(10), "disconnected client became ready");
            require(!client.PerformAuth(config), "disconnected client authenticated");
            require(!client.Send(nullptr, 0), "disconnected raw send succeeded");
            require(!client.SendProxyData(header, nullptr, 1), "null payload accepted");
            require(!client.SendProxyData(header, nullptr, 0), "disconnected proxy send succeeded");
            require(client.Connect("127.0.0.1", server_port), "connect for auth timeout");
            require(client.Connect("127.0.0.1", server_port), "duplicate connect lost connection");
            require(!client.EnsureProxyReady(25), "unauthenticated client became ready");
            client.Disconnect();
        } else if (!std::strcmp(scenario, "routing")) {
            using namespace potluck::protocol;
            P2pProxyClient a(packet_callback), b(packet_callback);
            ExternalProxyToken token{};
            token.address_family = 2;
            token.physical_ip[0] = 192;
            token.token[0] = 10; token.virtual_ip = 0x0A720002;
            server.AddWaitingToken(token);
            require(a.Connect("127.0.0.1", server_port), "connect A");
            ExternalProxyConfig auth{}; auth.token[0] = 10;
            require(a.PerformAuth(auth) && a.EnsureProxyReady(2000), "auth A");
            token.token[0] = 11; token.virtual_ip = 0x0A720003;
            server.AddWaitingToken(token);
            require(b.Connect("127.0.0.1", server_port), "connect B");
            auth.token[0] = 11;
            require(b.PerformAuth(auth) && b.EnsureProxyReady(2000), "auth B");
            require(a.IsConnected() && a.IsReady() && a.GetVirtualIp() != b.GetProxyConfig().proxy_ip, "peer identity");
            ProxyDataHeader data{};
            data.info.dest_ipv4 = b.GetVirtualIp();
            std::vector<uint8_t> payload(32768, 0x5A);
            data.data_length = payload.size();
            require(a.SendProxyData(data, payload.data(), payload.size()), "large unicast send");
            wait_packets(PacketId::ProxyData, 1);
            ProxyConnectRequest connect{}; connect.info.dest_ipv4 = b.GetVirtualIp();
            require(a.SendProxyConnect(connect), "proxy connect send");
            wait_packets(PacketId::ProxyConnect, 1);
            ProxyConnectResponse response{}; response.info.dest_ipv4 = a.GetVirtualIp();
            require(b.SendProxyConnectReply(response), "proxy connect reply send");
            wait_packets(PacketId::ProxyConnectReply, 1);
            ProxyDisconnectMessage disconnect{}; disconnect.info.dest_ipv4 = b.GetVirtualIp();
            require(a.SendProxyDisconnect(disconnect), "proxy disconnect send");
            wait_packets(PacketId::ProxyDisconnect, 1);
            require(!server.BroadcastFromHost(data, payload.data(), payload.size()), "unconfigured host should reject send");
            server.SetHostVirtualIp(0x0A720001);
            server.SetHostDataCallback([](uint32_t, uint16_t, uint32_t, uint16_t,
                                         ProtocolType, const uint8_t*, size_t, void*) {}, nullptr);
            require(server.BroadcastFromHost(data, payload.data(), payload.size()), "host unicast");
            wait_packets(PacketId::ProxyData, 2);
            data.info.source_ipv4 = a.GetVirtualIp();
            require(!server.BroadcastFromHost(data, payload.data(), payload.size()), "host spoof accepted");
            data.info.source_ipv4 = 0;
            data.info.dest_ipv4 = 0xC0A800FF;
            require(server.BroadcastFromHost(data, payload.data(), payload.size()), "host broadcast");
            wait_packets(PacketId::ProxyData, 4);
            data.info.source_ipv4 = 0;
            require(a.SendProxyData(data, payload.data(), 1400), "peer broadcast");
            data.data_length = 1400;
            require(a.SendProxyData(data, payload.data(), 1400), "peer broadcast with matching length");
            wait_packets(PacketId::ProxyData, 6);
            server.HandleExternalProxyStateChange(a.GetVirtualIp(), true);
            server.HandleExternalProxyStateChange(a.GetVirtualIp(), false);
            a.Disconnect(); b.Disconnect();
            require(!a.IsConnected() && !a.IsReady(), "client state after disconnect");
        } else if (!std::strcmp(scenario, "nat")) {
            require(server.IsRunning(), "running status");
            require(server.Start(server_port), "already started");
            P2pProxyServer duplicate(nullptr, nullptr);
            require(!duplicate.Start(server_port), "duplicate listener accepted occupied port");
            require(server.NatPunch() == 0, "unavailable UPnP should fail");
            upnp_available = true;
            require(server.NatPunch() == 0, "failed mapping should fail");
            upnp_mapping = true;
            require(server.NatPunch() != 0 && server.GetPublicPort() != 0, "successful mapping not retained");
            server.ReleaseNatPunch();
            require(server.GetPublicPort() == 0, "mapping not cleared");
            for (int i = 0; i < 18; ++i) {
                potluck::protocol::ExternalProxyToken token{};
                token.virtual_ip = 0x0A720002 + i;
                token.token[0] = i;
                server.AddWaitingToken(token);
            }
            server.HandleExternalProxyStateChange(0x0A720005, false);
        }
        server.Stop();
        require(!corrupt_payload, "production routing corrupted gameplay payload");
        require(ams::os::live_threads == 0, "Stop returned with running receive threads");
        require(fd_count() == before, "file descriptor leaked after Stop");
        std::printf("PASS %s\n", scenario);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
