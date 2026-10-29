#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>
#include "../sysmodule/source/network/server_probe.hpp"
#include "../sysmodule/source/protocol/relay_protocol.hpp"
#include "../sysmodule/source/p2p/p2p_socket_io.hpp"

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::abort(); }
}
int main() {
    using namespace potluck;
    network::socket_init();
    for (int scenario = 0; scenario < 6; ++scenario) {
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
        socklen_t len = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &len);
        listen(listener, 1);
        std::thread peer([&] {
            const int fd = accept(listener, nullptr, nullptr);
            uint8_t reply[64];
            size_t size = 0;
            protocol::InitializeMessage message{};
            protocol::encode(reply, sizeof(reply), protocol::PacketId::Initialize, message, size);
            if (scenario == 1) size = sizeof(protocol::LdnHeader);
            if (scenario == 2) reply[5] ^= 0x80;
            if (scenario == 3) reply[8] = 0;
            if (scenario == 5) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } else if (scenario == 4) {
                for (size_t i = 0; i < size; ++i) {
                    send(fd, reply + i, 1, MSG_NOSIGNAL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            } else {
                send(fd, reply, size, MSG_NOSIGNAL);
            }
            shutdown(fd, SHUT_WR);
            char byte;
            while (recv(fd, &byte, 1, 0) > 0) {}
            close(fd);
        });
        network::Socket client;
        require(client.connect("127.0.0.1", ntohs(address.sin_port), 1000) == network::SocketResult::Success, "connect");
        const auto result = network::ReceiveProbeReply(client, scenario == 5 ? 50 : 1000);
        const auto expected = scenario == 0 || scenario == 4 ? network::ProbeReplyResult::Success :
            scenario == 5 ? network::ProbeReplyResult::Timeout : network::ProbeReplyResult::ProtocolError;
        client.close();
        peer.join();
        close(listener);
        require(result == expected, "complete/fragmented/truncated/version/length/deadline reply validation");
        std::printf("PASS probe scenario %d\n", scenario);
    }
    int pair[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair");
    int capacity = 4096;
    setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
    std::vector<uint8_t> payload(1024 * 1024);
    const auto start = std::chrono::steady_clock::now();
    require(!ams::mitm::p2p::SendSocketBounded(pair[0], payload.data(), payload.size(), 100), "stalled peer send must time out");
    require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1000), "send exceeded fixed deadline");
    close(pair[0]); close(pair[1]);
    std::puts("PASS stalled peer send deadline");
}
