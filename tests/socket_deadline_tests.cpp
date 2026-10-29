/**
 * @file socket_deadline_tests.cpp
 * @brief Socket::send_all keeps its 5 second deadline when the peer takes a byte at a time.
 *
 * send is wrapped (-Wl,--wrap=send): every call waits 10 ms and takes one byte,
 * so the kernel never says EAGAIN. A frame of 620 bytes would need 6.2 s; the
 * deadline must cut it at 5 s and drop the connection, because half a frame
 * cannot be followed by the next one.
 */
#include "network/socket.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static std::atomic<bool> g_trickle{false};

extern "C" ssize_t __real_send(int fd, const void* data, size_t size, int flags);
extern "C" ssize_t __wrap_send(int fd, const void* data, size_t size, int flags) {
    if (!g_trickle) return __real_send(fd, data, size, flags);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return size == 0 ? 0 : 1;
}

int main() {
    using namespace potluck::network;
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listener, 1) != 0) return 2;
    socklen_t length = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &length);
    socket_init();
    Socket client;
    if (client.connect("127.0.0.1", ntohs(addr.sin_port), 1000) != SocketResult::Success) return 3;
    const int peer = ::accept(listener, nullptr, nullptr);

    unsigned char frame[620]{};
    g_trickle = true;
    const auto start = std::chrono::steady_clock::now();
    const SocketResult result = client.send_all(frame, sizeof(frame));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    g_trickle = false;

    const bool cut = result != SocketResult::Success && !client.is_connected();
    const bool on_time = elapsed >= 4900 && elapsed <= 5600;
    std::printf("send_all with one byte per 10 ms: result=%d connected=%d elapsed_ms=%lld\n",
                static_cast<int>(result), client.is_connected() ? 1 : 0, static_cast<long long>(elapsed));
    client.close();
    ::close(peer);
    ::close(listener);
    socket_exit();
    if (!cut || !on_time) {
        std::printf("FAIL: the frame must end at the 5 s deadline with the connection dropped\n");
        return 1;
    }
    std::printf("All socket deadline tests passed!\n");
    return 0;
}
