/**
 * @file poll_split_tests.cpp
 * @brief SplitPoll keeps proxy sockets in a poll() of any length.
 */
#include "bsd/poll_split.hpp"
#include <cstdio>
#include <vector>

using potluck::bsd::PollFd;
using potluck::bsd::SplitPoll;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static bool IsProxy(int fd) { return fd >= 100; }  // proxy sockets are fds 100 and up here

static std::vector<PollFd> Entries(size_t n, int fd) {
    std::vector<PollFd> v(n);
    for (auto& e : v) e = PollFd{fd, 1, 0};
    return v;
}

int main() {
    uint16_t at[64];

    // One proxy among 64 ignored entries: 65 in all, the proxy is still seen
    {
        auto in = Entries(65, -1);
        in[40] = PollFd{100, 1, 0};
        std::vector<PollFd> real(in.size());
        const auto s = SplitPoll(in.data(), in.size(), real.data(), at, 64, IsProxy);
        CHECK(s.proxies == 1 && at[0] == 40 && !s.dropped);
        CHECK(real[40].fd == -1 && real[0].fd == -1 && s.any_real);
    }
    // 64 proxies fit exactly; real entries pass through untouched
    {
        auto in = Entries(64, 100);
        in.push_back(PollFd{7, 1, 0});
        std::vector<PollFd> real(in.size());
        const auto s = SplitPoll(in.data(), in.size(), real.data(), at, 64, IsProxy);
        CHECK(s.proxies == 64 && !s.dropped && at[63] == 63);
        CHECK(real[64].fd == 7 && s.any_real);
    }
    // A 65th proxy entry has no room: it is turned off for the real call and flagged
    {
        auto in = Entries(65, 100);
        std::vector<PollFd> real(in.size());
        const auto s = SplitPoll(in.data(), in.size(), real.data(), at, 64, IsProxy);
        CHECK(s.proxies == 64 && s.dropped && !s.any_real && real[64].fd == -1);
    }
    // Only proxies: nothing for the real service
    {
        auto in = Entries(3, 101);
        std::vector<PollFd> real(in.size());
        const auto s = SplitPoll(in.data(), in.size(), real.data(), at, 64, IsProxy);
        CHECK(s.proxies == 3 && !s.any_real);
    }

    if (!failures) std::puts("All poll split tests passed!");
    return failures ? 1 : 0;
}
