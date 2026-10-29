#pragma once
// The games this module supports that are installed on this console, for viewing only: gamelist.txt and
// game_names.tsv are overwritten by every update, so nothing here edits them.
#include <cstdint>
#include <string>
#include <vector>

namespace inst {

constexpr const char* GameListFile = "atmosphere/contents/4200000000504C4B/gamelist.txt";
constexpr const char* GameNamesFile = "atmosphere/contents/4200000000504C4B/game_names.tsv";

struct Game {
    uint64_t id = 0;
    std::string name;   // the console's own name when it has one, else game_names.tsv's, else empty: show the id
    bool named = false; // the console's name has been looked up
};

/** The title id on a gamelist line: 16 hex digits, 0x optional, any case; 0 for blanks, # and ; lines and anything else. */
inline uint64_t gameIdOf(const std::string& line) {
    size_t a = 0, b = line.size();
    while (a < b && (line[a] == ' ' || line[a] == '\t')) ++a;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r' || line[b - 1] == '\n')) --b;
    if (a >= b || line[a] == '#' || line[a] == ';') return 0;
    if (b - a >= 2 && line[a] == '0' && (line[a + 1] == 'x' || line[a + 1] == 'X')) a += 2;
    if (b - a != 16) return 0;
    uint64_t id = 0;
    for (size_t i = a; i < b; ++i) {
        const char c = line[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        id = (id << 4) | uint64_t(d);
    }
    return id;
}

/** The installed applications that gamelist.txt names, in the list's order, each once, with their names from game_names.tsv. */
std::vector<Game> installedSupported(const std::vector<uint64_t>& installed);

} // namespace inst
