#include "sdfs.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace inst {

namespace {
constexpr int MaxDepth = 16;  // FAT has no links; this only stops a runaway walk
constexpr size_t Chunk = 4096;

bool isDir(const std::string& rel) {
    struct stat st;
    return stat(sdPath(rel).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
std::string backupOf(const std::string& rel) { return rel + ".bak"; }

bool listNames(const std::string& dir, std::vector<std::string>& names) {
    DIR* d = opendir(sdPath(dir).c_str());
    if (!d) return false;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    return true;
}

bool listInto(const std::string& dir, const std::string& prefix, int depth, std::vector<std::string>& out) {
    if (depth > MaxDepth) return false;
    std::vector<std::string> names;
    if (!listNames(dir, names)) return false;
    for (const auto& n : names) {
        const std::string child = dir + "/" + n;
        if (isDir(child)) {
            if (!listInto(child, prefix + n + "/", depth + 1, out)) return false;
        } else {
            out.push_back(prefix + n);
        }
    }
    return true;
}

// Returns how many kept files are left below `dir`, so a folder that must stay is not a failure.
int removeInto(const std::string& dir, const std::vector<std::string>& keep, int depth, Removal& r) {
    std::vector<std::string> names;
    if (depth > MaxDepth || !listNames(dir, names)) { r.ok = false; return 1; }
    int kept = 0;
    for (const auto& n : names) {
        const std::string child = dir + "/" + n;
        if (isDir(child)) { kept += removeInto(child, keep, depth + 1, r); continue; }
        if (std::find(keep.begin(), keep.end(), child) != keep.end()) { ++kept; continue; }
        if (unlink(sdPath(child).c_str()) == 0) ++r.removed;
        else { r.ok = false; ++kept; }
    }
    if (kept == 0 && rmdir(sdPath(dir).c_str()) != 0) { r.ok = false; return 1; }
    return kept;
}
} // namespace

std::string sdPath(const std::string& rel) { return "sdmc:/" + rel; }
bool exists(const std::string& rel) { return access(sdPath(rel).c_str(), F_OK) == 0; }
std::string trimSlash(const std::string& dir) { return !dir.empty() && dir.back() == '/' ? dir.substr(0, dir.size() - 1) : dir; }

bool makeDirs(const std::string& rel) {
    for (size_t pos = rel.find('/'); pos != std::string::npos; pos = rel.find('/', pos + 1)) {
        const std::string dir = sdPath(rel.substr(0, pos));
        if (mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    return true;
}

Text readText(const std::string& rel, size_t limit) {
    Text t;
    FILE* f = fopen(sdPath(rel).c_str(), "rb");
    if (!f) {
        t.status = errno == ENOENT ? ReadStatus::Missing : ReadStatus::Error;
        return t;
    }
    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0 && t.data.size() <= limit) t.data.append(buffer, n);
    const bool failed = ferror(f) != 0 || t.data.size() > limit;
    fclose(f);
    t.status = failed ? ReadStatus::Error : ReadStatus::Ok;
    if (failed) t.data.clear();
    return t;
}

bool writeNew(const std::string& rel, const std::string& text) {
    FILE* f = fopen(sdPath(rel).c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

bool removeFile(const std::string& rel) { return unlink(sdPath(rel).c_str()) == 0 || errno == ENOENT; }

bool swapIn(const std::string& fromRel, const std::string& rel, bool keepBackup) {
    const std::string from = sdPath(fromRel), dest = sdPath(rel), bak = sdPath(backupOf(rel));
    const bool had = exists(rel);
    if (had && (!removeFile(backupOf(rel)) || rename(dest.c_str(), bak.c_str()) != 0)) return false;
    if (rename(from.c_str(), dest.c_str()) != 0) {
        if (had) rename(bak.c_str(), dest.c_str());
        return false;
    }
    if (had && !keepBackup) removeFile(backupOf(rel));  // a leftover .bak is cleaned by recoverFile
    return true;
}

bool writeSafe(const std::string& rel, const std::string& text) {
    const std::string part = rel + ".part";
    if (!makeDirs(rel) || !writeNew(part, text) || !swapIn(part, rel)) {
        removeFile(part);
        return false;
    }
    return true;
}

bool recoverFile(const std::string& rel) {
    const std::string bak = backupOf(rel);
    if (!exists(bak)) return true;
    if (exists(rel)) return removeFile(bak);
    return rename(sdPath(bak).c_str(), sdPath(rel).c_str()) == 0;
}

Same sameContent(const std::string& a, const std::string& b) {
    FILE* fa = fopen(sdPath(a).c_str(), "rb");
    FILE* fb = fa ? fopen(sdPath(b).c_str(), "rb") : nullptr;
    Same result = fa && fb ? Same::Yes : Same::Error;
    char x[Chunk], y[Chunk];
    while (result == Same::Yes) {
        const size_t n = fread(x, 1, sizeof(x), fa), m = fread(y, 1, sizeof(y), fb);
        if (ferror(fa) || ferror(fb)) result = Same::Error;
        else if (n != m || std::memcmp(x, y, n) != 0) result = Same::No;
        else if (n == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return result;
}

bool listFiles(const std::string& dir, std::vector<std::string>& out) {
    return listInto(trimSlash(dir), "", 0, out);
}

Removal removeTree(const std::string& dir, const std::vector<std::string>& keep) {
    Removal r;
    if (!exists(trimSlash(dir))) return r;
    removeInto(trimSlash(dir), keep, 0, r);
    return r;
}

} // namespace inst
