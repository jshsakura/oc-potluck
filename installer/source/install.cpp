// Install, update and removal. An install happens in three steps so a failure never leaves a broken or
// half-enabled module: every file is unpacked into a staging folder and checked (CRC, size), the staged
// files then replace the live ones under a journal that can roll all of them back, and only then are the
// boot flags changed: ldn_mitm's off first (both on ldn:u crash at boot), PotLuck's on last.
#include <algorithm>
#include <cstdio>
#include <minizip/unzip.h>
#include <vector>
#include "install.hpp"

namespace inst {

namespace {
constexpr const char* StageDir = "config/potluck_installer/stage/";
constexpr const char* JournalFile = "config/potluck_installer/commit.txt";
constexpr const char* JournalHead = "potluck_commit 1";
constexpr size_t CopyChunk = 64 * 1024;
constexpr size_t MaxEntryName = 256;

std::string ownFlag() { return std::string(ContentsDir) + BootFlag; }
std::string mitmFlag() { return std::string(LegacyContentsDir) + BootFlag; }
std::string ownToolbox() { return std::string(ContentsDir) + "toolbox.json"; }
std::string staged(const std::string& rel) { return std::string(StageDir) + rel; }

bool setFlag(const std::string& flag, bool on) {
    if (!on) return removeFile(flag);
    return makeDirs(flag) && (exists(flag) || writeNew(flag, ""));
}

/** ldn_mitm, or any other module under its id that is not the older PotLuck build. */
bool mitmInstalled() {
    if (!exists(std::string(LegacyContentsDir) + "exefs.nsp")) return false;
    return !isLegacyToolbox(readText(std::string(LegacyContentsDir) + "toolbox.json").data);
}

// ---- The journal: which live files a commit replaces, so an interrupted one can be rolled back ----

struct Entry {
    bool hadOld;  // the file existed and waits as .bak until the commit is done
    std::string rel;
};
struct Journal {
    bool flagWasOn = false;
    std::vector<Entry> entries;
};

std::string journalText(const Journal& j) {
    std::string out = std::string(JournalHead) + "\n" + (j.flagWasOn ? "1" : "0") + "\n";
    for (const auto& e : j.entries) out += std::string(e.hadOld ? "B " : "N ") + e.rel + "\n";
    return out;
}
bool parseJournal(const std::string& text, Journal& j) {
    size_t pos = 0;
    int line = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(pos, end - pos);
        pos = end + 1;
        if (line == 0 && row != JournalHead) return false;
        if (line == 1) j.flagWasOn = row == "1";
        if (line >= 2) {
            const std::string rel = row.size() > 2 ? row.substr(2) : "";
            const bool known = allowedTarget(rel) || rel == LedgerFile;  // never roll back a path we do not own
            if (!known || (row[0] != 'B' && row[0] != 'N')) return false;
            j.entries.push_back({row[0] == 'B', rel});
        }
        ++line;
    }
    return line >= 2;
}

bool rollback(const Journal& j) {
    bool ok = true;
    for (auto e = j.entries.rbegin(); e != j.entries.rend(); ++e) {
        const std::string bak = e->rel + ".bak";
        if (!e->hadOld) ok = removeFile(e->rel) && ok;
        else if (exists(bak)) ok = removeFile(e->rel) && rename(sdPath(bak).c_str(), sdPath(e->rel).c_str()) == 0 && ok;
    }
    if (j.flagWasOn) ok = setFlag(ownFlag(), true) && ok;
    return ok && removeFile(JournalFile);
}

/** Put every staged file in place; on any failure the old ones come back. PotLuck's flag is off meanwhile. */
Code commit(const std::vector<std::string>& targets) {
    Journal j;
    j.flagWasOn = exists(ownFlag());
    for (const auto& rel : targets) {
        if (!recoverFile(rel)) return Code::Write;  // no stale .bak may stand in for the old file
        j.entries.push_back({exists(rel), rel});
    }
    if (!writeSafe(JournalFile, journalText(j))) return Code::Write;
    bool ok = setFlag(ownFlag(), false);
    for (size_t i = 0; ok && i < j.entries.size(); ++i)
        ok = makeDirs(j.entries[i].rel) && swapIn(staged(j.entries[i].rel), j.entries[i].rel, true);
    if (ok && removeFile(JournalFile)) {  // the install is complete from here
        for (const auto& e : j.entries) if (e.hadOld) removeFile(e.rel + ".bak");
        return Code::Ok;
    }
    rollback(j);
    return Code::Write;
}

// ---- Staging: unpack and check every file before anything live changes ----

Code copyEntry(unzFile zf, FILE* out, const Limits& limits, unsigned long& total) {
    std::vector<char> buffer(CopyChunk);
    unsigned long size = 0;
    for (;;) {
        const int n = unzReadCurrentFile(zf, buffer.data(), unsigned(buffer.size()));
        if (n < 0) return Code::Corrupt;
        if (n == 0) return Code::Ok;
        size += unsigned(n);
        total += unsigned(n);
        if (size > limits.entry || total > limits.total) return Code::TooLarge;
        if (fwrite(buffer.data(), 1, size_t(n), out) != size_t(n)) return Code::Write;
    }
}

Code stageEntry(unzFile zf, const unz_file_info& fi, const std::string& rel, const Limits& limits, unsigned long& total) {
    if (fi.uncompressed_size > limits.entry || total + fi.uncompressed_size > limits.total) return Code::TooLarge;
    const std::string path = staged(rel);
    if (!makeDirs(path)) return Code::Write;
    if (unzOpenCurrentFile(zf) != UNZ_OK) return Code::Corrupt;
    FILE* out = fopen(sdPath(path).c_str(), "wb");
    Code code = out ? copyEntry(zf, out, limits, total) : Code::Write;
    if (out && fclose(out) != 0 && code == Code::Ok) code = Code::Write;
    const int closed = unzCloseCurrentFile(zf);  // UNZ_CRCERROR when the data does not match its CRC
    if (code == Code::Ok && closed != UNZ_OK) code = Code::Corrupt;
    return code;
}

bool hasModuleFiles(unzFile zf) {
    bool exefs = false, npdm = false, toolbox = false;
    char name[MaxEntryName];
    for (int rc = unzGoToFirstFile(zf); rc == UNZ_OK; rc = unzGoToNextFile(zf)) {
        unz_file_info fi;
        if (unzGetCurrentFileInfo(zf, &fi, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) continue;
        const std::string n = name;
        exefs |= n == std::string(ContentsDir) + "exefs.nsp";
        npdm |= n == std::string(ContentsDir) + "main.npdm";
        toolbox |= n == ownToolbox();
    }
    return exefs && npdm && toolbox;
}

Code stageAll(unzFile zf, const Progress& progress, const Limits& limits, std::vector<std::string>& targets, Outcome& out) {
    unz_global_info gi;
    if (unzGetGlobalInfo(zf, &gi) != UNZ_OK) return Code::Corrupt;
    if (gi.number_entry > MaxEntries) return Code::TooLarge;
    unsigned long total = 0;
    int index = 0, rc = UNZ_OK;
    char name[MaxEntryName];
    for (rc = unzGoToFirstFile(zf); rc == UNZ_OK; rc = unzGoToNextFile(zf), ++index) {
        if (progress) progress(index, int(gi.number_entry));
        unz_file_info fi;
        if (unzGetCurrentFileInfo(zf, &fi, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) return Code::Corrupt;
        const std::string rel = name;
        if (fi.size_filename >= sizeof(name) || !extractable(rel)) { ++out.skipped; continue; }
        if (keepIfPresent(rel) && exists(rel)) { ++out.kept; continue; }
        const auto sameName = [&](const std::string& t) { return lower(t) == lower(rel); };  // FAT ignores case
        if (std::any_of(targets.begin(), targets.end(), sameName)) return Code::Corrupt;     // a name twice
        const Code staged = stageEntry(zf, fi, rel, limits, total);
        if (staged != Code::Ok) return staged;
        targets.push_back(rel);
    }
    if (rc != UNZ_END_OF_LIST_OF_FILE) return Code::Corrupt;
    if (progress) progress(int(gi.number_entry), int(gi.number_entry));
    return Code::Ok;
}

// ---- After staging: replace the files, move the older build, then the flags ----

Code commitRelease(const std::string& tag, const std::vector<std::string>& unpacked, Outcome& out) {
    const bool wasInstalled = readState().present;
    const Text oldLedger = readText(LedgerFile);
    if (oldLedger.status == ReadStatus::Error) return Code::Read;
    Ledger previous;
    const bool removedBefore = wasInstalled && parseLedger(oldLedger.data, previous) && previous.mitmFlagRemoved;

    const bool mitmOn = mitmInstalled() && exists(mitmFlag());
    Ledger ledger;
    ledger.tag = tag;
    ledger.mitmFlagRemoved = removedBefore || mitmOn;  // written before the flag goes, so removal can put it back
    if (!makeDirs(staged(LedgerFile)) || !writeNew(staged(LedgerFile), ledgerText(ledger))) return Code::Write;
    std::vector<std::string> targets = unpacked;
    targets.push_back(LedgerFile);
    const Code committed = commit(targets);
    if (committed != Code::Ok) return committed;
    out.written = int(unpacked.size());

    // The older build moves only now, with every new file in place: a failed commit leaves it running as it was.
    // When the move fails, PotLuck stays off so the two never start together.
    const Code moved = migrateLegacy(unpacked);
    if (moved != Code::Ok) return moved;

    if (mitmOn) {
        if (!setFlag(mitmFlag(), false)) return Code::Write;  // PotLuck stays off: never both at boot
        out.mitmDisabled = true;
    }
    if (!setFlag(ownFlag(), true)) return Code::Write;
    out.legacyPending = !cleanupLegacy();
    return Code::Ok;
}

// ---- Removal ----

/** PotLuck's folder is there with no file in it at all: what a removal cut off at its very end leaves. */
bool onlyEmptyFolders() {
    std::vector<std::string> files;
    return exists(trimSlash(ContentsDir)) && listFiles(ContentsDir, files) && files.empty();
}

/** Everything of PotLuck except toolbox.json (and config.ini when kept); false when anything is left. */
bool removeModuleFiles(bool withSettings, Outcome& out) {
    std::vector<std::string> keep{ownToolbox()};
    if (!withSettings) keep.push_back(ConfigFile);
    const Removal r = removeTree(ContentsDir, keep);
    out.removed += r.removed;
    if (exists(OverlayPath)) {
        if (!removeFile(OverlayPath)) return false;
        ++out.removed;
    }
    std::vector<std::string> left;
    if (!r.ok) return false;
    if (!exists(trimSlash(ContentsDir))) return true;  // nothing was kept, so the folder went too
    if (!listFiles(ContentsDir, left)) return false;
    for (const auto& name : left)
        if (std::find(keep.begin(), keep.end(), std::string(ContentsDir) + name) == keep.end()) return false;
    return true;
}
} // namespace

State readState() {
    State s;
    s.present = isOwnToolbox(readText(ownToolbox()).data);
    s.enabled = s.present && exists(ownFlag());
    s.legacyOurs = isLegacyToolbox(readText(std::string(LegacyContentsDir) + "toolbox.json").data);
    s.mitmPresent = mitmInstalled();
    s.mitmFlag = s.mitmPresent && exists(mitmFlag());
    s.legacyLeft = exists(LegacyOverlayPath) || exists(trimSlash(LegacyConfigDir));
    s.leftover = !s.present && !exists(ownToolbox()) && (exists(LedgerFile) || onlyEmptyFolders());
    Ledger l;
    if (s.present && parseLedger(readText(LedgerFile).data, l)) {
        s.tag = l.tag;
        s.mitmDisabledByUs = l.mitmFlagRemoved;
    }
    return s;
}

bool recoverInterrupted() {
    if (!recoverFile(JournalFile)) return false;
    const Text journal = readText(JournalFile);
    if (journal.status == ReadStatus::Error) return false;
    if (journal.status == ReadStatus::Ok) {
        Journal j;
        if (!parseJournal(journal.data, j) || !rollback(j)) return false;
    }
    const bool settings = recoverFile(ConfigFile) && removeFile(std::string(ConfigFile) + ".part");
    const bool ledger = recoverFile(LedgerFile);
    removeTree(StageDir);  // ours alone and never committed from; a leftover is overwritten, so it never blocks
    return settings && ledger;
}

Outcome installZip(const std::string& zipRel, const std::string& tag, const Progress& progress, const Limits& limits) {
    Outcome out;
    if (!recoverInterrupted()) { out.code = Code::Write; return out; }
    unzFile zf = unzOpen(sdPath(zipRel).c_str());
    if (!zf) { out.code = Code::ZipOpen; return out; }
    std::vector<std::string> unpacked;
    out.code = hasModuleFiles(zf) ? stageAll(zf, progress, limits, unpacked, out) : Code::NotRelease;
    unzClose(zf);
    if (out.code == Code::Ok) out.code = commitRelease(tag, unpacked, out);
    removeTree(StageDir);
    return out;
}

Outcome uninstall(bool withSettings) {
    Outcome out;
    if (!recoverInterrupted()) { out.code = Code::Write; return out; }
    const State state = readState();
    if (!state.present && !state.leftover) { out.code = Code::NotOurs; return out; }
    const Text ledgerText = readText(LedgerFile);
    if (ledgerText.status == ReadStatus::Error) { out.code = Code::Read; return out; }
    Ledger ledger;
    const bool restoreMitm = parseLedger(ledgerText.data, ledger) && ledger.mitmFlagRemoved && mitmInstalled();

    // PotLuck's flag first, so it cannot start half removed; then the ledger once ldn_mitm's flag is back,
    // and toolbox.json last, so a retry still knows the install.
    if (!setFlag(ownFlag(), false)) { out.code = Code::Write; return out; }
    if (!removeModuleFiles(withSettings, out)) { out.code = Code::Remove; return out; }
    if (restoreMitm) {
        if (!setFlag(mitmFlag(), true)) { out.code = Code::Write; return out; }
        out.mitmRestored = true;
    }
    if (!removeFile(LedgerFile) || !removeFile(ownToolbox())) { out.code = Code::Remove; return out; }
    ++out.removed;
    if (!removeTree(LogDir).ok) out.code = Code::Remove;
    if (!removeTree(ContentsDir, withSettings ? std::vector<std::string>{} : std::vector<std::string>{ConfigFile}).ok) out.code = Code::Remove;
    return out;
}

} // namespace inst
