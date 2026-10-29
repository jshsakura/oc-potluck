// Runs install.cpp on the host: "sdmc:/" is a folder named "sdmc:" in the working directory.
// Usage: test_install <release.zip> <not-a-release.zip>   (run.sh builds both)
#include "../source/install.hpp"
#include "../source/games.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace inst;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool has(const std::string& rel) { return access(sdPath(rel).c_str(), F_OK) == 0; }
static std::string slurp(const std::string& rel) { std::ifstream f(sdPath(rel), std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void put(const std::string& rel, const std::string& text) { makeDirs(rel); std::ofstream(sdPath(rel), std::ios::binary) << text; }
static const std::string C = ContentsDir, L = LegacyContentsDir;

static void freshSd(const std::string& zip, const std::string& bad) {
    // keep the zips, drop everything else
    std::system("mkdir -p keep && mv sdmc:/release.zip sdmc:/bad.zip keep/ 2>/dev/null; rm -rf sdmc:; mkdir sdmc: && mv keep/* sdmc:/; rmdir keep");
    (void)zip; (void)bad;
}

int main(int, char** argv) {
    const std::string zip = argv[1], bad = argv[2];
    mkdir("sdmc:", 0777);

    // A zip without the module files is refused before anything is written.
    Outcome o = installZip(bad, "v9", nullptr);
    CHECK(o.code == Code::NotRelease && !has(C + "exefs.nsp"));

    // ldn_mitm lives under its own id with its flag set.
    put(L + "toolbox.json", "{\"name\": \"ldn_mitm\"}");
    put(L + "exefs.nsp", "mitm");
    put(L + "flags/boot2.flag", "");
    State s = readState();
    CHECK(s.mitmPresent && s.mitmFlag && !s.present && !s.legacyOurs);

    // Installing turns PotLuck on and only ldn_mitm's flag off (both on ldn:u crash at boot).
    int steps = 0;
    o = installZip(zip, "v0.2.0", [&](int, int) { ++steps; });
    CHECK(o.code == Code::Ok && o.written >= 4 && steps > 0 && !o.legacyPending && o.mitmDisabled);
    CHECK(has(C + "exefs.nsp") && has(C + "main.npdm") && has(OverlayPath) && has(C + "flags/boot2.flag"));
    CHECK(slurp(L + "exefs.nsp") == "mitm" && slurp(L + "toolbox.json") == "{\"name\": \"ldn_mitm\"}" && !has(L + "flags/boot2.flag"));
    CHECK(!has("evil.txt") && !has("switch/hbmenu.nro") && !has("atmosphere/contents/0100000000001000/exefs.nsp"));
    CHECK(!has(C + "exefs.nsp.part") && !has(C + "exefs.nsp.bak") && !has("config/potluck_installer/stage"));
    CHECK(slurp(LedgerFile).find(MitmFlagMarker) != std::string::npos);
    s = readState();
    CHECK(s.present && s.enabled && s.tag == "v0.2.0" && s.mitmPresent && !s.mitmFlag && s.mitmDisabledByUs);

    // An update keeps the settings, keeps PotLuck on and remembers who turned ldn_mitm off.
    put(C + "config.ini", "server=192.0.2.1");
    put(C + "exefs.nsp", "old");
    o = installZip(zip, "v0.2.1", nullptr);
    CHECK(o.code == Code::Ok && o.kept == 0 && !o.mitmDisabled);  // the zip carries no config.ini; the flag was off already
    CHECK(slurp(C + "config.ini") == "server=192.0.2.1" && slurp(C + "exefs.nsp") != "old" && readState().tag == "v0.2.1");
    CHECK(readState().enabled && readState().mitmDisabledByUs);

    // Removal keeps config.ini by default, removes module-made files too, and gives ldn_mitm its flag back.
    put(C + "log/potluck.log", "log");
    put(std::string(LogDir) + "potluck.log", "log");
    o = uninstall(false);
    CHECK(o.code == Code::Ok && o.removed >= 4 && o.mitmRestored);
    CHECK(!has(C + "exefs.nsp") && !has(OverlayPath) && !has(C + "log/potluck.log") && !has(std::string(LogDir) + "potluck.log") && !has(C + "flags/boot2.flag") && !has(C + "toolbox.json"));
    CHECK(slurp(C + "config.ini") == "server=192.0.2.1" && !has(LedgerFile));
    CHECK(slurp(L + "exefs.nsp") == "mitm" && has(L + "flags/boot2.flag") && !readState().present && uninstall(false).code == Code::NotOurs);

    // Reinstall, then remove with the settings.
    o = installZip(zip, "v0.2.2", nullptr);
    CHECK(o.code == Code::Ok && o.mitmDisabled && slurp(C + "config.ini") == "server=192.0.2.1" && !has(L + "flags/boot2.flag"));
    o = uninstall(true);
    CHECK(o.code == Code::Ok && !has(C + "config.ini") && !has(std::string(ContentsDir)) && slurp(L + "exefs.nsp") == "mitm" && has(L + "flags/boot2.flag"));

    // ldn_mitm that was off stays off after removal: only a flag this app removed comes back.
    unlink(sdPath(L + "flags/boot2.flag").c_str());
    o = installZip(zip, "v0.2.3", nullptr);
    CHECK(o.code == Code::Ok && !o.mitmDisabled && slurp(LedgerFile).find(MitmFlagMarker) == std::string::npos);
    o = uninstall(true);
    CHECK(o.code == Code::Ok && !o.mitmRestored && !has(L + "flags/boot2.flag") && slurp(L + "exefs.nsp") == "mitm");

    // An older build (ryu_ldn_nx under ldn_mitm's id) is moved, and its old files wait until their settings are carried over.
    freshSd(zip, bad);
    put(L + "toolbox.json", "{\"name\": \"ryu_ldn_nx\"}");
    put(L + "exefs.nsp", "old build");
    put(L + "flags/boot2.flag", "");
    put(LegacyOverlayPath, "ovl");
    put("config/ryu_ldn_nx/config.ini", "[server]\nhost = 192.0.2.9\n[ldn]\nenabled = 1\npassphrase = secret\n");
    put("config/ryu_ldn_nx/gamelist.txt", "0100");
    s = readState();
    CHECK(s.legacyOurs && !s.mitmPresent && !s.present && s.legacyLeft);
    o = installZip(zip, "v0.2.0", nullptr);
    CHECK(o.code == Code::Ok && o.legacyPending && !o.mitmDisabled);
    CHECK(!has(L + "exefs.nsp") && !has(std::string(LegacyContentsDir) + "toolbox.json"));
    s = readState();
    CHECK(s.present && s.enabled && !s.legacyOurs);
    CHECK(!has(LegacyOverlayPath) && has("config/ryu_ldn_nx/config.ini"));  // the overlay goes; the settings wait for config.ini
    CHECK(!cleanupLegacy());
    put(ConfigFile, "[server]\nhost = 192.0.2.9\n[ldn]\npassphrase = secret\n");  // the module's first boot carries them over
    CHECK(cleanupLegacy() && !has("config/ryu_ldn_nx/config.ini") && !has("config/ryu_ldn_nx"));
    CHECK(!readState().legacyLeft);

    // Saving a setting before the module has run starts from the old settings, without the retired `[ldn] enabled`.
    freshSd(zip, bad);
    put("config/ryu_ldn_nx/config.ini", "[server]\nhost = 192.0.2.9\n[ldn]\nenabled = 0\npassphrase = secret\n");
    CHECK(setServerKey("port", "40000") == Code::Ok);
    CHECK(slurp(ConfigFile) == "[server]\nhost = 192.0.2.9\nport = 40000\n[ldn]\npassphrase = secret\n");
    CHECK(!has("config/ryu_ldn_nx"));  // every old key is in the new file, so the old one went

    // An older build next to an existing PotLuck folder: its binaries go, its settings move in when PotLuck has none.
    freshSd(zip, bad);
    put(C + "exefs.nsp", "potluck");
    put(L + "toolbox.json", "{\"name\": \"ryu_ldn_nx\"}");
    put(L + "exefs.nsp", "old build");
    put(L + "flags/boot2.flag", "");
    put(L + "config.ini", "[ldn]\npassphrase = secret\n");
    put(L + "gamelist.txt", "same");
    put(C + "gamelist.txt", "same");
    CHECK(migrateLegacy() == Code::Ok);
    CHECK(!has(std::string(LegacyContentsDir)) && slurp(ConfigFile) == "[ldn]\npassphrase = secret\n" && slurp(C + "exefs.nsp") == "potluck");
    CHECK(!has(C + "flags/boot2.flag"));

    // The server address: only its key changes, other lines and comments stay, a missing file is made.
    freshSd(zip, bad);
    CHECK(readServer().host.empty() && readServer().port.empty());
    CHECK(setServerKey("host", "play.example.com") == Code::Ok && readServer().host == "play.example.com" && readServer().port.empty());
    CHECK(setServerKey("port", "40000") == Code::Ok && readServer().port == "40000");
    put(ConfigFile, "; keep me\n[server]\nhost = 192.0.2.1\nport = 30456\nuse_tls = 0\n\n[network]\nconnect_timeout = 5000\n");
    CHECK(setServerKey("host", "192.0.2.77") == Code::Ok);
    CHECK(slurp(ConfigFile) == "; keep me\n[server]\nhost = 192.0.2.77\nport = 30456\nuse_tls = 0\n\n[network]\nconnect_timeout = 5000\n");
    CHECK(!has(std::string(ConfigFile) + ".part"));

    // Games: installed ones that the list names, with their names; blank, # and ; lines and a bad id are skipped.
    CHECK(gameIdOf("0100ABCD00000800") == 0x0100ABCD00000800ull && gameIdOf("0x0100abcd00000800") == 0x0100ABCD00000800ull);
    CHECK(gameIdOf("") == 0 && gameIdOf("# note") == 0 && gameIdOf("; note") == 0 && gameIdOf("0100ABCD0000080") == 0 && gameIdOf("0100ABCD000008000") == 0 && gameIdOf("0100ABCD0000080G") == 0 && gameIdOf("  0100ABCD00000800\r") != 0);
    freshSd(zip, bad);
    put(GameListFile, "# list\n0100000000000001\n0x0100000000000002\n\n; x\n0100000000000003\n0100000000000002\nbad\n");
    put(GameNamesFile, "0100000000000001\tFirst Game\n0100000000000003\tThird Game\r\nbroken line\n");
    auto found = installedSupported({0x0100000000000002ull, 0x0100000000000001ull, 0x0100000000000009ull});
    CHECK(found.size() == 2 && found[0].id == 0x0100000000000001ull && found[0].name == "First Game" && found[1].id == 0x0100000000000002ull && found[1].name.empty());
    CHECK(installedSupported({}).empty() && installedSupported({0x0100000000000009ull}).empty());
    unlink(sdPath(GameListFile).c_str());
    CHECK(installedSupported({0x0100000000000001ull}).empty());

    // Options: module defaults when absent; a missing key lands at the end of its section, a missing section at the end of the file.
    freshSd(zip, bad);
    Options opt = readOptions();
    CHECK(!opt.p2p && !opt.debug && opt.level == 1);  // defaults: relay only, log off
    put(ConfigFile, "[server]\nhost = a.example.com\n\n[ldn]\nmode = remote\npassphrase =\n\n[network]\nx = 1\n");
    CHECK(setConfigKey("ldn", "disable_p2p", "0") == Code::Ok && setConfigKey("debug", "level", "3") == Code::Ok);
    CHECK(slurp(ConfigFile) == "[server]\nhost = a.example.com\n\n[ldn]\nmode = remote\npassphrase =\ndisable_p2p = 0\n\n[network]\nx = 1\n\n[debug]\nlevel = 3\n");
    opt = readOptions();
    CHECK(opt.p2p && opt.level == 3 && !opt.debug);
    CHECK(setConfigKey("debug", "enabled", "1") == Code::Ok && readOptions().debug);
    put(ConfigFile, "[debug]\nlevel = 9\nenabled = yes\n");
    CHECK(readOptions().level == 1 && !readOptions().debug);  // anything but 0/1 and 0..3 reads as the default

    // Back to defaults: only host and port go.
    put(ConfigFile, "; keep me\n[server]\nhost = 192.0.2.77\nport = 30456\nuse_tls = 0\n\n[network]\nconnect_timeout = 5000\n");
    CHECK(clearServer() == Code::Ok);
    CHECK(slurp(ConfigFile) == "; keep me\n[server]\nuse_tls = 0\n\n[network]\nconnect_timeout = 5000\n" && readServer().host.empty());
    freshSd(zip, bad);
    CHECK(clearServer() == Code::Ok && !has(ConfigFile));

    // [ldn] mode is shown, not written: remote unless it says local or off.
    put(ConfigFile, "[ldn]\nmode = local\n");
    CHECK(readOptions().mode == "local");
    put(ConfigFile, "[ldn]\nmode = off\n");
    CHECK(readOptions().mode == "off");
    put(ConfigFile, "[ldn]\nmode = remote\n");
    CHECK(readOptions().mode == "remote");
    put(ConfigFile, "[ldn]\nmode = sideways\n");
    CHECK(readOptions().mode == "remote");

    // Unpacked sizes are bounded per file and in all; nothing changes when a release is over either.
    freshSd(zip, bad);
    std::system("cp fixtures/big.zip sdmc:/big.zip");
    CHECK(installZip("big.zip", "v1", nullptr, Limits{1UL << 20, 64UL << 20}).code == Code::TooLarge);
    CHECK(installZip("big.zip", "v1", nullptr, Limits{16UL << 20, 2UL << 20}).code == Code::TooLarge);
    CHECK(!has(C + "exefs.nsp") && !has(C + "flags/boot2.flag") && !has("config/potluck_installer/stage"));
    CHECK(installZip("big.zip", "v1", nullptr).code == Code::Ok && has(C + "big.bin"));

    // The app's language: nothing saved is "auto"; a saved one comes back; anything else is "auto" again.
    freshSd(zip, bad);
    CHECK(readLanguage() == "auto");
    CHECK(writeLanguage("ko") == Code::Ok && readLanguage() == "ko");
    CHECK(writeLanguage("en") == Code::Ok && readLanguage() == "en");
    put(LanguageFile, "fr\n");
    CHECK(readLanguage() == "auto");

    if (!failures) std::puts("All tests passed!");
    return failures ? 1 : 0;
}
