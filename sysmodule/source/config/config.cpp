/**
 * @file config.cpp
 * @brief Configuration Manager Implementation
 *
 * On Nintendo Switch (Atmosphere), uses ams::fs API for safe SD card access
 * at boot. The standard library fopen/fclose causes kernel panic (DABRT 0x101)
 * when called before the filesystem is fully ready.
 *
 * For testing on PC, uses standard C file I/O.
 */

#include "config.hpp"
#include "config_internal.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <strings.h>

#ifdef __SWITCH__
#include <stratosphere.hpp>
#else
#include <sys/stat.h>
#endif

namespace potluck::config {

// ============================================================================
// Helper Functions (shared with config_text.cpp through config_internal.hpp)
// ============================================================================

namespace detail {

/**
 * @brief Trim leading whitespace from string
 */
const char* trim_start(const char* str) {
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    return str;
}

/**
 * @brief Trim trailing whitespace from string (modifies in place)
 */
void trim_end(char* str) {
    size_t len = std::strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }
}

/**
 * @brief Copy string with length limit
 */
void safe_strcpy(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (i < max_len && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * @brief Parse boolean value (0/1, true/false, yes/no)
 */
bool parse_bool(const char* value) {
    if (value[0] == '0' || value[0] == 'f' || value[0] == 'F' ||
        value[0] == 'n' || value[0] == 'N') {
        return false;
    }
    return true;  // Default to true for any non-false value
}

/**
 * @brief Parse unsigned integer
 */
uint32_t parse_uint32(const char* value) {
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

/**
 * @brief Parse a TCP port: 1 to 65535 in plain digits, else 0
 *
 * 0 marks the value invalid so validate_server_config() falls back to the
 * default instead of connecting to a truncated port ("70000" is not 4464).
 */
uint16_t parse_port(const char* value) {
    uint32_t port = 0;
    if (value[0] == '\0') return 0;
    for (const char* c = value; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') return 0;
        port = port * 10 + static_cast<uint32_t>(*c - '0');
        if (port > MAX_PORT) return 0;
    }
    return static_cast<uint16_t>(port);
}

/** @brief True for one hostname label: 1 to 63 of [A-Za-z0-9-], no edge hyphen */
bool is_valid_host_label(const char* label, size_t len) {
    if (len == 0 || len > MAX_HOST_LABEL_LENGTH) return false;
    if (label[0] == '-' || label[len - 1] == '-') return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = label[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

/** @brief True when every label is digits only (the string is meant as IPv4) */
bool looks_numeric(const char* host) {
    for (const char* c = host; *c != '\0'; ++c) {
        if (*c != '.' && (*c < '0' || *c > '9')) return false;
    }
    return true;
}

/** @brief True for a dotted-quad IPv4 address: four 0 to 255 parts */
bool is_valid_ipv4(const char* host) {
    int parts = 0;
    const char* c = host;
    while (true) {
        uint32_t value = 0;
        int digits = 0;
        while (*c >= '0' && *c <= '9') {
            value = value * 10 + static_cast<uint32_t>(*c - '0');
            if (++digits > 3 || value > 255) return false;
            ++c;
        }
        if (digits == 0) return false;
        ++parts;
        if (*c == '\0') return parts == 4;
        if (*c != '.' || parts == 4) return false;
        ++c;
    }
}


/**
 * @brief Identify section from header line
 */
Section parse_section(const char* line) {
    if (std::strcmp(line, "[server]") == 0) return Section::Server;
    if (std::strcmp(line, "[network]") == 0) return Section::Network;
    if (std::strcmp(line, "[ldn]") == 0) return Section::Ldn;
    if (std::strcmp(line, "[debug]") == 0) return Section::Debug;
    if (line[0] == '[') return Section::Unknown;
    return Section::None;
}

/**
 * @brief Process a key=value line for server section
 */
void process_server_key(const char* key, const char* value, ServerConfig& config) {
    if (std::strcmp(key, "host") == 0) {
        // An over-long host is rejected rather than silently cut short
        if (std::strlen(value) > MAX_HOST_LENGTH) {
            config.host[0] = '\0';
        } else {
            safe_strcpy(config.host, value, MAX_HOST_LENGTH);
        }
    } else if (std::strcmp(key, "port") == 0) {
        config.port = parse_port(value);
    }
}

/**
 * @brief Process a key=value line for network section
 */
void process_network_key(const char* key, const char* value, NetworkConfig& config) {
    if (std::strcmp(key, "connect_timeout") == 0) {
        config.connect_timeout_ms = parse_uint32(value);
    } else if (std::strcmp(key, "reconnect_delay") == 0) {
        config.reconnect_delay_ms = parse_uint32(value);
    } else if (std::strcmp(key, "max_reconnect_attempts") == 0) {
        config.max_reconnect_attempts = parse_uint32(value);
    }
}

/**
 * @brief Process a key=value line for ldn section
 */
void process_ldn_key(const char* key, const char* value, LdnConfig& config) {
    if (std::strcmp(key, "mode") == 0) {
        // Unknown words keep the current value, as a bad port keeps the default
        if (strcasecmp(value, "local") == 0) config.mode = LdnMode::Local;
        else if (strcasecmp(value, "remote") == 0) config.mode = LdnMode::Remote;
        else if (strcasecmp(value, "off") == 0) config.mode = LdnMode::Off;
    } else if (std::strcmp(key, "passphrase") == 0) {
        safe_strcpy(config.passphrase, value, MAX_PASSPHRASE_LENGTH);
    } else if (std::strcmp(key, "disable_p2p") == 0) {
        config.disable_p2p = parse_bool(value);
    }
}

/**
 * @brief Process a key=value line for debug section
 */
void process_debug_key(const char* key, const char* value, DebugConfig& config) {
    if (std::strcmp(key, "enabled") == 0) {
        config.enabled = parse_bool(value);
    } else if (std::strcmp(key, "level") == 0) {
        config.level = parse_uint32(value);
    } else if (std::strcmp(key, "log_to_file") == 0) {
        config.log_to_file = parse_bool(value);
    }
}

#ifdef __SWITCH__
/**
 * @brief Parse file content line by line (for ams::fs buffer-based reading)
 */
/// @gdb{tag="CONFIG:PARSE", msg="Parsing config content"}
void parse_config_content(const char* content, size_t size, Config& config) {
    char line[512];
    Section current_section = Section::None;
    size_t line_pos = 0;

    size_t i = 0;
    while (i <= size) {
        // End of line or end of content
        if (i == size || content[i] == '\n' || content[i] == '\r') {
            line[line_pos] = '\0';

            if (line_pos > 0) {
                // Remove trailing whitespace/newlines
                trim_end(line);

                // Skip empty lines
                const char* trimmed = trim_start(line);
                if (trimmed[0] != '\0') {
                    // Skip comments
                    if (trimmed[0] != ';' && trimmed[0] != '#') {
                        // Check for section header
                        Section new_section = parse_section(trimmed);
                        if (new_section != Section::None) {
                            current_section = new_section;
                        } else if (current_section != Section::None &&
                                   current_section != Section::Unknown) {
                            // Parse key=value
                            char* eq_pos = std::strchr(line, '=');
                            if (eq_pos) {
                                *eq_pos = '\0';
                                char* key = line;
                                char* value = eq_pos + 1;

                                // Trim key and value
                                const char* trimmed_key = trim_start(key);
                                trim_end(key);

                                const char* trimmed_value = trim_start(value);
                                trim_end(value);

                                // Copy trimmed key
                                char key_buf[64];
                                safe_strcpy(key_buf, trimmed_key, sizeof(key_buf) - 1);
                                trim_end(key_buf);

                                apply_key(current_section, key_buf, trimmed_value, config);
                            }
                        }
                    }
                }
            }

            line_pos = 0;
            // Skip \r if followed by \n
            if (i < size && content[i] == '\r' && i + 1 < size && content[i + 1] == '\n') {
                i++;
            }
        } else if (line_pos < sizeof(line) - 1) {
            line[line_pos++] = content[i];
        }
        i++;
    }
}

#endif // __SWITCH__

/// Feed one key the way the loader does
void apply_key(Section section, const char* key, const char* value, Config& config) {
    switch (section) {
        case Section::Server:  process_server_key(key, value, config.server); break;
        case Section::Network: process_network_key(key, value, config.network); break;
        case Section::Ldn:     process_ldn_key(key, value, config.ldn); break;
        case Section::Debug:   process_debug_key(key, value, config.debug); break;
        default: break;
    }
}

} // namespace detail

using namespace detail;


bool is_valid_server_host(const char* host) {
    const size_t len = std::strlen(host);
    if (len == 0 || len > MAX_HOST_LENGTH) return false;
    if (looks_numeric(host)) return is_valid_ipv4(host);

    const char* label = host;
    for (const char* c = host;; ++c) {
        if (*c != '.' && *c != '\0') continue;
        if (!is_valid_host_label(label, static_cast<size_t>(c - label))) return false;
        if (*c == '\0') return true;
        label = c + 1;
    }
}

LdnMode effective_ldn_mode(const Config& config) {
    if (config.ldn.mode == LdnMode::Remote && config.server.host[0] == '\0') return LdnMode::Local;
    return config.ldn.mode;
}

ServerCheck validate_server_config(ServerConfig& server) {
    ServerCheck check{};
    // An empty host is "no server", not an invalid one
    if (server.host[0] != '\0' && !is_valid_server_host(server.host)) {
        safe_strcpy(server.host, DEFAULT_HOST, MAX_HOST_LENGTH);
        check.host_reset = true;
    }
    if (server.port == 0) {
        server.port = DEFAULT_PORT;
        check.port_reset = true;
    }
    return check;
}

// ============================================================================
// Public Functions
// ============================================================================

Config get_default_config() {
    Config config{};

    // Server defaults
    safe_strcpy(config.server.host, DEFAULT_HOST, MAX_HOST_LENGTH);
    config.server.port = DEFAULT_PORT;
    config.server.use_tls = DEFAULT_USE_TLS;

    // Network defaults
    config.network.connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
    config.network.ping_interval_ms = DEFAULT_PING_INTERVAL_MS;
    config.network.reconnect_delay_ms = DEFAULT_RECONNECT_DELAY_MS;
    config.network.max_reconnect_attempts = DEFAULT_MAX_RECONNECT_ATTEMPTS;

    // LDN defaults
    config.ldn.mode = DEFAULT_LDN_MODE;
    config.ldn.passphrase[0] = '\0';
    config.ldn.interface_name[0] = '\0';
    config.ldn.disable_p2p = DEFAULT_DISABLE_P2P;

    // Debug defaults
    config.debug.enabled = DEFAULT_DEBUG_ENABLED;
    config.debug.level = DEFAULT_DEBUG_LEVEL;
    config.debug.log_to_file = DEFAULT_LOG_TO_FILE;

    return config;
}

#ifdef __SWITCH__
// =============================================================================
// Nintendo Switch / Atmosphere Implementation
// Uses ams::fs API to avoid kernel panic at boot
// =============================================================================

namespace {

/// Largest config.ini PotLuck reads or rewrites
constexpr s64 MAX_CONFIG_FILE_SIZE = 65536;
/// Room the in-place update may add on top of the existing file
constexpr size_t CONFIG_UPDATE_SLACK = 2048;

/// path + suffix into out; false when it does not fit
bool sibling_path(char (&out)[260], const char* path, const char* suffix) {
    return std::snprintf(out, sizeof(out), "%s%s", path, suffix) < static_cast<int>(sizeof(out));
}

enum class EntryState { Missing, Present, Unknown };

/// Whether something is at path; Unknown when the SD card did not say
EntryState entry_state(const char* path) {
    ams::fs::DirectoryEntryType entry_type;
    const ams::Result rc = ams::fs::GetEntryType(&entry_type, path);
    if (R_SUCCEEDED(rc)) return EntryState::Present;
    return ams::fs::ResultPathNotFound::Includes(rc) ? EntryState::Missing : EntryState::Unknown;
}

/**
 * @brief Put back the backup a save left behind when its final rename failed
 *
 * write_config_text() moves the old file to path.bak before moving the new
 * one in. If the file is missing but the backup is there, that swap was cut
 * short and the backup is the last good config.
 *
 * @return Success when path is present, or truly absent with no backup;
 *         IoError when the backup could not be put back or either state is
 *         unknown. Callers must stop then: "missing" would let them write
 *         defaults over a config that still exists as path.bak.
 */
ConfigResult recover_config_file(const char* path) {
    char backup_path[260];
    if (!sibling_path(backup_path, path, ".bak")) return ConfigResult::IoError;
    switch (entry_state(path)) {
        case EntryState::Present: return ConfigResult::Success;
        case EntryState::Unknown: return ConfigResult::IoError;
        case EntryState::Missing: break;
    }
    switch (entry_state(backup_path)) {
        case EntryState::Missing: return ConfigResult::Success;
        case EntryState::Unknown: return ConfigResult::IoError;
        case EntryState::Present: break;
    }
    return R_SUCCEEDED(ams::fs::RenameFile(backup_path, path)) ? ConfigResult::Success : ConfigResult::IoError;
}

/**
 * @brief Read the whole file into a new[] buffer (NUL-terminated)
 *
 * FileNotFound for a missing or empty file; the caller deletes *out_text.
 */
ConfigResult read_config_text(const char* path, char** out_text, size_t* out_length) {
    *out_text = nullptr;
    *out_length = 0;
    if (recover_config_file(path) != ConfigResult::Success) {
        return ConfigResult::IoError;
    }

    ams::fs::DirectoryEntryType entry_type;
    const ams::Result entry_result = ams::fs::GetEntryType(&entry_type, path);
    if (ams::fs::ResultPathNotFound::Includes(entry_result)) {
        return ConfigResult::FileNotFound;
    }
    if (R_FAILED(entry_result) || entry_type != ams::fs::DirectoryEntryType_File) {
        return ConfigResult::IoError;  // something is there that we cannot read as a file
    }

    ams::fs::FileHandle file;
    if (R_FAILED(ams::fs::OpenFile(&file, path, ams::fs::OpenMode_Read))) {
        return ConfigResult::IoError;
    }

    s64 file_size = 0;
    if (R_FAILED(ams::fs::GetFileSize(&file_size, file))) {
        ams::fs::CloseFile(file);
        return ConfigResult::IoError;
    }
    if (file_size <= 0 || file_size > MAX_CONFIG_FILE_SIZE) {
        ams::fs::CloseFile(file);
        return file_size == 0 ? ConfigResult::FileNotFound : ConfigResult::ParseError;
    }

    char* text = new (std::nothrow) char[file_size + 1];
    if (!text) {
        ams::fs::CloseFile(file);
        return ConfigResult::IoError;
    }

    size_t bytes_read = 0;
    const ams::Result read_result = ams::fs::ReadFile(&bytes_read, file, 0, text, static_cast<size_t>(file_size));
    ams::fs::CloseFile(file);
    if (R_FAILED(read_result) || bytes_read != static_cast<size_t>(file_size)) {
        delete[] text;
        return ConfigResult::IoError;
    }

    text[bytes_read] = '\0';
    *out_text = text;
    *out_length = bytes_read;
    return ConfigResult::Success;
}

/**
 * @brief Replace the file at path with `size` bytes of `content`
 *
 * FAT has no atomic replace, so the old file is never deleted before the new
 * one is in place: the new text is flushed to path.tmp, the old file moves to
 * path.bak, and path.tmp moves to path. A failed last step moves the backup
 * back, and recover_config_file() finishes that rollback on the next load if
 * even that failed.
 */
ConfigResult write_config_text(const char* path, const char* content, size_t size) {
    char dir_path[256];
    safe_strcpy(dir_path, path, sizeof(dir_path) - 1);
    if (char* last_slash = std::strrchr(dir_path, '/')) {
        *last_slash = '\0';
        static_cast<void>(ams::fs::EnsureDirectory(dir_path));  // CreateFile reports a missing directory
    }

    char temp_path[260];
    char backup_path[260];
    if (!sibling_path(temp_path, path, ".tmp") || !sibling_path(backup_path, path, ".bak")) {
        return ConfigResult::IoError;
    }
    if (recover_config_file(path) != ConfigResult::Success) {
        return ConfigResult::IoError;  // the last good config may still be path.bak
    }
    static_cast<void>(ams::fs::DeleteFile(temp_path));  // a leftover from an interrupted save
    if (R_FAILED(ams::fs::CreateFile(temp_path, size))) {
        return ConfigResult::IoError;
    }

    ams::fs::FileHandle file;
    if (R_FAILED(ams::fs::OpenFile(&file, temp_path, ams::fs::OpenMode_Write))) {
        static_cast<void>(ams::fs::DeleteFile(temp_path));  // best-effort cleanup
        return ConfigResult::IoError;
    }
    const ams::Result write_result = ams::fs::WriteFile(file, 0, content, size, ams::fs::WriteOption::Flush);
    ams::fs::CloseFile(file);
    if (R_FAILED(write_result)) {
        static_cast<void>(ams::fs::DeleteFile(temp_path));  // best-effort cleanup
        return ConfigResult::IoError;
    }

    const EntryState current = entry_state(path);
    if (current == EntryState::Unknown) {
        static_cast<void>(ams::fs::DeleteFile(temp_path));  // the old file is untouched
        return ConfigResult::IoError;
    }
    const bool had_file = current == EntryState::Present;
    if (had_file) {
        static_cast<void>(ams::fs::DeleteFile(backup_path));  // stale: the live file exists
        if (R_FAILED(ams::fs::RenameFile(path, backup_path))) {
            static_cast<void>(ams::fs::DeleteFile(temp_path));  // the old file is untouched
            return ConfigResult::IoError;
        }
    }
    if (R_FAILED(ams::fs::RenameFile(temp_path, path))) {
        if (had_file) static_cast<void>(ams::fs::RenameFile(backup_path, path));  // else recovered on next load
        static_cast<void>(ams::fs::DeleteFile(temp_path));  // best-effort cleanup
        return ConfigResult::IoError;
    }
    if (had_file) static_cast<void>(ams::fs::DeleteFile(backup_path));  // best-effort cleanup
    return ConfigResult::Success;
}

} // anonymous namespace

ConfigResult load_config(const char* path, Config& config) {
    char* content = nullptr;
    size_t length = 0;
    const ConfigResult result = read_config_text(path, &content, &length);
    if (result != ConfigResult::Success) {
        return result;
    }
    parse_config_content(content, length, config);
    delete[] content;
    return ConfigResult::Success;
}

ConfigResult save_config(const char* path, const Config& config) {
    char* existing = nullptr;
    size_t existing_length = 0;
    const ConfigResult read_result = read_config_text(path, &existing, &existing_length);
    if (read_result != ConfigResult::Success && read_result != ConfigResult::FileNotFound) {
        return read_result;  // never replace a file we could not read
    }

    const size_t buffer_size = existing_length + CONFIG_UPDATE_SLACK;
    char* buffer = new (std::nothrow) char[buffer_size];
    if (!buffer) {
        delete[] existing;
        return ConfigResult::IoError;
    }

    const size_t size = existing != nullptr
                            ? update_config_text(existing, existing_length, config, buffer, buffer_size)
                            : format_config_content(buffer, buffer_size, config);
    delete[] existing;

    const ConfigResult result = size == 0 ? ConfigResult::IoError : write_config_text(path, buffer, size);
    delete[] buffer;
    return result;
}

ConfigResult ensure_config_exists(const char* path) {
    // A cut-short save is not a missing config; never write defaults over it
    if (recover_config_file(path) != ConfigResult::Success) {
        return ConfigResult::IoError;
    }

    // Check if file exists using ams::fs
    ams::fs::DirectoryEntryType entry_type;
    if (R_SUCCEEDED(ams::fs::GetEntryType(&entry_type, path))) {
        if (entry_type == ams::fs::DirectoryEntryType_File) {
            return ConfigResult::Success;  // File already exists
        }
    }

    // File doesn't exist, create with defaults
    Config default_config = get_default_config();
    return save_config(path, default_config);
}

#else
// =============================================================================
// PC/Test Implementation
// Uses standard C file I/O for testing on desktop platforms
// =============================================================================

ConfigResult load_config(const char* path, Config& config) {
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return ConfigResult::FileNotFound;
    }

    char line[512];
    Section current_section = Section::None;

    while (std::fgets(line, sizeof(line), file)) {
        // Remove trailing whitespace/newlines
        trim_end(line);

        // Skip empty lines
        const char* trimmed = trim_start(line);
        if (trimmed[0] == '\0') {
            continue;
        }

        // Skip comments
        if (trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        // Check for section header
        Section new_section = parse_section(trimmed);
        if (new_section != Section::None) {
            current_section = new_section;
            continue;
        }

        // Skip if in unknown section
        if (current_section == Section::None || current_section == Section::Unknown) {
            continue;
        }

        // Parse key=value
        char* eq_pos = std::strchr(line, '=');
        if (!eq_pos) {
            continue;  // No '=' found, skip line
        }

        // Split into key and value
        *eq_pos = '\0';
        char* key = line;
        char* value = eq_pos + 1;

        // Trim key and value
        const char* trimmed_key = trim_start(key);
        trim_end(key);

        const char* trimmed_value = trim_start(value);
        trim_end(value);

        // Copy trimmed key (need mutable copy for trim_end)
        char key_buf[64];
        safe_strcpy(key_buf, trimmed_key, sizeof(key_buf) - 1);
        trim_end(key_buf);

        apply_key(current_section, key_buf, trimmed_value, config);
    }

    std::fclose(file);
    return ConfigResult::Success;
}

ConfigResult save_config(const char* path, const Config& config) {
    static char existing[65536];
    static char buffer[sizeof(existing) + 2048];

    size_t existing_length = 0;
    FILE* in = std::fopen(path, "rb");
    if (!in && errno != ENOENT) {
        return ConfigResult::IoError;  // never replace a file we could not read
    }
    if (in) {
        existing_length = std::fread(existing, 1, sizeof(existing) - 1, in);
        const bool too_big = std::fgetc(in) != EOF;
        const bool failed = std::ferror(in) != 0;
        std::fclose(in);
        if (failed) return ConfigResult::IoError;
        if (too_big) return ConfigResult::ParseError;  // never replace what we cannot read whole
    }
    const bool has_existing = existing_length > 0;

    const size_t size = has_existing
                            ? update_config_text(existing, existing_length, config, buffer, sizeof(buffer))
                            : format_config_content(buffer, sizeof(buffer), config);
    if (size == 0) return ConfigResult::IoError;

    char dir_path[256];
    safe_strcpy(dir_path, path, sizeof(dir_path) - 1);
    if (char* last_slash = std::strrchr(dir_path, '/')) {
        *last_slash = '\0';
        mkdir(dir_path, 0755);
    }

    // Write a sibling temp file and rename it over the old one, so a failed
    // write never leaves a truncated config behind.
    char temp_path[260];
    if (std::snprintf(temp_path, sizeof(temp_path), "%s.tmp", path) >= static_cast<int>(sizeof(temp_path))) {
        return ConfigResult::IoError;
    }
    // The Switch SD card uses FAT32/exFAT which has no POSIX permission
    // model; fopen("w") creates with default FAT attributes.  // codeql[cpp/world-writable-file-creation]
    FILE* file = std::fopen(temp_path, "w");
    if (!file) {
        return ConfigResult::IoError;
    }
    const bool written = std::fwrite(buffer, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed || std::rename(temp_path, path) != 0) {
        std::remove(temp_path);
        return ConfigResult::IoError;
    }
    return ConfigResult::Success;
}

ConfigResult ensure_config_exists(const char* path) {
    // Try to open file to check if it exists
    FILE* file = std::fopen(path, "r");
    if (file) {
        std::fclose(file);
        return ConfigResult::Success;  // File already exists
    }

    // File doesn't exist, create with defaults
    Config default_config = get_default_config();
    return save_config(path, default_config);
}

#endif // __SWITCH__

ConfigResult ensure_config_exists(const char* path, const char* legacy_path, bool* out_migrated) {
    *out_migrated = false;
    Config existing = get_default_config();
    const ConfigResult current = load_config(path, existing);
    if (current == ConfigResult::IoError) {
        return current;  // could not tell whether it exists; touch nothing
    }
    if (current != ConfigResult::FileNotFound) {
        return ConfigResult::Success;  // Present (a malformed file is still the user's)
    }
    if (legacy_path != nullptr) {
        Config legacy = get_default_config();
        if (load_config(legacy_path, legacy) == ConfigResult::Success) {
            const ConfigResult saved = save_config(path, legacy);
            *out_migrated = saved == ConfigResult::Success;
            return saved;
        }
    }
    return ensure_config_exists(path);
}

} // namespace potluck::config
