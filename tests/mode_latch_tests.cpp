/**
 * @file mode_latch_tests.cpp
 * @brief Tests for the per-process mode latch
 */

#include "ldn/mode_latch.hpp"

#include <cstdio>

using ams::mitm::ldn::ModeLatch;

static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

constexpr uint32_t REMOTE = 0;
constexpr uint32_t LOCAL = 1;
constexpr uint32_t OFF = 2;

static void first_ask_fixes_the_mode() {
    ModeLatch latch;
    CHECK(latch.Latch(100, REMOTE) == REMOTE);
    CHECK(latch.Latch(100, LOCAL) == REMOTE);  // the setting changed mid-game
}

static void off_is_latched_like_the_others() {
    ModeLatch latch;
    CHECK(latch.Latch(100, OFF) == OFF);
    CHECK(latch.Latch(100, REMOTE) == OFF);  // bsd:u asks after ldn:u was skipped
}

static void another_process_does_not_evict_the_game() {
    ModeLatch latch;
    CHECK(latch.Latch(100, REMOTE) == REMOTE);
    CHECK(latch.Latch(200, LOCAL) == LOCAL);
    CHECK(latch.Latch(300, LOCAL) == LOCAL);
    CHECK(latch.Latch(400, LOCAL) == LOCAL);
    CHECK(latch.Latch(100, LOCAL) == REMOTE);
}

static void oldest_slot_is_reused_when_full() {
    ModeLatch latch;
    for (uint64_t pid = 1; pid <= 5; pid++) latch.Latch(pid, REMOTE);
    CHECK(latch.Latch(1, LOCAL) == LOCAL);  // pid 1 was evicted by pid 5
}

static void pid_zero_is_never_latched() {
    ModeLatch latch;
    CHECK(latch.Latch(0, LOCAL) == LOCAL);
    CHECK(latch.Latch(0, REMOTE) == REMOTE);
}

static void reset_forgets_everything() {
    ModeLatch latch;
    latch.Latch(100, REMOTE);
    latch.Reset();
    CHECK(latch.Latch(100, LOCAL) == LOCAL);
}

int main() {
    std::printf("=== Mode latch tests ===\n");
    first_ask_fixes_the_mode();
    off_is_latched_like_the_others();
    another_process_does_not_evict_the_game();
    oldest_slot_is_reused_when_full();
    pid_zero_is_never_latched();
    reset_forgets_everything();
    std::printf("%d failures\n", g_failed);
    if (g_failed == 0) std::printf("All tests passed!\n");
    return g_failed == 0 ? 0 : 1;
}
