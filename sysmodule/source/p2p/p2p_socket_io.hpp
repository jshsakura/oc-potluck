#pragma once
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <sys/socket.h>

namespace ams::mitm::p2p {
/** @brief Send a buffer within a fixed deadline, including partial writes. */
inline bool SendSocketBounded(int fd, const void* data, size_t size, int timeout_ms = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return false;
        const ssize_t count = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count > 0) { sent += static_cast<size_t>(count); continue; }
        if (count == 0) return false;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
        pollfd pfd{fd, POLLOUT, 0};
        const int result = poll(&pfd, 1, static_cast<int>(remaining));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
    }
    return true;
}
}
