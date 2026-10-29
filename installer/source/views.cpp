// What each screen shows: the texts and the View of every Screen.
#include <algorithm>
#include <cstdio>
#include "app.hpp"
#include "potluck_firmware.h"

namespace gui {

namespace {

// The way back when the console does not boot.
std::string bootHint() {
    return std::string(T("If the console will not boot, delete the SD folder atmosphere/contents/",
                         "부팅이 되지 않으면 SD의 atmosphere/contents/")) + inst::Tid + T(".", " 폴더를 지우면 됩니다.");
}

std::string riskText(inst::Risk r) {
    switch (r) {
    case inst::Risk::DnsOpen: return T("Nintendo servers are not blocked on this console.", "이 기기는 닌텐도 서버가 차단되어 있지 않습니다.");
    case inst::Risk::DnsUnknown: return T("Could not check whether Nintendo servers are blocked.", "닌텐도 서버 차단 여부를 확인하지 못했습니다.");
    case inst::Risk::SerialPresent: return T("The serial number is not hidden.", "기기의 시리얼 번호가 가려져 있지 않습니다.");
    case inst::Risk::SerialUnknown: return T("Could not read the serial number.", "시리얼 번호를 확인하지 못했습니다.");
    case inst::Risk::ProdinfoNotBlank: return T("blank_prodinfo is off for the running system.", "실행 중인 시스템의 blank_prodinfo가 꺼져 있습니다.");
    case inst::Risk::ProdinfoUnknown: return T("Could not check blank_prodinfo.", "blank_prodinfo 설정을 확인하지 못했습니다.");
    case inst::Risk::OnSysMmc: return T("Running on sysMMC; emuMMC is safer.", "sysMMC에서 실행 중입니다. emuMMC가 더 안전합니다.");
    case inst::Risk::MmcUnknown: return T("Could not tell sysMMC from emuMMC.", "sysMMC인지 emuMMC인지 확인하지 못했습니다.");
    }
    return "";
}

// [ldn] mode in config.ini; the overlay changes it, this app only shows it.
std::string modeText(const std::string& mode, bool hasServer) {
    if (mode == "off") return T("Off (after a reboot)", "끔 (재부팅 후 적용)");
    if (mode == "local") return T("Local (same router)", "로컬 (같은 공유기)");
    if (!hasServer) return T("Local (no server address)", "로컬 (서버 주소 없음)");  // remote without a host plays locally
    return T("Remote (relay server)", "원격 (중계 서버)");
}

std::string serverText(const inst::Server& s) {
    if (s.host.empty()) return T("None (plays in local mode)", "없음 (로컬 모드로 동작합니다)");
    return s.host + ":" + (s.port.empty() ? std::to_string(inst::DefaultPort) : s.port);
}
// Each language is named in itself, so it can be found from either one.
std::string languageText() {
    switch (language()) {
    case Language::English: return "English";
    case Language::Korean: return "한국어";
    case Language::Auto: break;
    }
    return std::string(T("Console (", "기기 설정 (")) + (korean ? "한국어" : "English") + ")";
}

Item mainRow(const App& app, MainItem item) {
    switch (item) {
    case MainItem::Install: return {app.state.present ? T("Install / update", "설치 / 업데이트") : T("Install", "설치"), app.latest.ok, "", Icon::Download};
    case MainItem::Server: return {T("Server address", "서버 주소"), true, serverText(app.server), Icon::Server};
    case MainItem::Options: return {T("P2P and diagnostics", "P2P와 진단"), app.state.present, "", Icon::Sliders};
    case MainItem::Games: return {T("LAN-play games", "LAN 플레이 가능 게임"), app.state.present, "", Icon::Gamepad};
    case MainItem::Language: return {T("Language", "언어"), true, languageText(), Icon::Globe};
    case MainItem::Safety: return {T("Check before the internet", "인터넷 연결 전 검사"), true, "", Icon::Shield};
    case MainItem::Remove: return {T("Uninstall the PotLuck module", "PotLuck 모듈 삭제"), app.state.present || app.state.leftover, "", Icon::Trash};
    case MainItem::Help: return {T("Help (keys, boot trouble)", "도움말 (조작, 부팅 문제)"), true, "", Icon::Info};
    case MainItem::Restart: return {T("Restart the console", "기기 다시 시작"), true, "", Icon::Power};
    case MainItem::Legacy: return {T("Delete older version files", "이전 버전 파일 지우기"), true, T("deleted once the settings are carried over", "설정을 옮긴 뒤 자동으로 지웁니다"), Icon::Archive};
    }
    return {};
}

} // namespace

std::string codeText(inst::Code c) {
    switch (c) {
    case inst::Code::ZipOpen: return T("The downloaded file could not be opened.", "내려받은 파일을 열지 못했습니다.");
    case inst::Code::NotRelease: return T("That file is not a PotLuck release.", "PotLuck 릴리스 파일이 아닙니다.");
    case inst::Code::Corrupt: return T("The release file is damaged (CRC check failed). Nothing was changed.", "릴리스 파일이 손상되어 있습니다(CRC 검사 실패). 아무것도 바꾸지 않았습니다.");
    case inst::Code::TooLarge: return T("The release is larger than allowed. Nothing was changed.", "릴리스가 허용된 크기보다 큽니다. 아무것도 바꾸지 않았습니다.");
    case inst::Code::Read: return T("A file on the SD card could not be read, so nothing was changed.", "SD 카드의 파일을 읽지 못해서 아무것도 바꾸지 않았습니다.");
    case inst::Code::Write: return T("Writing to the SD card failed.", "SD 카드에 쓰지 못했습니다.");
    case inst::Code::Remove: return T("Some files could not be removed. Remove again to finish.", "일부 파일을 지우지 못했습니다. 삭제를 다시 실행하면 이어서 지웁니다.");
    case inst::Code::NotOurs: return T("PotLuck is not installed, so nothing was removed.", "PotLuck이 설치되어 있지 않아서 아무것도 지우지 않았습니다.");
    case inst::Code::Migrate: return T("The older version's folder could not be moved.", "이전 버전의 폴더를 옮기지 못했습니다.");
    case inst::Code::LegacyConflict:
        return T("The older version's folder (atmosphere/contents/4200000000000010) holds files that differ from PotLuck's. Nothing was moved or deleted. Compare the two folders on a PC.",
                 "이전 버전 폴더(atmosphere/contents/4200000000000010)에 PotLuck 폴더와 다른 파일이 있습니다. 아무것도 옮기거나 지우지 않았습니다. PC에서 두 폴더를 비교해 주세요.");
    default: return "";
    }
}

// A config.ini save that did not happen; the file is as it was either way.
std::string settingsError(inst::Code c) {
    return c == inst::Code::Read ? T("config.ini could not be read, so it was not changed.", "config.ini를 읽지 못해서 바꾸지 않았습니다.")
                                 : T("Could not write config.ini.", "config.ini에 쓰지 못했습니다.");
}

// What the running system itself confirmed; the risk lines say what it did not.
std::vector<std::string> confirmedLines(const inst::Facts& f) {
    std::vector<std::string> out;
    if (f.serial == inst::Serial::Blank)
        out.push_back(std::string(T("The serial number is hidden", "시리얼 번호가 가려져 있습니다")) +
                      (f.blankSerial.empty() ? "." : " (" + f.blankSerial + ")."));
    if (f.blankRuntime == 1) out.push_back(T("blank_prodinfo is on for the running system.", "실행 중인 시스템의 blank_prodinfo가 켜져 있습니다."));
    if (f.mmc == inst::Mmc::Emu) out.push_back(T("Running on emuMMC.", "emuMMC에서 실행 중입니다."));
    if (f.dns == inst::Reach::Blocked) out.push_back(T("Nintendo servers are blocked.", "닌텐도 서버가 차단되어 있습니다."));
    return out;
}

// exosphere.ini and emummc.ini as they are on the card: the next boot's settings, not what is running.
std::vector<std::string> sdSettingLines(const inst::Facts& f) {
    const auto value = [](int v) { return v < 0 ? std::string("?") : std::to_string(v); };
    const std::string emummc = f.emummcIni == -2 ? std::string(T("no file", "파일 없음")) : "enabled=" + value(f.emummcIni);
    return {T("Settings on the SD card (asked for at boot, not checked):", "SD 카드의 설정입니다(부팅 때 쓰일 값이며 확인된 상태가 아닙니다)."),
            "exosphere.ini: blank_prodinfo_sysmmc=" + value(f.exo.blankSys) + ", blank_prodinfo_emummc=" + value(f.exo.blankEmu),
            "emummc.ini: " + emummc};
}

std::vector<MainItem> mainItems(const App& app) {
    // Install first, everyday settings next, the destructive ones last
    std::vector<MainItem> items{MainItem::Install, MainItem::Server, MainItem::Options, MainItem::Games, MainItem::Language, MainItem::Safety, MainItem::Help};
    if (app.state.legacyLeft) items.push_back(MainItem::Legacy);
    items.push_back(MainItem::Restart);
    items.push_back(MainItem::Remove);
    return items;
}
int indexOf(const App& app, MainItem item) {
    const auto items = mainItems(app);
    for (size_t i = 0; i < items.size(); ++i) if (items[i] == item) return int(i);
    return 0;
}

View viewOf(const App& app) {
    View v;
    v.title = title();
    v.selected = app.selected;
    switch (app.screen) {
    case Screen::Done:
        v.lines = app.result;
        // Back first, so a stray A never restarts the console
        v.menu = {{T("Back", "돌아가기"), true, "", Icon::Back}, {T("Restart the console now", "지금 기기 다시 시작"), true, "", Icon::Power}};
        v.hints = {{'A', T("Select", "선택")}};
        return v;
    case Screen::ConfirmDelete:
        v.notes = {{Note::Warn, T("Uninstall the PotLuck module. Your settings (config.ini) stay by default.", "PotLuck 모듈을 삭제합니다. 설정(config.ini)은 기본으로 남깁니다.")}};
        v.menu = {{T("Remove, keep settings", "설정은 남기고 삭제"), true, "", Icon::Trash},
                  {T("Remove everything, settings too", "설정까지 모두 지우기"), true, "", Icon::Trash},
                  {T("Cancel", "취소"), true, "", Icon::Back}};
        v.hints = {{'A', T("Select", "선택")}, {'B', T("Back", "뒤로")}};
        return v;
    case Screen::Server:
        v.title = T("Server address", "서버 주소");
        v.menu = {{T("Address (IP or host name)", "주소 (IP 또는 호스트 이름)"), app.state.present, app.server.host.empty() ? T("None (plays in local mode)", "없음 (로컬 모드로 동작합니다)") : app.server.host, Icon::Server},
                  {T("Port (1 to 65535)", "포트 (1~65535)"), app.state.present, app.server.port.empty() ? std::to_string(inst::DefaultPort) + T(" (default)", " (기본값)") : app.server.port, Icon::Hash},
                  {T("Test the connection", "서버 연결 시험"), app.online, "", Icon::Wifi},
                  {T("Clear the server address", "서버 주소 지우기"), app.state.present && (!app.server.host.empty() || !app.server.port.empty()), "", Icon::Trash},
                  {T("Back", "뒤로"), true, "", Icon::Back}};
        if (!app.state.present) v.notes = {{Note::Warn, T("Install PotLuck first.", "PotLuck을 먼저 설치하세요.")}};
        v.hint = T("It applies after you restart the console.", "재부팅하면 적용됩니다.");
        v.hints = {{'A', T("Select", "선택")}, {'B', T("Back", "뒤로")}};
        return v;
    case Screen::Options: {
        const auto onOff = [](bool on) { return std::string(on ? T("on", "켜짐") : T("off", "꺼짐")); };
        const char* levels[] = {T("errors", "오류"), T("warnings", "경고"), T("info", "정보"), T("everything", "상세")};
        v.title = T("P2P and diagnostics", "P2P와 진단");
        v.menu = {{T("P2P", "P2P"), true, app.options.p2p ? T("on", "켜짐") : T("off (relay only)", "꺼짐 (중계만)"), Icon::Share},
                  {T("Diagnostic log", "진단 로그"), true, onOff(app.options.debug), Icon::File},
                  {T("Log level", "로그 수준"), true, levels[std::clamp(app.options.level, 0, 3)], Icon::Layers},
                  {T("Back", "뒤로"), true, "", Icon::Back}};
        if (app.options.debug) v.notes = {{Note::Good, T("The log is written to config/potluck/potluck.log.", "로그는 config/potluck/potluck.log에 남습니다.")}};
        v.hint = T("The log applies at once, P2P after a restart.", "로그는 바로, P2P는 재부팅하면 적용됩니다.");
        v.hints = {{'A', T("Change", "바꾸기")}, {'B', T("Back", "뒤로")}};
        return v;
    }
    case Screen::Games: {
        const int total = int(app.games.size()), pages = std::max(1, (total + GamePage - 1) / GamePage);
        const int first = app.gamePage * GamePage;
        v.title = T("LAN-play games", "LAN 플레이 가능 게임");
        if (total == 0) v.lines = {T("No LAN-play game is installed on this console.", "이 기기에 설치된 LAN 플레이 가능 게임이 없습니다.")};
        else v.lines = {std::string(T("Installed games on the module's list: ", "목록에 있는 설치된 게임: ")) + std::to_string(total) +
                        T("  (page ", "  (") + std::to_string(app.gamePage + 1) + "/" + std::to_string(pages) + T(")", "쪽)")};
        for (int i = first; i < std::min(total, first + GamePage); ++i) {
            char id[24];
            snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(app.games[i].id));
            v.menu.push_back({app.games[i].name.empty() ? std::string(id) : app.games[i].name, true, app.games[i].name.empty() ? "" : id, Icon::Gamepad});
        }
        v.hint = T("The list is replaced by every update, so it can only be viewed here.", "목록은 업데이트 때마다 바뀌므로 여기서는 보기만 합니다.");
        v.hints = {{Shoulders, T("Page", "쪽 넘기기")}, {'B', T("Back", "뒤로")}};
        return v;
    }
    case Screen::ConfirmLegacy:
        v.notes = {{Note::Warn, T("This deletes the older version's overlay and its settings (config/ryu_ldn_nx) now. To carry the old settings over instead, restart and open this app again.",
                                  "이전 버전의 오버레이와 설정(config/ryu_ldn_nx)을 지금 지웁니다. 이전 설정을 새 버전으로 옮기려면 지우지 말고 재부팅한 뒤 이 앱을 다시 여세요.")}};
        v.menu = {{T("Delete the older files", "이전 버전 파일 지우기"), true, "", Icon::Archive}, {T("Cancel", "취소"), true, "", Icon::Back}};
        v.hints = {{'A', T("Select", "선택")}, {'B', T("Back", "뒤로")}};
        return v;
    case Screen::Safety: {
        v.title = T("Check before the internet", "인터넷 연결 전 검사");
        v.compact = true;
        v.lines = app.confirmed;
        for (auto r : app.risks) v.lines.push_back(riskText(r));
        for (const auto& d : app.dnsDetail) v.lines.push_back(d);
        for (const auto& d : app.sdSettings) v.lines.push_back(d);
        if (app.safe) {
            v.notes = {{Note::Good, app.risks.empty() ? T("Safe: no problem found.", "안전합니다. 문제가 없습니다.")
                                                      : T("Safe: the serial number is hidden, so Nintendo cannot tell this console apart.",
                                                          "안전합니다. 시리얼 번호가 가려져 있어 닌텐도가 이 기기를 식별할 수 없습니다.")}};
        } else {
            v.notes = {{Note::Bad, T("Connecting with these risks can get the console banned.", "이 상태로 연결하면 기기가 밴될 수 있습니다.")}};
        }
        v.menu = {{T("Check again", "다시 검사"), true, "", Icon::Refresh}, {T("Back", "뒤로"), true, "", Icon::Back}};
        v.hints = {{'A', T("Select", "선택")}, {'B', T("Back", "뒤로")}};
        return v;
    }
    case Screen::Help:
        v.title = T("Help", "도움말");
        v.sections = {
            {T("Controls", "조작"),
             {T("D-Pad or left stick: move   A: select   B: back", "방향키 또는 왼쪽 스틱: 이동   A: 선택   B: 뒤로"),
              T("L / R: pages in the game list   +: quit (main screen)", "L / R: 게임 목록 쪽 넘기기   +: 종료 (첫 화면)")}},
            {T("In a game", "게임 중"),
             {T("Open the overlay with L + D-Pad Down + Right Stick Click. Some overlay loaders use another combination.",
                "L + 방향키 아래 + 오른쪽 스틱 클릭으로 오버레이를 엽니다. 오버레이 로더에 따라 키가 다를 수 있습니다."),
              T("Pick the LAN Play mode (off, local, remote), the channel and the server address there.",
                "거기서 LAN Play 모드(끔, 로컬, 원격)와 채널, 서버 주소를 정합니다.")}},
            {T("If the console does not boot", "부팅이 되지 않을 때"),
             {T("Put the SD card in a PC and delete this folder. Games and saves are not touched.",
                "SD 카드를 PC에 꽂고 아래 폴더를 지웁니다. 게임과 저장 데이터는 그대로입니다."),
              std::string("atmosphere/contents/") + inst::Tid,
              T("To only stop it starting, delete flags/boot2.flag in it. config.ini in it holds your settings.",
                "시작만 막으려면 그 안의 flags/boot2.flag만 지웁니다. 설정은 그 안의 config.ini에 있습니다.")}},
            {T("ldn_mitm", "ldn_mitm"),
             {T("You do not need ldn_mitm. PotLuck's local mode does the same job.",
                "ldn_mitm은 따로 필요 없습니다. PotLuck의 로컬 모드가 같은 일을 합니다."),
              T("Installing keeps ldn_mitm from starting at boot.",
                "설치할 때 ldn_mitm이 부팅 때 켜지지 않게 해 둡니다.")}},
        };
        v.hints = {{'B', T("Back", "뒤로")}};
        return v;
    case Screen::Main: break;
    }
    const std::string installed = app.state.present
        ? (app.state.tag.empty() ? T("installed (version unknown)", "설치됨 (버전을 알 수 없음)") : app.state.tag)
        : app.state.legacyOurs ? T("older version (moves on update)", "이전 버전 (업데이트하면 옮깁니다)")
                               : T("not installed", "설치되어 있지 않습니다");
    // Installed and newest on one line
    std::string version = installed;
    SDL_Color versionColor = pal.ink;
    if (!app.latest.ok) {
        version += std::string(T(", newest: could not check (", ", 최신 버전: 확인하지 못했습니다 (")) + app.latest.error + ")";
        versionColor = pal.bad;
    } else if (app.state.present && !app.state.tag.empty() && inst::sameVersion(app.state.tag, app.latest.tag)) {
        version += T(" (newest)", " (최신)");
        versionColor = pal.good;
    } else {
        version += std::string(T(", newest ", ", 최신 ")) + app.latest.tag;
        if (app.state.present && !app.state.tag.empty()) versionColor = pal.pending;
    }
    v.rows = {{T("Version", "버전"), version, versionColor}};
    if (app.state.present) v.rows.push_back({T("LAN Play mode", "LAN Play 모드"), modeText(app.options.mode, !app.server.host.empty()), pal.ink});
    if (app.recoveryFailed)
        v.notes.push_back({Note::Bad, T("An interrupted install could not be undone. Run Install / update again.", "중단된 설치를 되돌리지 못했습니다. 설치 / 업데이트를 다시 실행하세요.")});
    if (app.state.present && app.state.mitmFlag)
        v.notes.push_back({Note::Warn, T("ldn_mitm also starts at boot, so PotLuck stays idle. Run Install / update to turn ldn_mitm off.",
                                         "ldn_mitm도 부팅 때 켜지게 되어 있어 PotLuck이 쉬고 있습니다. 설치 / 업데이트를 다시 실행하면 ldn_mitm을 끕니다.")});
    else if (app.state.present && !app.state.enabled)
        v.notes.push_back({Note::Warn, T("PotLuck is installed but does not start at boot. Run Install / update to turn it on.",
                                         "PotLuck이 설치되어 있지만 부팅 때 시작하지 않습니다. 설치 / 업데이트를 다시 실행하면 켜집니다.")});
    if (app.newerFirmware)
        v.notes.push_back({Note::Warn, T("This firmware is newer than PotLuck supports (up to " POTLUCK_MAX_FIRMWARE_TEXT "). Update PotLuck before you play.",
                                         "이 펌웨어는 PotLuck이 지원하는 버전(" POTLUCK_MAX_FIRMWARE_TEXT "까지)보다 새롭습니다. 플레이하기 전에 PotLuck을 업데이트하세요.")});
    if (app.otherMitm)
        v.notes.push_back({Note::Warn, T("Another module intercepts ldn:u or bsd:u right now. PotLuck cannot start while it runs.",
                                         "지금 다른 모듈이 ldn:u 또는 bsd:u를 가로채고 있습니다. 그 모듈이 켜져 있으면 PotLuck이 시작되지 않습니다.")});
    for (MainItem item : mainItems(app)) v.menu.push_back(mainRow(app, item));
    v.hint = bootHint();
    v.hints = {{'A', T("Select", "선택")}, {'+', T("Quit", "종료")}};
    return v;
}

} // namespace gui
