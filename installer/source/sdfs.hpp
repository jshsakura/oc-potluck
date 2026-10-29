#pragma once
// File primitives on the SD card. Every path is relative to the card root ("sdmc:/"), and every
// replacement of a file the user has goes through a ".bak" swap, so a failure keeps the old copy.
#include <string>
#include <vector>

namespace inst {

constexpr size_t MaxTextBytes = 64 * 1024;  // config.ini, ledgers, toolbox.json

std::string sdPath(const std::string& rel);
bool exists(const std::string& rel);
std::string trimSlash(const std::string& dir);
/** Make every folder above the last "/" of `rel`. */
bool makeDirs(const std::string& rel);

/** Missing is the only result that may be taken as "no file": an error or a file over the limit is never an empty file. */
enum class ReadStatus { Missing, Ok, Error };
struct Text {
    ReadStatus status = ReadStatus::Missing;
    std::string data;
};
Text readText(const std::string& rel, size_t limit = MaxTextBytes);

/** Write a new file in one go (no swap): for staging and ".part" files. */
bool writeNew(const std::string& rel, const std::string& text);
/** Remove a file; true when it is gone (it was never there counts). */
bool removeFile(const std::string& rel);

/** Put the finished file `fromRel` in place of `rel`. The old `rel` waits as rel.bak until the new one is
 *  there and comes back on any failure; with `keepBackup` the caller removes rel.bak itself later. */
bool swapIn(const std::string& fromRel, const std::string& rel, bool keepBackup = false);
/** Replace `rel` with `text` through rel.part and the swap above. */
bool writeSafe(const std::string& rel, const std::string& text);
/** After an interrupted swap: rel missing with rel.bak present brings rel.bak back; both present drops rel.bak. */
bool recoverFile(const std::string& rel);

enum class Same { Yes, No, Error };
Same sameContent(const std::string& a, const std::string& b);

/** Every file below the folder `dir`, relative to it ("flags/boot2.flag"). False when a folder cannot be read. */
bool listFiles(const std::string& dir, std::vector<std::string>& out);

struct Removal {
    int removed = 0;
    bool ok = true;  // every delete that was tried worked
};
/** Delete the folder `dir` and everything below it except the files in `keep` (full relative paths). */
Removal removeTree(const std::string& dir, const std::vector<std::string>& keep = {});

} // namespace inst
