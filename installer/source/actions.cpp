// What the keys do on each screen, and the work behind them: install, remove, check, edit config.ini.
#include "potluck_firmware.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app.hpp"
#include "mitm.hpp"

namespace gui {

void busy(const char* en, const char* ko) {
    View v;
    v.title = title();
    v.lines = {T(en, ko)};
    draw(v);
}

void refresh(App& app) {
    app.state = inst::readState();
    app.server = inst::readServer();
    app.options = inst::readOptions();
    app.otherMitm = inst::otherMitmActive();
    app.newerFirmware = potluckFirmwareIsNewer(hosversionGet());
}

void check(App& app) {
    busy("Checking the newest release...", "최신 버전을 확인하는 중입니다...");
    app.latest = app.online ? net::fetchLatest() : net::Latest{};
    if (!app.online) app.latest.error = "network";
    app.recoveryFailed = !inst::recoverInterrupted();
    inst::cleanupLegacy();
    refresh(app);
}

namespace {

// The result screen: one line saying what happened, which ends with the reboot notice when it needs one.
void finish(App& app, std::vector<std::string> lines) {
    app.result = std::move(lines);
    app.screen = Screen::Done;
    app.selected = 0;
    refresh(app);
}
void failed(App& app, inst::Code code) { finish(app, {codeText(code)}); }

// A normal HOS restart: running software is closed first. Every file was already closed.
void restartConsole() {
    busy("Restarting...", "다시 시작하는 중입니다...");
    if (R_SUCCEEDED(spsmInitialize())) {
        static_cast<void>(spsmShutdown(true));
        spsmExit();
    }
}

void install(App& app) {
    View v;
    v.title = title();
    v.hints = {{'B', T("Cancel download", "내려받기 취소")}};
    const std::string zip = std::string(inst::WorkDir) + "release.zip";
    inst::makeDirs(zip);
    std::string error;
    auto onProgress = [&](long now, long total) {
        v.lines = {T("Downloading ", "내려받는 중: ") + app.latest.tag};
        v.progress = total > 0 ? double(now) / double(total) : 0;
        draw(v);
        return !(pressed() & HidNpadButton_B);
    };
    if (!net::download(app.latest.url, inst::sdPath(zip), app.latest.size, onProgress, error)) {
        finish(app, {error == "cancel" ? T("Cancelled.", "취소했습니다.") : T("Download failed: ", "내려받지 못했습니다: ") + error});
        return;
    }
    v.hints.clear();
    const inst::Outcome out = inst::installZip(zip, app.latest.tag, [&](int done, int total) {
        v.lines = {T("Installing ", "설치하는 중: ") + app.latest.tag};
        v.progress = total > 0 ? double(done) / double(total) : 0;
        draw(v);
    });
    remove(inst::sdPath(zip).c_str());
    if (out.code != inst::Code::Ok) { failed(app, out.code); return; }
    std::vector<std::string> lines{T("Installed. Restart the console to apply it.", "설치했습니다. 재부팅하면 적용됩니다.")};
    if (out.mitmDisabled)
        lines.push_back(T("ldn_mitm's boot flag was turned off because PotLuck now includes the local mode. Removing PotLuck turns it back on.",
                          "PotLuck에 로컬 모드가 들어 있어서 ldn_mitm의 boot2.flag를 껐습니다. PotLuck을 삭제하면 다시 켭니다."));
    if (out.legacyPending)
        lines.push_back(T("Files of the older version stay until you restart and open this app again.", "이전 버전의 파일은 재부팅한 뒤 이 앱을 다시 열면 정리합니다."));
    finish(app, lines);
}

void uninstallAll(App& app, bool withSettings) {
    const inst::Outcome out = inst::uninstall(withSettings);
    if (out.code != inst::Code::Ok) { failed(app, out.code); return; }
    std::vector<std::string> lines{withSettings ? T("Removed with the settings. Restart the console to finish.", "설정까지 모두 지웠습니다. 재부팅하면 끝납니다.")
                                                : T("Removed; config.ini was kept. Restart the console to finish.", "삭제했습니다. 설정(config.ini)은 남겨 두었습니다. 재부팅하면 끝납니다.")};
    if (out.mitmRestored) lines.push_back(T("ldn_mitm starts at boot again.", "ldn_mitm이 다시 부팅 때 시작됩니다."));
    finish(app, lines);
}

void runSafety(App& app) {
    busy("Checking the console...", "기기를 검사하는 중입니다...");
    const inst::Facts facts = inst::gatherFacts(app.online);
    app.risks = inst::risks(facts);
    app.safe = inst::isSafe(facts);
    app.confirmed = confirmedLines(facts);
    app.dnsDetail = facts.dnsDetail;
    app.sdSettings = sdSettingLines(facts);
    app.screen = Screen::Safety;
    app.selected = 0;
}

// The system keyboard; false when the user cancels it.
// After config.ini changed: the running module reads it again, or the change waits for a restart.
const char* appliedLine() {
    return inst::reloadModuleConfig() ? T("Saved and applied.", "저장했고 바로 적용했습니다.")
                                      : T("Saved. Restart the console to apply it.", "저장했습니다. 재부팅하면 적용됩니다.");
}

bool askText(const char* guide, const std::string& initial, int maxLength, bool numbers, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    if (numbers) swkbdConfigSetType(&kbd, SwkbdType_NumPad);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, u32(maxLength));
    char buffer[300] = {};
    const Result rc = swkbdShow(&kbd, buffer, sizeof(buffer));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = inst::trimmed(buffer);
    return true;
}

// Open a TCP connection to the saved address; one line saying whether it worked and how long it took.
std::string probeLine(const App& app) {
    const int port = app.server.port.empty() ? inst::DefaultPort : std::atoi(app.server.port.c_str());
    const int ms = net::probe(app.server.host, port, 5000);
    if (ms >= 0) return std::string(T("Connection OK (", "연결 성공 (")) + std::to_string(ms) + " ms)";
    switch (ms) {
        case net::ProbeNotFound: return T("Connection failed: this name does not exist in DNS. Check the spelling.", "연결 실패: DNS에 없는 이름입니다. 철자를 확인해야 합니다.");
        case net::ProbeIpv6Only: return T("Connection failed: this name has only an IPv6 address; an IPv4 (A) record is needed.", "연결 실패: IPv6 주소만 있는 이름입니다. IPv4(A) 레코드가 필요합니다.");
        case net::ProbeDnsDown: return T("Connection failed: the DNS server did not answer.", "연결 실패: DNS 서버가 응답하지 않았습니다.");
        case net::ProbeRefused: return T("Connection failed: the host answered, but no server is on that port.", "연결 실패: 주소는 맞지만 그 포트에 서버가 없습니다.");
        default: return T("Connection failed: no answer in time. Check the address, the port and the firewall.", "연결 실패: 응답이 없습니다. 주소, 포트, 방화벽을 확인해야 합니다.");
    }
}

// Ask for the host or the port, check it, and write only that key into config.ini.
void editServer(App& app, bool host) {
    std::string value;
    const bool ok = host ? askText(T("Server address (IP or host name)", "서버 주소 (IP 또는 호스트 이름)"), app.server.host, 253, false, value)
                         : askText(T("Port (1 to 65535)", "포트 (1~65535)"), app.server.port, 5, true, value);
    if (!ok) {
        // The system keyboard could not open, or the user backed out of it
        finish(app, {T("Nothing was saved.", "저장하지 않았습니다."),
                     T("If no keyboard appeared, open this app from a game's title (hold R) and try again.",
                       "키보드가 뜨지 않았다면 게임을 R 버튼을 누른 채 실행해 이 앱을 연 뒤 다시 시도하세요.")});
        return;
    }
    if (host ? !inst::validHost(value) : !inst::validPort(value)) {
        finish(app, {host ? T("Not saved: use an IPv4 address or a host name without spaces.", "저장하지 않았습니다. 공백 없는 호스트 이름이나 IPv4 주소를 입력하세요.")
                          : T("Not saved: the port must be a number from 1 to 65535.", "저장하지 않았습니다. 포트는 1에서 65535 사이의 숫자여야 합니다.")});
        return;
    }
    const inst::Code saved = inst::setServerKey(host ? inst::HostKey : inst::PortKey, value);
    if (saved != inst::Code::Ok) {
        finish(app, {settingsError(saved)});
        return;
    }
    refresh(app);
    std::vector<std::string> lines{appliedLine()};
    if (app.online && !app.server.host.empty()) {
        busy("Connecting to the server...", "서버에 연결하는 중입니다...");
        lines.push_back(probeLine(app));
    }
    finish(app, lines);
}

// The console's own name for each game on the page, in the console's language; game_names.tsv only fills the gaps.
void fillNames(App& app) {
    const int first = app.gamePage * GamePage, last = std::min(int(app.games.size()), first + GamePage);
    if (R_FAILED(nsInitialize())) return;
    for (int i = first; i < last; ++i) {
        inst::Game& g = app.games[i];
        if (g.named) continue;
        g.named = true;
        auto* data = new NsApplicationControlData;
        u64 got = 0;
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, g.id, data, sizeof(*data), &got)) && got >= sizeof(data->nacp)) {
            NacpLanguageEntry* entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &entry)) && entry && entry->name[0]) g.name.assign(entry->name, strnlen(entry->name, sizeof(entry->name)));
        }
        delete data;
    }
    nsExit();
}

void testServer(App& app) {
    if (app.server.host.empty()) {
        finish(app, {T("A server address is needed for remote mode.", "원격 모드를 쓰려면 서버 주소가 필요합니다.")});
        return;
    }
    busy("Connecting to the server...", "서버에 연결하는 중입니다...");
    finish(app, {probeLine(app)});
}

} // namespace

void handle(App& app, u64 down, bool& quit) {
    const View view = viewOf(app);
    const int count = app.screen == Screen::Games ? 0 : int(view.menu.size());
    if (count > 0 && (down & (HidNpadButton_Down | HidNpadButton_StickLDown))) app.selected = (app.selected + 1) % count;
    if (count > 0 && (down & (HidNpadButton_Up | HidNpadButton_StickLUp))) app.selected = (app.selected + count - 1) % count;
    // A greyed row does nothing, whether A or a tap reached it
    const bool rowOff = count > 0 && app.selected >= 0 && app.selected < count && !view.menu[app.selected].enabled;
    const bool back = down & HidNpadButton_B, accept = (down & HidNpadButton_A) && !rowOff;
    auto toMain = [&](MainItem item) { app.screen = Screen::Main; app.selected = indexOf(app, item); };
    switch (app.screen) {
    case Screen::Done:
        if (accept && app.selected == 1) { restartConsole(); return; }
        if (accept || back) { app.screen = Screen::Main; app.selected = 0; }
        return;
    case Screen::Help:
        if (accept || back) toMain(MainItem::Help);
        return;
    case Screen::ConfirmDelete:
        if (accept) {
            if (app.selected == 2) toMain(MainItem::Remove);
            else uninstallAll(app, app.selected == 1);
        }
        if (back) toMain(MainItem::Remove);
        return;
    case Screen::ConfirmLegacy:
        if (back || (accept && app.selected == 1)) { toMain(MainItem::Legacy); return; }
        if (accept) {
            const bool ok = inst::removeLegacy();
            finish(app, {ok ? T("Deleted the older version's files.", "이전 버전의 파일을 지웠습니다.")
                            : T("Some files could not be deleted. Remove config/ryu_ldn_nx on a PC.", "일부를 지우지 못했습니다. PC에서 config/ryu_ldn_nx 폴더를 지워 주세요.")});
        }
        return;
    case Screen::Safety:
        if (back || (accept && app.selected == 1)) { toMain(MainItem::Safety); return; }
        if (accept) runSafety(app);
        return;
    case Screen::Games: {
        if (back) { toMain(MainItem::Games); return; }
        const int total = int(app.games.size()), pages = std::max(1, (total + GamePage - 1) / GamePage);
        const int onPage = std::min(GamePage, std::max(0, total - app.gamePage * GamePage));
        // Up and Down run through the whole list and cross pages; L and R jump a page.
        if (down & HidNpadButton_L) { app.gamePage = (app.gamePage + pages - 1) % pages; app.selected = 0; }
        else if (down & HidNpadButton_R) { app.gamePage = (app.gamePage + 1) % pages; app.selected = 0; }
        else if ((down & (HidNpadButton_Down | HidNpadButton_StickLDown)) && app.selected >= onPage - 1) { app.gamePage = (app.gamePage + 1) % pages; app.selected = 0; }
        else if ((down & (HidNpadButton_Up | HidNpadButton_StickLUp)) && app.selected <= 0) {
            app.gamePage = (app.gamePage + pages - 1) % pages;
            app.selected = std::min(GamePage, total - app.gamePage * GamePage) - 1;
        }
        else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) ++app.selected;
        else if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) --app.selected;
        fillNames(app);
        return;
    }
    case Screen::Options: {
        if (back || (accept && app.selected == 3)) { toMain(MainItem::Options); return; }
        if (!accept) return;
        const bool flag = app.selected == 0 ? !app.options.p2p : !app.options.debug;
        inst::Code code = inst::Code::Ok;
        if (app.selected == 0) code = inst::setConfigKey("ldn", "disable_p2p", flag ? "0" : "1");
        else if (app.selected == 1) code = inst::setConfigKey("debug", "enabled", flag ? "1" : "0");
        else if (app.selected == 2) code = inst::setConfigKey("debug", "level", std::to_string((app.options.level + 1) % 4));
        if (code != inst::Code::Ok) finish(app, {settingsError(code)});
        else {
            app.options = inst::readOptions();
            static_cast<void>(inst::reloadModuleConfig());  // the module picks the option up at once
        }
        return;
    }
    case Screen::Server:
        if (back || (accept && app.selected == 4)) { toMain(MainItem::Server); return; }
        if (!accept) return;
        if (app.selected <= 1 && app.state.present) editServer(app, app.selected == 0);
        else if (app.selected == 2) testServer(app);
        else if (app.selected == 3 && app.state.present) {
            const inst::Code reset = inst::clearServer();
            if (reset != inst::Code::Ok) finish(app, {settingsError(reset)});
            else { refresh(app); finish(app, {T("The server address was cleared. Without one the module plays in local mode.", "서버 주소를 지웠습니다. 주소가 없으면 로컬 모드로 동작합니다."), appliedLine()}); }
        }
        return;
    case Screen::Main: break;
    }
    if (down & HidNpadButton_Plus) quit = true;
    if (!accept) return;
    const auto items = mainItems(app);
    if (app.selected < 0 || app.selected >= int(items.size())) return;
    switch (items[app.selected]) {
    case MainItem::Install:
        if (app.latest.ok) install(app);
        break;
    case MainItem::Server: app.screen = Screen::Server; app.selected = 0; break;
    case MainItem::Options:
        if (app.state.present) { app.screen = Screen::Options; app.selected = 0; }
        break;
    case MainItem::Games:
        if (app.state.present) {
            busy("Reading the game list...", "게임 목록을 읽는 중입니다...");
            app.games = inst::installedSupported(inst::installedApplications());
            app.gamePage = 0;
            app.screen = Screen::Games;
            app.selected = 0;
            fillNames(app);
        }
        break;
    case MainItem::Language: {
        // Auto, then English, then Korean, then back to the console's language
        const Language next = language() == Language::Auto ? Language::English : language() == Language::English ? Language::Korean : Language::Auto;
        setLanguage(next);
        if (inst::writeLanguage(languageCode(next)) != inst::Code::Ok)
            finish(app, {T("The language changed, but it could not be saved to the SD card, so it resets on the next start.",
                           "언어는 바꿨지만 SD 카드에 저장하지 못해 다음에 켜면 원래대로 돌아갑니다.")});
        break;
    }
    case MainItem::Safety: runSafety(app); break;
    case MainItem::Remove:
        if (app.state.present || app.state.leftover) { app.screen = Screen::ConfirmDelete; app.selected = 0; }
        break;
    case MainItem::Help: app.screen = Screen::Help; app.selected = 0; break;
    case MainItem::Restart: restartConsole(); break;
    case MainItem::Legacy: app.screen = Screen::ConfirmLegacy; app.selected = 1; break;
    }
}

} // namespace gui
