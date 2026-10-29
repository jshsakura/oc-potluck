#pragma once
// Touch on the installer's screen, free of SDL and libnx so the host tests can drive it.
#include <algorithm>
#include <cstdlib>

namespace touch {

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

inline bool inside(const Box& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

/** The part of a menu row between top and bottom that a tap may hit; false when less than half of it shows. */
inline bool visiblePart(const Box& row, int top, int bottom, Box& out) {
    const int y0 = std::max(row.y, top), y1 = std::min(row.y + row.h, bottom);
    if ((y1 - y0) * 2 < row.h) return false;
    out = {row.x, y0, row.w, y1 - y0};
    return true;
}

/**
 * One finger on the screen. The page does not move until the finger has
 * travelled Slop pixels, so a tap lands on the row that was drawn under it;
 * past that the finger drags the page. Lifting it before then is a tap.
 */
class Finger {
public:
    static constexpr int Slop = 16;
    struct Step {
        int scroll = 0;       // pixels to scroll the page by, down positive
        bool tapped = false;  // the finger came up without dragging
        int x = -1, y = -1;   // where it came up
    };

    /** The finger is on the screen at x, y this frame. */
    Step down(int x, int y) {
        Step s;
        if (!m_active) {
            m_active = true;
            m_dragging = false;
            m_travel = 0;
            m_startY = y;
        } else {
            m_travel += std::abs(x - m_x) + std::abs(y - m_y);
            if (m_dragging) {
                s.scroll = m_y - y;  // drag up to read further down
            } else if (m_travel >= Slop) {
                m_dragging = true;
                s.scroll = m_startY - y;
            }
        }
        m_x = x;
        m_y = y;
        return s;
    }

    /** No finger on the screen this frame. */
    Step up() {
        Step s;
        if (!m_active) return s;
        s.tapped = !m_dragging;
        s.x = m_x;
        s.y = m_y;
        m_active = false;
        return s;
    }

private:
    bool m_active = false, m_dragging = false;
    int m_x = 0, m_y = 0, m_startY = 0, m_travel = 0;
};

} // namespace touch
