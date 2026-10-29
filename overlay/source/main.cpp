/**
 * @file main.cpp
 * @brief potluck. Overlay: an in-game check of the sysmodule
 *
 * The overlay shows whether the running game is a LAN-play target, the
 * relay connection, the room size and the latency, and keeps the few
 * settings worth changing mid-game: remote or local mode, the channel and a
 * forced reconnect. Installing the module, the server address, P2P and
 * diagnostics belong to the installer.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "potluck_paths.h"
#include "potluck_room_code.h"
#include "potluck_firmware.h"
#include "potluck_ipc.h"
#include "game_list.hpp"
#include "ui_text.hpp"
#include "startup_trace.hpp"
#include "keypad.hpp"
#include "keypad_gui.hpp"
#include "theme.hpp"

using overlay::Text;

//=============================================================================
// Global State
//=============================================================================

enum class InitState {
    Uninit,   ///< Not yet initialized
    Error,    ///< The service did not answer
    Loaded,   ///< Connected to the sysmodule
    Stopped,  ///< The sysmodule is not running
};

InitState g_initState = InitState::Uninit;
char g_version[32] = "Unknown";

/// pm returns this when no process has the program id
constexpr Result ProcessNotFound = 0x20F;
/// runtime::Phase::Yielded: another module held ldn:u at boot, PotLuck is idle
constexpr u32 PhaseYielded = 5;
/// runtime::Phase::Disabled: the mode was off at boot, nothing is intercepted until a reboot
constexpr u32 PhaseDisabled = 6;
/// Status refresh period in frames (~1 s at 60 fps)
constexpr u32 RefreshFrames = 60;
/// How long a one-line result stays under the settings
constexpr u64 NoticeDurationNs = 4000000000ULL;

//=============================================================================
// Live Status
//=============================================================================

enum class GameSupport { NoGame, Supported, NotListed, Unknown };

/** @brief Snapshot drawn by the status card, refreshed once a second. */
struct LiveStatus {
    GameSupport game = GameSupport::Unknown;
    u64 game_pid = 0;
    bool has_connection = false;
    PotluckConnectionStatus connection = PotluckStatus_Disconnected;
    bool has_session = false;
    PotluckSessionInfo session{};
    u32 rtt_ms = 0;
    bool has_traffic = false;
    PotluckRelayTraffic traffic{};
    u32 mode = PotluckLdnMode_Remote;
    bool idle = false;      ///< Another LDN module holds ldn:u
    bool disabled = false;  ///< Off at boot: games bypass PotLuck until a reboot
    bool has_server = false;
};

LiveStatus g_status;

/** @brief Whether the foreground game is in gamelist.txt; reads the file only when the game changes. */
void RefreshGameSupport(LiveStatus& status) {
    u64 pid = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid)) || pid == 0) {
        status.game = GameSupport::NoGame;
        status.game_pid = 0;
        return;
    }
    if (pid == status.game_pid && status.game != GameSupport::Unknown) return;

    status.game_pid = pid;
    u64 program_id = 0;
    if (R_FAILED(pminfoGetProgramId(&program_id, pid))) {
        status.game = GameSupport::Unknown;
        return;
    }
    uint64_t ids[1] = {program_id};
    const auto page = overlay::GetInstalledSupportedGamesPage(POTLUCK_GAMELIST_PATH, ids, 1, 0);
    status.game = !page.readable ? GameSupport::Unknown
                : page.total_matched > 0 ? GameSupport::Supported : GameSupport::NotListed;
}

void RefreshStatus() {
    RefreshGameSupport(g_status);
    PotluckService* svc = g_initState == InitState::Loaded ? potluckGetService() : nullptr;
    g_status.has_connection = svc && R_SUCCEEDED(potluckGetConnectionStatus(svc, &g_status.connection));
    g_status.has_session = svc && R_SUCCEEDED(potluckGetSessionInfo(svc, &g_status.session));
    if (!svc || R_FAILED(potluckGetLastRtt(svc, &g_status.rtt_ms))) g_status.rtt_ms = 0;
    g_status.has_traffic = svc && R_SUCCEEDED(potluckGetRelayTraffic(svc, &g_status.traffic));
    if (!svc || R_FAILED(potluckGetLdnMode(svc, &g_status.mode))) g_status.mode = PotluckLdnMode_Remote;
    u32 phase = 0;
    const bool has_phase = svc && R_SUCCEEDED(potluckGetRuntimePhase(svc, &phase));
    g_status.idle = has_phase && phase == PhaseYielded;
    g_status.disabled = has_phase && phase == PhaseDisabled;
    char host[64] = "";
    u16 port = 0;
    g_status.has_server = svc && R_SUCCEEDED(potluckGetServerAddress(svc, host, &port)) && host[0] != '\0';
}

const char* GameText(GameSupport game) {
    switch (game) {
        case GameSupport::NoGame:    return Text("No game running", "실행 중인 게임 없음");
        case GameSupport::Supported: return Text("Supported", "지원 게임");
        case GameSupport::NotListed: return Text("Not on the list", "목록에 없는 게임");
        default:                     return Text("Cannot check", "확인 불가");
    }
}

/** @brief The mode games play in: remote without a server address is local. */
u32 ShownMode(const LiveStatus& s) {
    return s.mode == PotluckLdnMode_Remote && !s.has_server ? PotluckLdnMode_Local : s.mode;
}

const char* ConnectionText(const LiveStatus& s) {
    if (g_initState == InitState::Stopped) return Text("Module off", "모듈 꺼짐");
    if (s.idle) return Text("Other module", "다른 모듈 사용 중");
    if (s.disabled) return s.mode == PotluckLdnMode_Off ? Text("Off", "꺼짐") : Text("On after reboot", "재부팅하면 켜짐");
    if (s.mode == PotluckLdnMode_Off) return Text("Off after reboot", "재부팅하면 꺼짐");
    if (ShownMode(s) == PotluckLdnMode_Local) return Text("Local", "로컬 모드");
    if (!s.has_connection) return Text("No reply", "응답 없음");
    switch (s.connection) {
        // The relay is dialed when a game opens local play, not before
        case PotluckStatus_Disconnected: return Text("Waiting for a game", "게임 대기 중");
        case PotluckStatus_Connecting:   return Text("Connecting", "연결 중");
        case PotluckStatus_Connected:    return Text("Connected", "연결됨");
        case PotluckStatus_Ready:        return Text("Ready", "준비됨");
        default:                         return Text("Error", "오류");
    }
}

/** @brief Good for a live link, accent while connecting, bad for errors, muted otherwise. */
tsl::Color ConnectionColor(const LiveStatus& s) {
    const auto& p = *overlay::theme::current;
    if (s.idle) return overlay::theme::Rgb(p.bad);
    if (s.disabled || s.mode == PotluckLdnMode_Off) return overlay::theme::Rgb(p.muted);
    if (ShownMode(s) == PotluckLdnMode_Local) return overlay::theme::Rgb(p.good);
    if (!s.has_connection) return overlay::theme::Rgb(p.muted);
    switch (s.connection) {
        case PotluckStatus_Ready:
        case PotluckStatus_Connected:  return overlay::theme::Rgb(p.good);
        case PotluckStatus_Connecting: return overlay::theme::Rgb(p.accentText);
        case PotluckStatus_Error:      return overlay::theme::Rgb(p.bad);
        default:                       return overlay::theme::Rgb(p.muted);
    }
}

void FormatRoom(const LiveStatus& s, char* out, size_t size) {
    if (!s.has_session || s.session.node_count == 0) {
        snprintf(out, size, "%s", Text("No room", "방 없음"));
        return;
    }
    snprintf(out, size, Text("%u/%u · %s", "%u/%u명 · %s"), s.session.node_count, s.session.max_nodes,
             s.session.is_host ? Text("Host", "방장") : Text("Guest", "참가"));
}

void FormatLatency(const LiveStatus& s, char* out, size_t size) {
    if (s.rtt_ms == 0) snprintf(out, size, "-");
    else snprintf(out, size, "%u ms", s.rtt_ms);
}

/** @brief Game data packets and their size, "120 · 34.1 KB"; "-" before any moved. */
void FormatData(bool known, u32 packets, u64 bytes, char* out, size_t size) {
    if (!known || packets == 0) {
        snprintf(out, size, "-");
        return;
    }
    const u64 tenths = (bytes * 10 + 512) / 1024;
    if (tenths < 10240) snprintf(out, size, Text("%u · %llu.%llu KB", "%u개 · %llu.%llu KB"), packets,
                                 (unsigned long long)(tenths / 10), (unsigned long long)(tenths % 10));
    else snprintf(out, size, Text("%u · %llu MB", "%u개 · %llu MB"), packets, (unsigned long long)(tenths / 10240));
}

/** @brief The newest control packet from the relay, in words. Packet ids are the relay protocol's. */
const char* LastMessageText(const LiveStatus& s) {
    if (!s.has_traffic) return "-";
    switch (s.traffic.last_received) {
        case 0:   return Text("Greeting", "접속 확인");
        case 4: case 5: case 6: return Text("P2P setup", "P2P 설정");
        case 7:   return Text("Room update", "방 정보 갱신");
        case 9:   return Text("Kick done", "내보내기 완료");
        case 11: case 12: return Text("Room list", "방 목록");
        case 15:  return Text("Joined a room", "방 입장");
        case 16:  return Text("Room closed", "방 닫힘");
        case 17:  return Text("Address given", "주소 배정");
        case 18: case 19: case 21: return Text("TCP link", "TCP 연결");
        case 254: return Text("Keepalive", "연결 확인");
        case 255: return Text("Error", "오류");
        default:  return "-";
    }
}

/**
 * @brief The status card: game, link, room size, latency, game data each way and the newest server message.
 *
 * A CustomDrawer with a fixed height, so the list lays it out like any other
 * row; the card sits inside it with a margin, so nothing is cut at the top.
 */
constexpr s32 StatusRowHeight = 44;
constexpr s32 StatusRows = 7;
constexpr s32 StatusMargin = 12;
constexpr u16 StatusHeight = StatusRows * StatusRowHeight + StatusMargin * 2 + 8;

void DrawStatusRow(tsl::gfx::Renderer* renderer, s32 left, s32 top, s32 width, int row,
                   const char* label, const char* value, tsl::Color valueColor) {
    const auto& p = *overlay::theme::current;
    const s32 rowTop = top + row * StatusRowHeight;
    const s32 baseline = rowTop + StatusRowHeight / 2 + 7;
    renderer->drawString(label, false, left + 22, baseline, 17, tsl::gfx::Renderer::a(overlay::theme::Rgb(p.muted)));
    const s32 valueWidth = renderer->getTextDimensions(value, false, 20).first;
    renderer->drawString(value, false, left + width - 22 - valueWidth, baseline, 20, tsl::gfx::Renderer::a(valueColor));
    if (row + 1 < StatusRows) {
        // The page's own ground shows through as the rule between rows
        renderer->drawRect(left + 18, rowTop + StatusRowHeight - 1, width - 36, 1,
                           tsl::gfx::Renderer::a(overlay::theme::Rgb(p.background)));
    }
}

void AddStatusCard(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32) {
        const auto& p = *overlay::theme::current;
        const s32 left = x + 4, top = y + StatusMargin, width = w - 8;
        const s32 height = StatusRows * StatusRowHeight + 8;
        // A step lighter than the page, no accent bar
        renderer->drawRoundedRect(left, top, width, height, 14, tsl::gfx::Renderer::a(overlay::theme::Rgb(p.separator)));

        char room[48], latency[16], sent[40], received[40];
        FormatRoom(g_status, room, sizeof(room));
        FormatLatency(g_status, latency, sizeof(latency));
        FormatData(g_status.has_traffic, g_status.traffic.data_sent, g_status.traffic.data_bytes_sent, sent, sizeof(sent));
        FormatData(g_status.has_traffic, g_status.traffic.data_received, g_status.traffic.data_bytes_received, received, sizeof(received));
        const tsl::Color gameColor = g_status.game == GameSupport::Supported
                                         ? overlay::theme::Rgb(p.good) : overlay::theme::Rgb(p.text);
        const s32 rows = top + 4;
        DrawStatusRow(renderer, left, rows, width, 0, Text("Game", "게임"), GameText(g_status.game), gameColor);
        DrawStatusRow(renderer, left, rows, width, 1, Text("Link", "연결"), ConnectionText(g_status), ConnectionColor(g_status));
        DrawStatusRow(renderer, left, rows, width, 2, Text("Room", "방 인원"), room, overlay::theme::Rgb(p.text));
        DrawStatusRow(renderer, left, rows, width, 3, Text("Latency", "지연 시간"), latency, overlay::theme::Rgb(p.text));
        DrawStatusRow(renderer, left, rows, width, 4, Text("Game data sent", "보낸 게임 데이터"), sent, overlay::theme::Rgb(p.text));
        DrawStatusRow(renderer, left, rows, width, 5, Text("Game data received", "받은 게임 데이터"), received, overlay::theme::Rgb(p.text));
        DrawStatusRow(renderer, left, rows, width, 6, Text("Last from server", "서버 마지막 메시지"), LastMessageText(g_status), overlay::theme::Rgb(p.text));
    }), StatusHeight);
}

//=============================================================================
// Notices
//=============================================================================

constexpr u16 NoticeHeight = 26;

/** @brief Non-interactive explanatory line; never a focusable ListItem. */
void AddNotice(tsl::elm::List* list, const char* text) {
    list->addItem(new tsl::elm::CustomDrawer([text](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32) {
        renderer->drawString(text, false, x + 16, y + 18, 15,
                             tsl::gfx::Renderer::a(overlay::theme::Rgb(overlay::theme::current->muted)), w - 32);
    }), NoticeHeight);
}

/// Bumped when a setting is saved so the main screen rebuilds its rows
u32 g_settingsGeneration = 0;

/// Result line shown under the settings for a few seconds
const char* g_notice = nullptr;
u64 g_noticeUntil = 0;

void ShowNotice(const char* text) {
    g_notice = text;
    g_noticeUntil = armTicksToNs(armGetSystemTick()) + NoticeDurationNs;
}

void AddTimedNotice(tsl::elm::List* list) {
    list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32) {
        if (!g_notice || armTicksToNs(armGetSystemTick()) >= g_noticeUntil) return;
        renderer->drawString(g_notice, false, x + 16, y + 18, 15,
                             tsl::gfx::Renderer::a(overlay::theme::Rgb(overlay::theme::current->accentText)), w - 32);
    }), NoticeHeight);
}

/** @brief Saves through the module, which rewrites only the changed config.ini lines. */
bool SaveConfig(PotluckService* svc) {
    PotluckResult result = PotluckResult_Success;
    return R_SUCCEEDED(potluckSaveConfig(svc, &result)) && result == PotluckResult_Success;
}

//=============================================================================
// Main GUI
//=============================================================================

/** @brief Channel row: shows the digits or "Public", opens the keypad on A. */
tsl::elm::ListItem* MakeRoomCodeItem(PotluckService* svc) {
    char passphrase[64] = "";
    const bool read = R_SUCCEEDED(potluckGetPassphrase(svc, passphrase));
    const char* digits = read ? overlay::RoomDigits(passphrase) : nullptr;
    const char* value = !read ? Text("Cannot read", "확인 불가")
                      : digits ? digits
                      : passphrase[0] == '\0' ? Text("Public", "공개 채널") : Text("Invalid", "형식 오류");
    auto item = new tsl::elm::ListItem(Text("Channel", "채널"), value);
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        tsl::changeTo<overlay::RoomCodeGui>();
        return true;
    });
    return item;
}

const char* ModeText(u32 mode) {
    switch (mode) {
        case PotluckLdnMode_Local: return Text("Local", "로컬");
        case PotluckLdnMode_Off:   return Text("Off", "끔");
        default:                   return Text("Remote", "원격");
    }
}

/** @brief A game keeps the mode it started in, so a change waits for a relaunch. */
const char* RelaunchText() {
    return Text("Close the game and start it again.", "게임을 닫고 다시 실행하면 적용됩니다.");
}

/**
 * @brief What the shown mode needs, under the mode row.
 *
 * Remote without a server address plays in local mode, and local mode only
 * reaches consoles on the same Wi-Fi router; both left players guessing.
 * One line at most, and none in remote mode with a server: the relaunch
 * hint shows only for a few seconds after a change.
 */
void AddModeGuide(tsl::elm::List* list, const LiveStatus& status) {
    if (status.disabled || status.mode == PotluckLdnMode_Off) return;
    if (status.mode == PotluckLdnMode_Remote && !status.has_server) {
        AddNotice(list, Text("No server address, so it plays local.", "서버 주소가 없어 로컬로 동작합니다."));
    } else if (status.mode == PotluckLdnMode_Local) {
        AddNotice(list, Text("Local reaches the same router only.", "로컬은 같은 공유기끼리만 됩니다."));
    }
}

/** @brief One short line after a mode change; remote tests the server at once. */
const char* ModeChangedText(PotluckService* s, u32 next) {
    if (next == PotluckLdnMode_Off) return Text("Off after a reboot.", "재부팅하면 꺼집니다.");
    if (g_status.disabled) return Text("On after a reboot.", "재부팅하면 켜집니다.");
    if (next == PotluckLdnMode_Local) return RelaunchText();
    PotluckServerTestResult result{};
    return R_SUCCEEDED(potluckTestServer(s, &result)) ? overlay::ServerTestText(result) : RelaunchText();
}

/**
 * @brief Mode row ([ldn] mode): A cycles remote, local and off; a game picks it up when it starts.
 *
 * Shows the mode games play in, so remote without a server reads as local.
 * Without a server address only local and off are offered.
 */
tsl::elm::ListItem* MakeModeItem(const LiveStatus& status) {
    const u32 mode = status.mode;
    const u32 shown = ShownMode(status);
    auto item = new tsl::elm::ListItem(Text("LAN Play mode", "LAN Play 모드"), ModeText(shown));
    item->setClickListener([mode, shown](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        auto s = potluckGetService();
        const u32 after_off = g_status.has_server ? PotluckLdnMode_Remote : PotluckLdnMode_Local;
        const u32 next = shown == PotluckLdnMode_Remote ? PotluckLdnMode_Local
                       : shown == PotluckLdnMode_Local  ? PotluckLdnMode_Off
                                                        : after_off;
        if (s && R_SUCCEEDED(potluckSetLdnMode(s, next)) && SaveConfig(s)) {
            ShowNotice(ModeChangedText(s, next));
        } else {
            // The setter may have changed the module; undo it there so a later save cannot persist it
            if (s) static_cast<void>(potluckSetLdnMode(s, mode));
            ShowNotice(Text("Could not save.", "저장하지 못했습니다."));
        }
        ++g_settingsGeneration;  // redraw the list for the mode the module now holds
        return true;
    });
    return item;
}

tsl::elm::ListItem* MakeReconnectItem() {
    auto item = new tsl::elm::ListItem(Text("Force reconnect", "강제 재연결"));
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        auto s = potluckGetService();
        ShowNotice(s && R_SUCCEEDED(potluckForceReconnect(s))
                       ? Text("Reconnecting to the relay server.", "중계 서버에 다시 연결합니다.")
                       : Text("Could not reconnect.", "다시 연결하지 못했습니다."));
        return true;
    });
    return item;
}

/** @brief Server address row; A opens the address keypad, and saving tests the server. */
tsl::elm::ListItem* MakeServerItem(PotluckService* svc) {
    char host[64] = "";
    u16 port = 0;
    static char value[64];
    if (R_FAILED(potluckGetServerAddress(svc, host, &port))) snprintf(value, sizeof(value), "%s", Text("Cannot read", "확인 불가"));
    else snprintf(value, sizeof(value), "%s", host[0] ? host : Text("None", "없음"));
    auto item = new tsl::elm::ListItem(Text("Server address", "서버 주소"), value);
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        tsl::changeTo<overlay::ServerHostGui>();
        return true;
    });
    return item;
}

/** @brief Port row; the default port is written out so it is never a guess. */
tsl::elm::ListItem* MakePortItem(PotluckService* svc) {
    char host[64] = "";
    u16 port = 0;
    static char value[24];
    if (R_FAILED(potluckGetServerAddress(svc, host, &port))) snprintf(value, sizeof(value), "%s", Text("Cannot read", "확인 불가"));
    else if (port == 0 || port == overlay::DefaultPort) snprintf(value, sizeof(value), Text("%u (default)", "%u (기본값)"), overlay::DefaultPort);
    else snprintf(value, sizeof(value), "%u", port);
    auto item = new tsl::elm::ListItem(Text("Port", "포트"), value);
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        tsl::changeTo<overlay::ServerPortGui>();
        return true;
    });
    return item;
}

/** @brief P2P switch ([ldn] disable_p2p, inverted); rooms created afterwards use it. */
tsl::elm::ToggleListItem* MakeP2pItem(PotluckService* svc) {
    u32 disabled = 1;
    const bool read = R_SUCCEEDED(potluckGetDisableP2p(svc, &disabled));
    auto item = new tsl::elm::ToggleListItem(Text("P2P", "P2P 직접 연결"), read && disabled == 0);
    item->setStateChangedListener([item](bool on) {
        auto s = potluckGetService();
        if (s && R_SUCCEEDED(potluckSetDisableP2p(s, on ? 0 : 1)) && SaveConfig(s)) {
            ShowNotice(Text("Applies from the next room.", "다음 방부터 적용됩니다."));
            return;
        }
        if (s) static_cast<void>(potluckSetDisableP2p(s, on ? 1 : 0));
        item->setState(!on);
        ShowNotice(Text("Could not save.", "저장하지 못했습니다."));
    });
    return item;
}

/**
 * @brief The settings on their own page, so neither page scrolls.
 *
 * Every row works in both modes; the channel, P2P, reconnect and the server
 * only matter when a game plays in remote mode.
 */
class SettingsGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame(Text("Settings", "설정"), g_version);
        auto list = new tsl::elm::List();
        frame->setContent(list);
        PotluckService* svc = potluckGetService();
        if (!svc) {
            AddNotice(list, Text("The module did not answer.", "모듈이 응답하지 않습니다."));
            return frame;
        }
        RefreshStatus();
        list->addItem(MakeRoomCodeItem(svc));
        list->addItem(MakeP2pItem(svc));
        list->addItem(MakeReconnectItem());
        list->addItem(MakeServerItem(svc));
        list->addItem(MakePortItem(svc));
        AddTimedNotice(list);
        return frame;
    }

    void update() override {
        if (m_generation != g_settingsGeneration) tsl::swapTo<SettingsGui>();
    }

private:
    u32 m_generation = g_settingsGeneration;
};

/** @brief Remote only: probes the relay server and shows the result under the rows. */
tsl::elm::ListItem* MakeTestItem() {
    auto item = new tsl::elm::ListItem(Text("Test the connection", "연결 시험"));
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        auto s = potluckGetService();
        PotluckServerTestResult result{};
        ShowNotice(s && R_SUCCEEDED(potluckTestServer(s, &result)) ? overlay::ServerTestText(result)
                                                                   : Text("Could not test the server.", "서버를 시험하지 못했습니다."));
        return true;
    });
    return item;
}

/** @brief The last row on the main page: channel, P2P, reconnect and the server. */
tsl::elm::ListItem* MakeSettingsItem() {
    auto item = new tsl::elm::ListItem(Text("Settings", "설정"));
    item->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        tsl::changeTo<SettingsGui>();
        return true;
    });
    return item;
}

/** @brief The main page: the status card and one row into the settings. */
class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        StartupTrace("main-gui-create");
        auto frame = new tsl::elm::OverlayFrame("potluck.", g_version);
        auto list = new tsl::elm::List();
        frame->setContent(list);

        RefreshStatus();
        AddStatusCard(list);
        if (potluckFirmwareIsNewer(hosversionGet())) {
            AddNotice(list, Text("Firmware newer than " POTLUCK_MAX_FIRMWARE_TEXT ".", "펌웨어가 " POTLUCK_MAX_FIRMWARE_TEXT "보다 새롭습니다."));
            AddNotice(list, Text("Update PotLuck before you play.", "플레이하기 전에 업데이트하세요."));
        }

        switch (g_initState) {
            case InitState::Uninit:
                AddNotice(list, Text("Loading.", "불러오는 중입니다."));
                return frame;
            case InitState::Stopped:
                AddNotice(list, Text("The module is not running.", "모듈이 실행 중이 아닙니다."));
                return frame;
            case InitState::Error:
                AddNotice(list, Text("The module did not answer.", "모듈이 응답하지 않습니다."));
                return frame;
            case InitState::Loaded:
                break;
        }

        if (!potluckGetService()) {
            AddNotice(list, Text("The module did not answer.", "모듈이 응답하지 않습니다."));
            return frame;
        }
        if (g_status.idle) {
            AddNotice(list, Text("Another LDN module is running.", "다른 LDN 모듈이 켜져 있습니다."));
            AddNotice(list, Text("Reinstall and reboot.", "다시 설치하고 재부팅해야 합니다."));
            return frame;
        }
        list->addItem(MakeModeItem(g_status));
        AddModeGuide(list, g_status);
        if (ShownMode(g_status) == PotluckLdnMode_Remote) list->addItem(MakeTestItem());
        list->addItem(MakeSettingsItem());
        AddTimedNotice(list);
        return frame;
    }

    void update() override {
        if (m_generation != g_settingsGeneration) {
            tsl::swapTo<MainGui>();
            return;
        }
        if (++m_frames < RefreshFrames) return;
        m_frames = 0;
        RefreshStatus();
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (!(keysDown & HidNpadButton_R)) return false;
        RefreshStatus();  // R refreshes the card at once
        return true;
    }

private:
    u32 m_frames = 0;
    u32 m_generation = g_settingsGeneration;
};

//=============================================================================
// Overlay Class
//=============================================================================

class PotluckOverlay : public tsl::Overlay {
public:
    void initServices() override {
        StartupTrace("init-services");
        g_initState = InitState::Uninit;

        u64 language = 0;
        if (R_SUCCEEDED(setInitialize())) {
            if (R_SUCCEEDED(setGetSystemLanguage(&language))) overlay::SelectLanguage(language);
            setExit();
        }
        overlay::theme::Apply();

        tsl::hlp::doWithSmSession([&] {
            m_pminfo = R_SUCCEEDED(pminfoInitialize());
            const Result rc = potluckInitialize();
            if (R_FAILED(rc)) {
                u64 pid = 0;
                if (pmdmntGetProcessId(&pid, POTLUCK_PROGRAM_ID) == ProcessNotFound) {
                    snprintf(g_version, sizeof(g_version), "%s", POTLUCK_VERSION);
                    g_initState = InitState::Stopped;
                    return;
                }
                snprintf(g_version, sizeof(g_version), "IPC 0x%08X", rc);
                StartupTrace(g_version);
                g_initState = InitState::Error;
                return;
            }
            PotluckService* svc = potluckGetService();
            if (!svc || R_FAILED(potluckGetVersion(svc, g_version))) strcpy(g_version, "Unknown");
            g_initState = InitState::Loaded;
        });
    }

    void exitServices() override {
        potluckExit();
        if (m_pminfo) pminfoExit();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        StartupTrace("load-initial-gui");
        return initially<MainGui>();
    }

private:
    bool m_pminfo = false;
};

int main(int argc, char** argv) {
    StartupTrace("main", true);
    return tsl::loop<PotluckOverlay>(argc, argv);
}
