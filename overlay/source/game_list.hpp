#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace overlay {

constexpr uint32_t GameListPageSize = 8;
constexpr uint32_t MaxInstalledRecords = 128;

struct GameListPage {
    uint64_t ids[GameListPageSize]{};
    char names[GameListPageSize][96]{};
    uint32_t count = 0;
    bool has_next = false;
    bool readable = false;
    uint32_t total_matched = 0;
};

/** @brief Query installed application records from Switch OS. */
inline uint32_t GetInstalledApplicationIds(uint64_t* out_ids, uint32_t max_records) {
#ifdef __SWITCH__
    if (!out_ids || max_records == 0) return 0;
    Result rc = nsInitialize();
    if (R_FAILED(rc)) return 0;

    uint32_t total_count = 0;
    s32 offset = 0;
    constexpr s32 ChunkSize = 32;
    NsApplicationRecord chunk[ChunkSize];

    while (total_count < max_records) {
        s32 entry_count = 0;
        s32 to_read = static_cast<s32>(max_records - total_count);
        if (to_read > ChunkSize) to_read = ChunkSize;

        rc = nsListApplicationRecord(chunk, to_read, offset, &entry_count);
        if (R_FAILED(rc) || entry_count <= 0) break;

        for (s32 i = 0; i < entry_count && total_count < max_records; ++i) {
            if (chunk[i].application_id != 0) {
                out_ids[total_count++] = chunk[i].application_id;
            }
        }
        offset += entry_count;
        if (entry_count < to_read) break;
    }

    nsExit();
    return total_count;
#else
    (void)out_ids;
    (void)max_records;
    return 0;
#endif
}

/** @brief Filter installed application IDs against gamelist.txt, returning one page slice. */
inline GameListPage GetInstalledSupportedGamesPage(const char* gamelist_path,
                                                  const uint64_t* installed_ids,
                                                  uint32_t installed_count,
                                                  uint32_t page_offset) {
    GameListPage page{};
    if (!installed_ids || installed_count == 0 || !gamelist_path) {
        return page;
    }

    uint64_t matched_ids[MaxInstalledRecords];
    uint32_t matched_count = 0;

    FILE* file = std::fopen(gamelist_path, "r");
    if (!file) return page;
    char io_buffer[512];
    std::setvbuf(file, io_buffer, _IOFBF, sizeof(io_buffer));
    char line[32];

    while (matched_count < MaxInstalledRecords && std::fgets(line, sizeof(line), file)) {
        size_t i = (line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) ? 2 : 0;
        uint64_t id = 0;
        uint32_t digits = 0;
        for (; line[i]; ++i) {
            const char c = line[i];
            if (c == '\r' || c == '\n') break;
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else { digits = 0; break; }
            if (++digits > 16) { digits = 0; break; }
            id = (id << 4) | digit;
        }
        if (!id || !digits) continue;

        for (uint32_t j = 0; j < installed_count; ++j) {
            if (installed_ids[j] == id) {
                bool already = false;
                for (uint32_t k = 0; k < matched_count; ++k) {
                    if (matched_ids[k] == id) { already = true; break; }
                }
                if (!already) {
                    matched_ids[matched_count++] = id;
                }
                break;
            }
        }
    }
    page.readable = !std::ferror(file);
    std::fclose(file);

    page.total_matched = matched_count;
    if (page_offset < matched_count) {
        for (uint32_t i = page_offset; i < matched_count && page.count < GameListPageSize; ++i) {
            page.ids[page.count++] = matched_ids[i];
        }
        page.has_next = (page_offset + page.count) < matched_count;
    }
    return page;
}

/** @brief Read only enough of the SD list to fill one page; retain no catalog cache. */
inline GameListPage ReadGameListPage(const char* path, uint32_t offset) {
    GameListPage page{};
    FILE* file = std::fopen(path, "r");
    if (!file) return page;
    char io_buffer[512];
    std::setvbuf(file, io_buffer, _IOFBF, sizeof(io_buffer));
    char line[32];
    uint32_t skipped = 0;
    while (std::fgets(line, sizeof(line), file)) {
        // Reject an overlong line rather than interpreting its truncated prefix.
        size_t length = 0;
        while (line[length]) ++length;
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int c;
            while ((c = std::fgetc(file)) != EOF && c != '\n') {}
            continue;
        }
        size_t i = 0;
        if (line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) i = 2;
        uint64_t id = 0;
        uint32_t digits = 0;
        for (; line[i]; ++i) {
            const char c = line[i];
            if (c == '\r' || c == '\n') break;
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else { digits = 0; break; }
            if (++digits > 16) { digits = 0; break; }
            id = (id << 4) | digit;
        }
        if (!id || !digits) continue;
        if (skipped < offset) { ++skipped; continue; }
        if (page.count == GameListPageSize) { page.has_next = true; break; }
        page.ids[page.count++] = id;
    }
    page.readable = !std::ferror(file);
    std::fclose(file);
    return page;
}

/** @brief Resolve just this page's names by streaming the SD metadata file once. */
inline void ReadGameNames(const char* path, GameListPage& page) {
    if (!page.count) return;
    const uint32_t count = page.count < GameListPageSize ? page.count : GameListPageSize;
    FILE* file = std::fopen(path, "r");
    if (!file) return;
    char io_buffer[512];
    std::setvbuf(file, io_buffer, _IOFBF, sizeof(io_buffer));
    char line[128];
    uint32_t found = 0;
    while (found < count && std::fgets(line, sizeof(line), file)) {
        char* name = std::strchr(line, '\t');
        if (!name || name - line != 16) continue;
        *name++ = '\0';
        char* end = nullptr;
        const uint64_t id = std::strtoull(line, &end, 16);
        if (*end != '\0') continue;
        name[std::strcspn(name, "\r\n")] = '\0';
        for (uint32_t i = 0; i < count; ++i) {
            if (page.ids[i] == id && !page.names[i][0] && name[0]) {
                std::strncpy(page.names[i], name, sizeof(page.names[i]) - 1);
                ++found;
            }
        }
    }
    std::fclose(file);
}

} // namespace overlay
