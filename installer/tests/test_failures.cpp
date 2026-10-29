// Failure injection for install.cpp: rename, unlink and fopen are wrapped (-Wl,--wrap) so a test can make
// one SD operation fail and then check that nothing the user had was lost.
// Usage: test_failures <fixture dir>   (run.sh builds the zips)
#include "../source/install.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace inst;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// ---- injected failures: a predicate on the path, cleared after each use ----
static std::function<bool(const std::string&)> failRenameTo, failUnlink, failOpenWrite;
extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* from, const char* to) {
    if (failRenameTo && failRenameTo(to)) { errno = EIO; return -1; }
    return __real_rename(from, to);
}
extern "C" int __real_unlink(const char*);
extern "C" int __wrap_unlink(const char* path) {
    if (failUnlink && failUnlink(path)) { errno = EIO; return -1; }
    return __real_unlink(path);
}
extern "C" FILE* __real_fopen(const char*, const char*);
extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    if (mode[0] == 'w' && failOpenWrite && failOpenWrite(path)) { errno = EIO; return nullptr; }
    return __real_fopen(path, mode);
}
static void clearFaults() { failRenameTo = nullptr; failUnlink = nullptr; failOpenWrite = nullptr; }
// Fails the first rename onto a path ending in `tail`; the ones after it (a rollback) work.
static std::function<bool(const std::string&)> onceTo(const std::string& tail) {
    auto used = std::make_shared<bool>(false);
    return [tail, used](const std::string& to) {
        if (*used || to.size() < tail.size() || to.compare(to.size() - tail.size(), tail.size(), tail) != 0) return false;
        return *used = true;
    };
}

static std::string fixtures;
static const std::string C = ContentsDir, L = LegacyContentsDir;
static bool has(const std::string& rel) { return access(sdPath(rel).c_str(), F_OK) == 0; }
static std::string slurp(const std::string& rel) { std::ifstream f(sdPath(rel), std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void put(const std::string& rel, const std::string& text) { makeDirs(rel); std::ofstream(sdPath(rel), std::ios::binary) << text; }
static bool contains(const std::string& rel, const std::string& part) { return slurp(rel).find(part) != std::string::npos; }
static bool endsIn(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

// An empty SD with the named fixture zips copied in.
static void freshSd(std::initializer_list<const char*> zips = {}) {
    std::system("chmod -R u+w sdmc: 2>/dev/null");
    clearFaults();
    fs::remove_all("sdmc:");
    fs::create_directory("sdmc:");
    for (const char* z : zips) fs::copy_file(fixtures + "/" + z, sdPath(z));
}
static void installed() {  // a working install of an older version, PotLuck on at boot
    put(C + "exefs.nsp", "original-binary");
    put(C + "main.npdm", "original-npdm");
    put(C + "toolbox.json", "{\"name\": \"potluck\"}");
    put(C + "flags/boot2.flag", "");
    put(OverlayPath, "original-ovl");
}
static bool noTransactionLeft() {
    for (const auto& e : fs::recursive_directory_iterator("sdmc:")) {
        const std::string p = e.path().string();
        if (endsIn(p, ".part") || endsIn(p, ".bak")) { std::printf("  left behind: %s\n", p.c_str()); return false; }
    }
    return true;
}

// HIGH: a failed rename must not lose the installed binary, nor the rest of the old version.
static void renameFailureKeepsOldFiles() {
    freshSd({"release.zip"});
    installed();
    failRenameTo = onceTo("4200000000504C4B/exefs.nsp");
    const Outcome o = installZip("release.zip", "v1", nullptr);
    clearFaults();
    CHECK(o.code != Code::Ok);
    CHECK(slurp(C + "exefs.nsp") == "original-binary" && slurp(C + "main.npdm") == "original-npdm" && slurp(OverlayPath) == "original-ovl");
    CHECK(has(C + "flags/boot2.flag"));  // the old version still starts as before
    CHECK(noTransactionLeft());

    // The card keeps failing, so even the rollback cannot finish: the next start does it.
    freshSd({"release.zip"});
    installed();
    failRenameTo = [](const std::string& to) { return endsIn(to, "4200000000504C4B/exefs.nsp"); };
    CHECK(installZip("release.zip", "v1", nullptr).code != Code::Ok);
    clearFaults();
    CHECK(!has(C + "exefs.nsp") && has(C + "exefs.nsp.bak"));
    CHECK(recoverInterrupted());
    CHECK(slurp(C + "exefs.nsp") == "original-binary" && slurp(C + "main.npdm") == "original-npdm" && has(C + "flags/boot2.flag"));
    CHECK(noTransactionLeft() && !has("config/potluck_installer/commit.txt"));
}

// A commit cut off half way (power loss): the journal rolls every file back and the flag comes back.
static void interruptedCommitIsRolledBack() {
    freshSd();
    installed();
    rename(sdPath(C + "exefs.nsp").c_str(), sdPath(C + "exefs.nsp.bak").c_str());
    put(C + "exefs.nsp", "new-binary");
    rename(sdPath(C + "main.npdm").c_str(), sdPath(C + "main.npdm.bak").c_str());  // cut off between the two renames
    unlink(sdPath(C + "flags/boot2.flag").c_str());
    put("config/potluck_installer/commit.txt", "potluck_commit 1\n1\nB " + C + "exefs.nsp\nB " + C + "main.npdm\nN " + C + "new.bin\n");
    put(C + "new.bin", "new");
    CHECK(recoverInterrupted());
    CHECK(slurp(C + "exefs.nsp") == "original-binary" && slurp(C + "main.npdm") == "original-npdm" && !has(C + "new.bin"));
    CHECK(has(C + "flags/boot2.flag") && noTransactionLeft() && !has("config/potluck_installer/commit.txt"));

    // A settings swap cut off after config.ini moved aside.
    put(std::string(ConfigFile) + ".bak", "[ldn]\npassphrase = private\n");
    CHECK(recoverInterrupted() && slurp(ConfigFile) == "[ldn]\npassphrase = private\n" && noTransactionLeft());
}

// HIGH: a config.ini that cannot be read is never replaced.
static void unreadableConfigIsKept() {
    freshSd();
    put(ConfigFile, "; KEEP-ME\n[ldn]\npassphrase = private\n");
    chmod(sdPath(ConfigFile).c_str(), 0200);
    const Code set = setServerKey("host", "test.example");
    const Code reset = clearServer();
    chmod(sdPath(ConfigFile).c_str(), 0600);
    CHECK(set != Code::Ok && reset != Code::Ok);
    CHECK(slurp(ConfigFile) == "; KEEP-ME\n[ldn]\npassphrase = private\n");

    // Larger than the reader takes: refused, not cut short.
    const std::string big = "[ldn]\npassphrase = private\n; " + std::string(70000, 'x') + "\n";
    put(ConfigFile, big);
    CHECK(setServerKey("host", "test.example") != Code::Ok && slurp(ConfigFile) == big);
}

// HIGH: a config.ini that fails to swap in stays as it was.
static void configSwapFailureKeepsOld() {
    freshSd();
    put(ConfigFile, "[ldn]\npassphrase = private\n");
    failRenameTo = onceTo("4200000000504C4B/config.ini");
    const Code set = setServerKey("host", "test.example");
    clearFaults();
    CHECK(set != Code::Ok && slurp(ConfigFile) == "[ldn]\npassphrase = private\n" && noTransactionLeft());
}

// HIGH: the release's own boot flag is never unpacked, and a failed install leaves the flag as it was.
static void failedInstallDoesNotEnable() {
    freshSd({"flagfirst.zip"});
    failOpenWrite = [](const std::string& p) { return p.find("main.npdm") != std::string::npos; };
    const Outcome o = installZip("flagfirst.zip", "v1", nullptr);
    clearFaults();
    CHECK(o.code != Code::Ok && !has(C + "flags/boot2.flag") && !has(C + "exefs.nsp"));

    // An update that fails keeps the old version running.
    freshSd({"flagfirst.zip"});
    installed();
    failOpenWrite = [](const std::string& p) { return p.find("main.npdm") != std::string::npos; };
    CHECK(installZip("flagfirst.zip", "v1", nullptr).code != Code::Ok);
    clearFaults();
    CHECK(has(C + "flags/boot2.flag") && slurp(C + "exefs.nsp") == "original-binary");
}

// HIGH: a zip entry whose CRC does not match is refused before any live file changes.
static void badCrcIsRefused() {
    freshSd({"badcrc.zip"});
    installed();
    const Outcome o = installZip("badcrc.zip", "v1", nullptr);
    CHECK(o.code != Code::Ok);
    CHECK(slurp(C + "exefs.nsp") == "original-binary" && slurp(C + "main.npdm") == "original-npdm" && slurp(OverlayPath) == "original-ovl");
    CHECK(noTransactionLeft());
}

// HIGH: an existing new folder does not make the old one disposable.
static void migrationCollisionKeepsData() {
    freshSd();
    put(L + "toolbox.json", "{\"name\":\"ryu_ldn_nx\"}");
    put(L + "exefs.nsp", "old build");
    put(L + "config.ini", "only-copy");
    makeDirs(C + "x");  // the new folder exists, empty
    const Code moved = migrateLegacy();
    const bool kept = (has(L + "config.ini") && slurp(L + "config.ini") == "only-copy") || (has(ConfigFile) && slurp(ConfigFile) == "only-copy");
    CHECK(moved == Code::Ok && kept);

    // Both folders hold different settings: nothing is deleted.
    freshSd();
    put(L + "toolbox.json", "{\"name\":\"ryu_ldn_nx\"}");
    put(L + "config.ini", "old settings");
    put(ConfigFile, "new settings");
    CHECK(migrateLegacy() != Code::Ok);
    CHECK(slurp(L + "config.ini") == "old settings" && slurp(ConfigFile) == "new settings" && has(L + "toolbox.json"));
}

// HIGH: the old settings stay until their keys are in the new config.ini.
static void cleanupWaitsForTheSettings() {
    freshSd();
    put(OverlayPath, "ovl");
    put(C + "toolbox.json", "{\"name\": \"potluck\"}");
    put(std::string(LegacyConfigDir) + "config.ini", "[ldn]\npassphrase = only-copy\n");
    put(ConfigFile, "[server]\nhost = new.example\n");  // made without the old settings
    cleanupLegacy();
    CHECK(has(std::string(LegacyConfigDir) + "config.ini"));

    // Saving a key with no config.ini yet starts from the old settings, so the passphrase is not lost.
    freshSd();
    put(std::string(LegacyConfigDir) + "config.ini", "[ldn]\npassphrase = only-copy\n");
    CHECK(setServerKey("host", "new.example") == Code::Ok);
    cleanupLegacy();
    CHECK(contains(ConfigFile, "only-copy") || has(std::string(LegacyConfigDir) + "config.ini"));
}

// MEDIUM: a removal that fails is reported and leaves enough behind for a retry.
static void uninstallFailureIsReported() {
    freshSd();
    installed();
    failUnlink = [](const std::string& p) { return endsIn(p, "4200000000504C4B/main.npdm"); };
    const Outcome o = uninstall(true);
    clearFaults();
    CHECK(o.code != Code::Ok && has(C + "toolbox.json") && readState().present);
    CHECK(uninstall(true).code == Code::Ok && !has(C + "main.npdm") && !has(C + "toolbox.json") && !has(OverlayPath));
}

// MEDIUM-1: the older build is moved only after the new files are in place; a failed commit leaves it running.
static void failedCommitKeepsOlderBuild() {
    freshSd({"release.zip"});
    put(L + "exefs.nsp", "old-ryu");
    put(L + "main.npdm", "old-npdm");
    put(L + "toolbox.json", "{\"name\": \"ryu_ldn_nx\"}");
    put(L + "flags/boot2.flag", "");
    failRenameTo = onceTo("4200000000504C4B/main.npdm");
    const Outcome o = installZip("release.zip", "v1", nullptr);
    clearFaults();
    CHECK(o.code != Code::Ok);
    CHECK(slurp(L + "exefs.nsp") == "old-ryu" && has(L + "flags/boot2.flag") && readState().legacyOurs);
    CHECK(!has(C + "exefs.nsp") && !has(C + "flags/boot2.flag") && noTransactionLeft());
    CHECK(installZip("release.zip", "v1", nullptr).code == Code::Ok && !has(std::string(LegacyContentsDir)) && has(C + "flags/boot2.flag"));
}

// MEDIUM-2: deep paths are refused, and a stage folder that cannot be cleaned never blocks recovery.
static void deepEntriesNeverBlock() {
    freshSd({"deep.zip"});
    const Outcome o = installZip("deep.zip", "v1", nullptr);
    CHECK(o.code == Code::Ok && has(C + "data/sub/x.bin") && !has(C + "a/b/c/d/e/f.bin") && o.skipped == 1);
    CHECK(!has("config/potluck_installer/stage") && recoverInterrupted());
    CHECK(uninstall(true).code == Code::Ok && !has(std::string(ContentsDir)));
    put("config/potluck_installer/stage/a/b/c/d/e/f/g/h/i/x", "left");
    CHECK(recoverInterrupted() && installZip("deep.zip", "v1", nullptr).code == Code::Ok);
}

// MEDIUM-3: many entries never make the ledger or the journal unreadable.
static void manyEntriesStayReadable() {
    freshSd({"many.zip"});
    const Outcome o = installZip("many.zip", "v1", nullptr);
    CHECK(o.code == Code::TooLarge && !has(C + "exefs.nsp"));
    freshSd({"release.zip"});
    CHECK(installZip("release.zip", "v1", nullptr).code == Code::Ok && slurp(LedgerFile).size() < 200);
    CHECK(readState().tag == "v1" && uninstall(true).code == Code::Ok);
}

// LOW-1 and LOW-2: FAT ignores case, and .bak/.part names belong to the installer's own swaps.
static void caseAndSwapNamesAreRefused() {
    freshSd({"case.zip", "bakname.zip"});
    put(ConfigFile, "[ldn]\npassphrase = mine\n");
    Outcome o = installZip("case.zip", "v1", nullptr);
    CHECK(o.code == Code::Corrupt);  // EXEFS.NSP is exefs.nsp twice on a FAT card
    CHECK(!extractable(C + "Flags/boot2.flag") && !extractable(C + "FLAGS/x") && keepIfPresent(C + "Config.ini"));
    CHECK(slurp(ConfigFile) == "[ldn]\npassphrase = mine\n");
    o = installZip("bakname.zip", "v1", nullptr);
    CHECK(o.code == Code::Ok && o.skipped == 2 && !has(C + "main.npdm.bak") && !has(C + "exefs.nsp.PART"));
}

// LOW-3: a key with another value is not carried over, so the old settings stay.
static void carryChecksValues() {
    freshSd();
    put(C + "toolbox.json", "{\"name\": \"potluck\"}");
    put(std::string(LegacyConfigDir) + "config.ini", "[server]\nhost = private.example\n[ldn]\npassphrase = secret\n");
    put(ConfigFile, "[server]\nhost = \nport = 30456\n[ldn]\npassphrase = \n");
    cleanupLegacy();
    CHECK(has(std::string(LegacyConfigDir) + "config.ini"));
    put(ConfigFile, "[server]\nhost = private.example\nport = 30456\n[ldn]\npassphrase = secret\n");
    CHECK(cleanupLegacy() && !has(std::string(LegacyConfigDir)));
}

// LOW-4: a removal cut off after toolbox.json is gone can still be finished.
static void uninstallResumesAfterToolbox() {
    freshSd({"release.zip"});
    CHECK(installZip("release.zip", "v1", nullptr).code == Code::Ok);
    failUnlink = [](const std::string& p) { return endsIn(p, "4200000000504C4B/toolbox.json"); };
    CHECK(uninstall(true).code != Code::Ok);
    clearFaults();
    CHECK(!has(LedgerFile) && readState().present);  // the ledger goes first, toolbox.json last
    CHECK(uninstall(true).code == Code::Ok && !has(std::string(ContentsDir)));

    // Only an empty folder left (power loss after toolbox.json): it shows as removable and finishes.
    put(C + "flags/x", "");
    unlink(sdPath(C + "flags/x").c_str());
    CHECK(readState().leftover && uninstall(true).code == Code::Ok && !has(std::string(ContentsDir)));
    put(ConfigFile, "[ldn]\n");  // a kept config.ini is what removal leaves on purpose
    CHECK(!readState().leftover && uninstall(false).code == Code::NotOurs);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    fixtures = argv[1];
    renameFailureKeepsOldFiles();
    interruptedCommitIsRolledBack();
    unreadableConfigIsKept();
    configSwapFailureKeepsOld();
    failedInstallDoesNotEnable();
    badCrcIsRefused();
    migrationCollisionKeepsData();
    cleanupWaitsForTheSettings();
    uninstallFailureIsReported();
    failedCommitKeepsOlderBuild();
    deepEntriesNeverBlock();
    manyEntriesStayReadable();
    caseAndSwapNamesAreRefused();
    carryChecksValues();
    uninstallResumesAfterToolbox();
    if (!failures) std::puts("All failure tests passed!");
    return failures ? 1 : 0;
}
