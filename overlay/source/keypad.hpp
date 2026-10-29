#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace overlay::keypad {

/** @brief Row-major key grid; ' ' marks an empty cell. */
struct Layout {
    const char* keys;
    int cols;
    int rows;

    char At(int x, int y) const { return keys[y * cols + x]; }
};

inline constexpr Layout HexLayout{"0123456789abcdef", 4, 4};
/// Host names and IPv4 addresses: digits, lowercase letters, '-' and '.'
inline constexpr Layout HostLayout{"1234567890qwertyuiopasdfghjkl-zxcvbnm.  ", 10, 4};
inline constexpr Layout PortLayout{"123456789 0 ", 3, 4};

/** @brief Fixed-capacity text input; no heap. */
class Buffer {
public:
    static constexpr size_t Capacity = 64;

    explicit Buffer(size_t limit) : m_limit(limit < Capacity ? limit : Capacity - 1) {}

    bool Push(char ch) {
        if (m_size >= m_limit) return false;
        m_data[m_size++] = ch;
        m_data[m_size] = '\0';
        return true;
    }

    bool Pop() {
        if (m_size == 0) return false;
        m_data[--m_size] = '\0';
        return true;
    }

    void Clear() {
        m_size = 0;
        m_data[0] = '\0';
    }

    void Assign(const char* text) {
        Clear();
        for (; text != nullptr && *text != '\0' && Push(*text); ++text) {}
    }

    const char* Text() const { return m_data; }
    size_t Size() const { return m_size; }
    size_t Limit() const { return m_limit; }

private:
    char m_data[Capacity] = {};
    size_t m_size = 0;
    size_t m_limit;
};

/** @brief Grid cursor that wraps at the edges and never rests on an empty cell. */
class Cursor {
public:
    void Move(int dx, int dy, const Layout& layout) {
        if (dx != 0) {
            for (int step = 0; step < layout.cols; ++step) {
                m_x = Wrap(m_x + dx, layout.cols);
                if (layout.At(m_x, m_y) != ' ') return;
            }
            return;
        }
        m_y = Wrap(m_y + dy, layout.rows);
        SnapToKey(layout);
    }

    char Selected(const Layout& layout) const { return layout.At(m_x, m_y); }
    void Set(int x, int y) {
        m_x = x;
        m_y = y;
    }
    int X() const { return m_x; }
    int Y() const { return m_y; }

private:
    static int Wrap(int value, int size) { return (value % size + size) % size; }

    /** Pick the nearest non-empty column in the current row. */
    void SnapToKey(const Layout& layout) {
        for (int distance = 0; distance < layout.cols; ++distance) {
            if (m_x - distance >= 0 && layout.At(m_x - distance, m_y) != ' ') { m_x -= distance; return; }
            if (m_x + distance < layout.cols && layout.At(m_x + distance, m_y) != ' ') { m_x += distance; return; }
        }
    }

    int m_x = 0;
    int m_y = 0;
};

/** @brief Key grid geometry as drawn, for mapping a touch to a key. */
struct Grid {
    int left;
    int top;
    int keyWidth;
    int keyHeight;
    int gap;
};

/** @brief The key under (x, y); false for a gap, an empty cell or outside the grid. */
inline bool KeyAt(const Layout& layout, const Grid& grid, int x, int y, int* col, int* row) {
    const int stepX = grid.keyWidth + grid.gap;
    const int stepY = grid.keyHeight + grid.gap;
    if (x < grid.left || y < grid.top || stepX <= 0 || stepY <= 0) return false;
    const int c = (x - grid.left) / stepX;
    const int r = (y - grid.top) / stepY;
    if (c >= layout.cols || r >= layout.rows) return false;
    if ((x - grid.left) % stepX >= grid.keyWidth || (y - grid.top) % stepY >= grid.keyHeight) return false;
    if (layout.At(c, r) == ' ') return false;
    *col = c;
    *row = r;
    return true;
}

inline bool IsHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

/** @brief Empty clears the code; otherwise exactly 8 lowercase hex digits. */
inline bool IsValidRoomCode(const char* code) {
    const size_t length = strlen(code);
    if (length == 0) return true;
    if (length != 8) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!IsHexDigit(code[i])) return false;
    }
    return true;
}

/** @brief Digits, dots and hyphens; labels must be non-empty and not start or end with '-'. */
inline bool IsValidHost(const char* host) {
    char previous = '.';
    for (const char* ch = host; *ch != '\0'; ++ch) {
        const bool isSeparator = *ch == '.';
        if (!isSeparator && *ch != '-' && !(*ch >= '0' && *ch <= '9') &&
            !(*ch >= 'a' && *ch <= 'z') && !(*ch >= 'A' && *ch <= 'Z')) return false;
        if (isSeparator && (previous == '.' || previous == '-')) return false;
        if (*ch == '-' && previous == '.') return false;
        previous = *ch;
    }
    return host[0] != '\0' && previous != '.' && previous != '-';
}

/** @brief Accept 1..65535 written as up to five digits. */
inline bool ParsePort(const char* text, uint16_t* out) {
    const size_t length = strlen(text);
    if (length == 0 || length > 5) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + static_cast<uint32_t>(text[i] - '0');
    }
    if (value == 0 || value > 65535) return false;
    *out = static_cast<uint16_t>(value);
    return true;
}

} // namespace overlay::keypad
