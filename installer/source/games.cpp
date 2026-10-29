#include "games.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include "install.hpp"

namespace inst {

namespace {

// Read a file line by line without holding it whole: the list has thousands of lines.
template <typename F>
void eachLine(const std::string& rel, F&& each) {
    FILE* f = fopen(sdPath(rel).c_str(), "rb");
    if (!f) return;
    std::string line;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') { each(line); line.clear(); }
        else if (line.size() < 512) line.push_back(char(c));
    }
    if (!line.empty()) each(line);
    fclose(f);
}

} // namespace

std::vector<Game> installedSupported(const std::vector<uint64_t>& installed) {
    std::vector<Game> out;
    if (installed.empty()) return out;
    eachLine(GameListFile, [&](const std::string& line) {
        const uint64_t id = gameIdOf(line);
        if (!id || std::find(installed.begin(), installed.end(), id) == installed.end()) return;
        for (const auto& g : out) if (g.id == id) return;
        out.push_back({id, ""});
    });
    if (out.empty()) return out;
    std::map<uint64_t, std::string> names;
    eachLine(GameNamesFile, [&](const std::string& line) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) return;
        const uint64_t id = gameIdOf(line.substr(0, tab));
        if (!id) return;
        std::string name = line.substr(tab + 1);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        names[id] = name;
    });
    for (auto& g : out) {
        const auto found = names.find(g.id);
        if (found != names.end()) g.name = found->second;
    }
    return out;
}

} // namespace inst
