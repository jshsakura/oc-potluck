/**
 * @file mode_latch.hpp
 * @brief The mode each game process plays in, fixed at its first ask
 *
 * The bsd:u and ldn:u MITMs both ask when a game opens them. The first ask
 * records the mode for that process; later asks get the recorded answer, so
 * changing the setting mid-game never pairs a local or skipped ldn:u with an
 * intercepted bsd:u or the reverse. A few slots keep an applet that asks in
 * between from evicting the game. Not thread-safe; the owner locks.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace ams::mitm::ldn {

class ModeLatch {
public:
    /// @return the mode `pid` plays in: `mode_now` on its first ask, the recorded one after
    uint32_t Latch(uint64_t pid, uint32_t mode_now) {
        if (pid == 0) return mode_now;
        for (const auto& slot : m_slots) {
            if (slot.pid == pid) return slot.mode;
        }
        m_slots[m_next] = {pid, mode_now};
        m_next = (m_next + 1) % SLOTS;
        return mode_now;
    }

    void Reset() { *this = ModeLatch{}; }

private:
    struct Slot {
        uint64_t pid = 0;
        uint32_t mode = 0;
    };
    static constexpr size_t SLOTS = 4;
    Slot m_slots[SLOTS]{};
    size_t m_next = 0;
};

} // namespace ams::mitm::ldn
