/**
 * @file keypad_gui.hpp
 * @brief On-screen keypads for the channel, the server address and the port
 *
 * An overlay cannot open the system keyboard, so these are drawn here. The
 * D-Pad moves over the keys and on to the Cancel, Delete and Save buttons
 * under them; A presses, and everything can be touched too. B cancels,
 * X deletes one character and + saves.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <tesla.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "potluck_room_code.h"
#include "potluck_ipc.h"
#include "keypad.hpp"
#include "theme.hpp"
#include "ui_text.hpp"

// Defined in main.cpp
void ShowNotice(const char* text);
bool SaveConfig(PotluckService* svc);
extern u32 g_settingsGeneration;

namespace overlay {

/// The port the module uses when config.ini has none
constexpr u16 DefaultPort = 30456;

/** @brief The 8 hex digits after the room prefix, or nullptr for a public room. */
inline const char* RoomDigits(const char* passphrase) {
    if (strlen(passphrase) != POTLUCK_ROOM_CODE_LEN) return nullptr;
    if (strncmp(passphrase, POTLUCK_ROOM_PREFIX, POTLUCK_ROOM_PREFIX_LEN) != 0) return nullptr;
    return passphrase + POTLUCK_ROOM_PREFIX_LEN;
}

/** @brief Shared keypad screen; subclasses say what is typed and how it is saved. */
class KeypadGui : public tsl::Gui {
public:
    struct Spec {
        const char* title;
        const char* subtitle;
        const keypad::Layout* layout;
        size_t limit;
        const char* emptyText;  ///< Shown in the field while it is empty
        const char* extraHint;  ///< Second help line, or nullptr
    };

    explicit KeypadGui(const Spec& spec) : m_input(spec.limit), m_spec(spec) {}

    tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame(m_spec.title, m_spec.subtitle, true);
        auto list = new tsl::elm::List();
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32) {
            Draw(renderer, x, y, w);
        }), PanelHeight());
        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState& touch, HidAnalogStickState, HidAnalogStickState) override {
        const bool touching = touch.x != 0 || touch.y != 0;
        const bool pressed = touching && !m_touching;
        m_touching = touching;
        if (pressed) return OnTouch(static_cast<s32>(touch.x) - static_cast<s32>(ult::layerEdge), static_cast<s32>(touch.y));

        if (keysDown & HidNpadButton_AnyUp)    return Move(0, -1);
        if (keysDown & HidNpadButton_AnyDown)  return Move(0, 1);
        if (keysDown & HidNpadButton_AnyLeft)  return Move(-1, 0);
        if (keysDown & HidNpadButton_AnyRight) return Move(1, 0);
        if (keysDown & HidNpadButton_A) {
            if (m_button >= 0) return Press(m_button);
            return Type(m_cursor.Selected(*m_spec.layout));
        }
        if (keysDown & HidNpadButton_B) return Press(ButtonCancel);
        if (keysDown & HidNpadButton_X) return Press(ButtonDelete);
        if (keysDown & HidNpadButton_Y) return OnY();
        if (keysDown & HidNpadButton_Plus) return Save();
        return false;
    }

protected:
    /** @return nullptr when saved, otherwise the message to show. */
    virtual const char* Commit(const char* text) = 0;
    /** @brief Y does nothing unless a keypad gives it a job. */
    virtual bool OnY() { return true; }

    keypad::Buffer m_input;

    bool ClearError() {
        m_error = nullptr;
        return true;
    }

private:
    static constexpr s32 Margin = 8;
    static constexpr s32 FieldHeight = 64;
    static constexpr s32 KeyGap = 6;
    static constexpr s32 ButtonHeight = 48;
    static constexpr s32 LineHeight = 24;

    s32 KeyHeight() const { return m_spec.layout->cols > 6 ? 46 : 52; }
    s32 KeyFont() const { return m_spec.layout->cols > 6 ? 22 : 26; }

    u16 PanelHeight() const {
        return static_cast<u16>(Margin + FieldHeight + Margin * 2 + m_spec.layout->rows * (KeyHeight() + KeyGap) +
                                Margin + ButtonHeight + LineHeight * 2 + 8);
    }

    enum { ButtonCancel = 0, ButtonDelete = 1, ButtonSave = 2, ButtonCount = 3 };

    /** The D-Pad walks the keys, and below the last row (or above the first) the buttons. */
    bool Move(int dx, int dy) {
        const auto& layout = *m_spec.layout;
        if (m_button >= 0) {
            if (dx != 0) m_button = (m_button + dx + ButtonCount) % ButtonCount;
            if (dy != 0) {
                m_cursor.Set(ColumnUnder(m_button), dy < 0 ? layout.rows - 1 : 0);
                m_cursor.Move(0, 0, layout);  // land on the nearest key in that row
                m_button = -1;
            }
            return true;
        }
        const bool leavesGrid = (dy > 0 && m_cursor.Y() == layout.rows - 1) || (dy < 0 && m_cursor.Y() == 0);
        if (leavesGrid) {
            m_button = std::min(ButtonCount - 1, m_cursor.X() * ButtonCount / layout.cols);
            return true;
        }
        m_cursor.Move(dx, dy, layout);
        return true;
    }

    int ColumnUnder(int button) const {
        const int cols = m_spec.layout->cols;
        return std::min(cols - 1, (button * cols + cols / 2) / ButtonCount);
    }

    bool Press(int button) {
        switch (button) {
            case ButtonCancel:
                tsl::goBack();  // this Gui is gone after goBack; touch no members
                return true;
            case ButtonDelete:
                m_input.Pop();
                return ClearError();
            default:
                return Save();
        }
    }

    bool Type(char key) {
        if (key != ' ') m_input.Push(key);
        return ClearError();
    }

    bool Save() {
        m_error = Commit(m_input.Text());
        if (m_error == nullptr) tsl::goBack();
        return true;
    }

    bool Inside(const keypad::Grid& box, s32 x, s32 y) const {
        return x >= box.left && x < box.left + box.keyWidth && y >= box.top && y < box.top + box.keyHeight;
    }

    bool OnTouch(s32 x, s32 y) {
        int col = 0, row = 0;
        if (keypad::KeyAt(*m_spec.layout, m_grid, x, y, &col, &row)) {
            m_cursor.Set(col, row);
            m_button = -1;
            return Type(m_spec.layout->At(col, row));
        }
        for (int i = 0; i < ButtonCount; ++i) {
            if (Inside(m_buttons[i], x, y)) {
                m_button = i;
                return Press(i);
            }
        }
        return false;
    }

    void Draw(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w) {
        const auto& p = *theme::current;
        // Keys, field and buttons stand out from the panel: the separator tone is a step lighter
        const tsl::Color card = a(theme::Rgb(p.separator));
        const tsl::Color ground = a(theme::Rgb(p.background));
        const tsl::Color text = a(theme::Rgb(p.text));
        const tsl::Color muted = a(theme::Rgb(p.muted));
        const tsl::Color accent = a(theme::Rgb(p.accent));
        const s32 left = x + Margin;
        const s32 width = w - Margin * 2;
        s32 top = y + Margin;

        renderer->drawRoundedRect(left, top, width, FieldHeight, 10, card);
        const bool empty = m_input.Size() == 0;
        renderer->drawString(empty ? m_spec.emptyText : m_input.Text(), false, left + 20, top + 42, 24,
                             empty ? muted : text, width - 96);
        char counter[16];
        snprintf(counter, sizeof(counter), "%zu/%zu", m_input.Size(), m_input.Limit());
        const s32 counterWidth = renderer->getTextDimensions(counter, false, 15).first;
        renderer->drawString(counter, false, left + width - 16 - counterWidth, top + 40, 15, muted);
        top += FieldHeight + Margin * 2;

        const auto& layout = *m_spec.layout;
        const s32 keyHeight = KeyHeight();
        const s32 keyWidth = (width - KeyGap * (layout.cols - 1)) / layout.cols;
        m_grid = {left, top, keyWidth, keyHeight, KeyGap};
        for (int row = 0; row < layout.rows; ++row) {
            for (int col = 0; col < layout.cols; ++col) {
                const char key[2] = {layout.At(col, row), '\0'};
                if (key[0] == ' ') continue;
                const s32 keyX = left + col * (keyWidth + KeyGap);
                const s32 keyY = top + row * (keyHeight + KeyGap);
                const bool selected = m_button < 0 && col == m_cursor.X() && row == m_cursor.Y();
                renderer->drawRoundedRect(keyX, keyY, keyWidth, keyHeight, 8, selected ? accent : card);
                const s32 keyTextWidth = renderer->getTextDimensions(key, false, KeyFont()).first;
                renderer->drawString(key, false, keyX + (keyWidth - keyTextWidth) / 2, keyY + keyHeight / 2 + KeyFont() / 2 - 2,
                                     KeyFont(), selected ? ground : text);
            }
        }
        top += layout.rows * (keyHeight + KeyGap) + Margin;

        const s32 buttonWidth = (width - KeyGap * (ButtonCount - 1)) / ButtonCount;
        const char* labels[ButtonCount] = {Text("Cancel", "취소"), Text("Delete", "지우기"), Text("Save", "저장")};
        for (int i = 0; i < ButtonCount; ++i) {
            m_buttons[i] = {left + i * (buttonWidth + KeyGap), top, buttonWidth, ButtonHeight, 0};
            const bool save = i == ButtonSave;
            DrawButton(renderer, m_buttons[i], labels[i], save ? accent : card, save ? ground : text);
            if (i == m_button) renderer->drawBorderedRoundedRect(m_buttons[i].left, top, buttonWidth, ButtonHeight, 3, 10,
                                                                 save ? text : accent);
        }
        top += ButtonHeight + 4;

        // The buttons above name every action, so no key legend repeats them here
        if (m_spec.extraHint) renderer->drawString(m_spec.extraHint, false, left, top + 16, 15, muted, width);
        if (m_error) renderer->drawString(m_error, false, left, top + 16 + LineHeight, 15, a(theme::Rgb(p.bad)), width);
    }

    void DrawButton(tsl::gfx::Renderer* renderer, const keypad::Grid& box, const char* label,
                    tsl::Color fill, tsl::Color ink) {
        renderer->drawRoundedRect(box.left, box.top, box.keyWidth, box.keyHeight, 10, fill);
        const s32 labelWidth = renderer->getTextDimensions(label, false, 19).first;
        renderer->drawString(label, false, box.left + (box.keyWidth - labelWidth) / 2, box.top + 31, 19, ink);
    }

    static tsl::Color a(const tsl::Color& color) { return tsl::gfx::Renderer::a(color); }

    Spec m_spec;
    keypad::Cursor m_cursor;
    keypad::Grid m_grid{};
    keypad::Grid m_buttons[ButtonCount]{};
    int m_button = -1;  ///< Cursor on a button (ButtonCancel..ButtonSave), or -1 on the keys
    bool m_touching = false;
    const char* m_error = nullptr;
};

/** @brief Hex keypad for the channel; empty is the public channel. Y makes a random code. */
class RoomCodeGui : public KeypadGui {
public:
    RoomCodeGui()
        : KeypadGui({Text("Channel", "채널"), Text("Players on the same channel see each other", "같은 채널끼리만 방이 보입니다"),
                     &keypad::HexLayout, POTLUCK_ROOM_HEX_LEN, Text("Public", "공개 채널"), Text(" Random", " 무작위")}) {
        char current[64] = "";
        auto svc = potluckGetService();
        if (svc && R_SUCCEEDED(potluckGetPassphrase(svc, current))) {
            if (const char* digits = RoomDigits(current)) m_input.Assign(digits);
        }
    }

protected:
    bool OnY() override {
        static bool seeded = false;
        if (!seeded) {
            srand(static_cast<unsigned>(armGetSystemTick()));
            seeded = true;
        }
        static constexpr char Hex[] = "0123456789abcdef";
        char digits[POTLUCK_ROOM_HEX_LEN + 1];
        for (int i = 0; i < POTLUCK_ROOM_HEX_LEN; i++) digits[i] = Hex[rand() % 16];
        digits[POTLUCK_ROOM_HEX_LEN] = '\0';
        m_input.Assign(digits);
        return ClearError();
    }

    const char* Commit(const char* digits) override {
        if (!keypad::IsValidRoomCode(digits)) return Text("Enter all 8 digits, or clear for the public channel.", "8자리를 모두 넣거나 비워야 저장됩니다.");
        char full[32] = "";
        if (digits[0] != '\0') snprintf(full, sizeof(full), POTLUCK_ROOM_PREFIX "%s", digits);
        auto svc = potluckGetService();
        char previous[64] = "";
        if (!svc || R_FAILED(potluckGetPassphrase(svc, previous))) return Text("Could not save.", "저장하지 못했습니다.");
        if (R_FAILED(potluckSetPassphrase(svc, full))) return Text("Could not save.", "저장하지 못했습니다.");
        if (!SaveConfig(svc)) {
            // Undo the in-memory change so the module and config.ini keep agreeing
            if (R_FAILED(potluckSetPassphrase(svc, previous))) {
                ++g_settingsGeneration;
                return Text("Could not save.", "저장하지 못했습니다.");
            }
            return Text("Could not save.", "저장하지 못했습니다.");
        }
        ++g_settingsGeneration;
        ShowNotice(Text("Applies from the next room.", "다음 방부터 적용됩니다."));
        return nullptr;
    }
};

/** @brief One line for a connection test result. */
inline const char* ServerTestText(const PotluckServerTestResult& r) {
    static char line[96];
    switch (r.status) {
        case PotluckServerTest_Success:
            snprintf(line, sizeof(line), Text("Server OK (%u ms).", "서버 연결 성공 (%u ms)"), r.rtt_ms);
            return line;
        case PotluckServerTest_Timeout:        return Text("The server did not answer.", "서버가 응답하지 않습니다.");
        case PotluckServerTest_Refused:        return Text("No server on that port.", "그 포트에 서버가 없습니다.");
        case PotluckServerTest_Unreachable:    return Text("The network is unreachable.", "네트워크에 닿지 않습니다.");
        case PotluckServerTest_InvalidAddress: return Text("Cannot find that address.", "주소를 찾지 못했습니다.");
        case PotluckServerTest_ProtocolError:  return Text("Not a relay server.", "중계 서버가 아닙니다.");
        default:                               return Text("Could not test the server.", "서버를 시험하지 못했습니다.");
    }
}

/** @brief Saves host and port together, undoing the module's copy when config.ini cannot be written. */
inline const char* SaveServer(const char* host, u16 port) {
    auto svc = potluckGetService();
    char previousHost[64] = "";
    u16 previousPort = 0;
    if (!svc || R_FAILED(potluckGetServerAddress(svc, previousHost, &previousPort))) return Text("Could not save.", "저장하지 못했습니다.");
    if (R_FAILED(potluckSetServerAddress(svc, host, port))) return Text("The module refused this address.", "모듈이 이 주소를 받지 않았습니다.");
    if (!SaveConfig(svc)) {
        // Undo the in-memory change so the module and config.ini keep agreeing
        static_cast<void>(potluckSetServerAddress(svc, previousHost, previousPort));
        ++g_settingsGeneration;
        return Text("Could not save.", "저장하지 못했습니다.");
    }
    ++g_settingsGeneration;
    if (host[0] == '\0') {
        ShowNotice(Text("Server cleared: plays as local.", "서버를 지워 로컬로 동작합니다."));
        return nullptr;
    }
    // Check the new address at once; the overlay waits a few seconds at most
    PotluckServerTestResult result{};
    ShowNotice(R_SUCCEEDED(potluckTestServer(svc, &result)) ? ServerTestText(result)
                                                            : Text("Saved. Applies from the next game.", "다음 게임부터 적용됩니다."));
    return nullptr;
}

/** @brief Host name or IPv4 keypad; empty clears the server. */
class ServerHostGui : public KeypadGui {
public:
    ServerHostGui()
        : KeypadGui({Text("Server address", "서버 주소"), Text("IPv4 address or host name", "IPv4 주소 또는 호스트 이름"),
                     &keypad::HostLayout, keypad::Buffer::Capacity - 1, Text("None (local mode)", "없음 (로컬 모드)"), nullptr}) {
        auto svc = potluckGetService();
        if (svc && R_SUCCEEDED(potluckGetServerAddress(svc, m_host, &m_port))) m_input.Assign(m_host);
    }

protected:
    const char* Commit(const char* host) override {
        if (host[0] != '\0' && !keypad::IsValidHost(host)) return Text("Use an IPv4 address or a host name.", "IP나 호스트 이름을 넣어야 합니다.");
        return SaveServer(host, m_port != 0 ? m_port : DefaultPort);
    }

private:
    char m_host[64] = "";
    u16 m_port = 0;
};

/** @brief Port keypad; empty means the default port. */
class ServerPortGui : public KeypadGui {
public:
    ServerPortGui()
        : KeypadGui({Text("Port", "포트"), Text("1 to 65535", "1~65535"), &keypad::PortLayout, 5,
                     Text("30456 (default)", "30456 (기본값)"), nullptr}) {
        auto svc = potluckGetService();
        if (svc && R_SUCCEEDED(potluckGetServerAddress(svc, m_host, &m_port)) && m_port != DefaultPort) {
            char digits[8];
            snprintf(digits, sizeof(digits), "%u", m_port);
            m_input.Assign(digits);
        }
    }

protected:
    const char* Commit(const char* text) override {
        u16 port = DefaultPort;
        if (text[0] != '\0' && !keypad::ParsePort(text, &port)) return Text("Use a number from 1 to 65535.", "1에서 65535 사이의 숫자여야 합니다.");
        return SaveServer(m_host, port);
    }

private:
    char m_host[64] = "";
    u16 m_port = 0;
};

} // namespace overlay
