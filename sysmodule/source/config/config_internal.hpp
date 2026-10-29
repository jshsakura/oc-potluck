#pragma once

/**
 * @file config_internal.hpp
 * @brief INI helpers shared by the loader (config.cpp) and the writer (config_text.cpp)
 *
 * Both sides must read a line the same way, or a save could change a key the
 * loader never sees. Not part of the public config API.
 */

#include "config.hpp"
#include <cstddef>

namespace potluck::config::detail {

/// Section a header line selects; Unknown for any other "[...]" line
enum class Section {
    None,
    Server,
    Network,
    Ldn,
    Debug,
    Unknown
};

const char* trim_start(const char* str);
void trim_end(char* str);
void safe_strcpy(char* dest, const char* src, size_t max_len);

/// Exact header match only: "[ldn] ; note" or "[ldn" is Unknown
Section parse_section(const char* line);

/// Store one key=value from `section` into config, as the loader does
void apply_key(Section section, const char* key, const char* value, Config& config);

} // namespace potluck::config::detail
