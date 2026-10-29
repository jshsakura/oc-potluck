// The older builds (ryu_ldn_nx) lived under ldn_mitm's title id, with their settings in config/ryu_ldn_nx/.
// Nothing of theirs is deleted on a guess: a file goes only when it is the old build's own binary, an
// identical copy of the new one, or settings whose keys are already in the new config.ini.
#include <algorithm>
#include <cstdio>
#include "install.hpp"

namespace inst {

namespace {
// The old build's own files: the release brings new ones, and its flag must not start the moved files.
constexpr const char* OldBuildFiles[] = {"exefs.nsp", "main.npdm", "toolbox.json", "flags/boot2.flag"};

enum class Step { Move, Delete };
struct Planned {
    std::string name;  // relative to the module folder
    Step step;
};

bool ownedByOldBuild(const std::string& name, const std::vector<std::string>& shipped) {
    const std::string path = std::string(ContentsDir) + name;
    if (keepIfPresent(path)) return false;  // settings are the user's, whatever the release carries
    for (const char* f : OldBuildFiles) if (name == f) return true;
    return std::find(shipped.begin(), shipped.end(), path) != shipped.end();
}

// What happens to each old file; LegacyConflict when one differs from the new folder's copy.
Code plan(const std::vector<std::string>& names, const std::vector<std::string>& shipped, std::vector<Planned>& out) {
    for (const auto& n : names) {
        if (ownedByOldBuild(n, shipped)) { out.push_back({n, Step::Delete}); continue; }
        const std::string to = std::string(ContentsDir) + n;
        if (!exists(to)) { out.push_back({n, Step::Move}); continue; }
        const Same same = sameContent(std::string(LegacyContentsDir) + n, to);
        if (same == Same::Error) return Code::Read;
        if (same == Same::No) return Code::LegacyConflict;
        out.push_back({n, Step::Delete});
    }
    return Code::Ok;
}

bool apply(const Planned& p) {
    const std::string from = std::string(LegacyContentsDir) + p.name, to = std::string(ContentsDir) + p.name;
    if (p.step == Step::Delete) return removeFile(from);
    return makeDirs(to) && rename(sdPath(from).c_str(), sdPath(to).c_str()) == 0;
}

// Carry over or delete file by file. The old build's boot flag goes first, so a cut-off move never leaves it
// starting half moved; the ownership marker goes last, so a retry still recognises what is left.
Code mergeInto(const std::vector<std::string>& shipped) {
    std::vector<std::string> names;
    std::vector<Planned> steps;
    if (!listFiles(LegacyContentsDir, names)) return Code::Read;
    const Code planned = plan(names, shipped, steps);
    if (planned != Code::Ok) return planned;
    std::stable_partition(steps.begin(), steps.end(), [](const Planned& p) { return p.name == BootFlag; });
    std::stable_partition(steps.begin(), steps.end(), [](const Planned& p) { return p.name != "toolbox.json"; });
    for (const auto& p : steps) if (!apply(p)) return Code::Migrate;
    return removeTree(LegacyContentsDir).ok ? Code::Ok : Code::Migrate;
}

std::string legacyConfig() { return std::string(LegacyConfigDir) + "config.ini"; }

// The old settings folder may go: its config.ini is gone already, or its keys are all in the new one.
bool settingsCarried() {
    const Text old = readText(legacyConfig());
    if (old.status == ReadStatus::Missing) return true;
    if (old.status == ReadStatus::Error) return false;
    const Text current = readText(ConfigFile);
    return current.status == ReadStatus::Ok && carriedOver(old.data, current.data);
}
} // namespace

Code migrateLegacy(const std::vector<std::string>& shipped) {
    const Text marker = readText(std::string(LegacyContentsDir) + "toolbox.json");
    if (marker.status == ReadStatus::Error) return Code::Read;
    if (!isLegacyToolbox(marker.data)) return Code::Ok;
    return mergeInto(shipped);
}

bool cleanupLegacy() {
    const bool potluck = isOwnToolbox(readText(std::string(ContentsDir) + "toolbox.json").data);
    if (potluck && exists(LegacyOverlayPath)) removeFile(LegacyOverlayPath);
    if (exists(trimSlash(LegacyConfigDir)) && settingsCarried()) removeTree(LegacyConfigDir);
    return !exists(LegacyOverlayPath) && !exists(trimSlash(LegacyConfigDir));
}

bool removeLegacySettings() { return removeTree(LegacyConfigDir).ok; }

bool removeLegacy() {
    const bool overlay = removeFile(LegacyOverlayPath);
    return removeTree(LegacyConfigDir).ok && overlay && !exists(trimSlash(LegacyConfigDir));
}

} // namespace inst
