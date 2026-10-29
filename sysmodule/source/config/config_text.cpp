/**
 * @file config_text.cpp
 * @brief config.ini text: the first-boot template and the in-place update
 *
 * Saving rewrites only the lines of keys whose value changed. Every other
 * byte, including user comments, unknown keys, blank lines, spacing and line
 * endings, is copied as is. Pure text in and out, so the Switch and host
 * builds share it.
 */

#include "config.hpp"
#include "config_internal.hpp"
#include <cstdio>
#include <cstring>

namespace potluck::config {

using namespace detail;

namespace {

/// One key PotLuck writes to config.ini, in file order
struct KeySpec {
    const char* section;
    const char* key;
    const char* comment;
    void (*format)(const Config& config, char* out, size_t size);
};

void format_host(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%s", c.server.host); }
void format_port(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%u", c.server.port); }
void format_connect_timeout(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%u", c.network.connect_timeout_ms); }
void format_reconnect_delay(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%u", c.network.reconnect_delay_ms); }
void format_max_reconnect(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%u", c.network.max_reconnect_attempts); }
void format_ldn_mode(const Config& c, char* out, size_t n) {
    const char* word = c.ldn.mode == LdnMode::Local ? "local" : c.ldn.mode == LdnMode::Off ? "off" : "remote";
    std::snprintf(out, n, "%s", word);
}
void format_passphrase(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%s", c.ldn.passphrase); }
void format_disable_p2p(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%d", c.ldn.disable_p2p ? 1 : 0); }
void format_debug_enabled(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%d", c.debug.enabled ? 1 : 0); }
void format_debug_level(const Config& c, char* out, size_t n) { std::snprintf(out, n, "%u", c.debug.level); }

constexpr KeySpec KEYS[] = {
    {"server", "host", "Server hostname or IPv4 address", format_host},
    {"server", "port", "Server port (1-65535)", format_port},
    {"network", "connect_timeout", "Connection timeout in milliseconds", format_connect_timeout},
    {"network", "reconnect_delay", "Reconnect delay in milliseconds", format_reconnect_delay},
    {"network", "max_reconnect_attempts", "Max reconnect attempts (0 = disable auto-reconnect)", format_max_reconnect},
    {"ldn", "mode", "remote (relay server), local (same Wi-Fi network) or off (after a reboot)", format_ldn_mode},
    {"ldn", "passphrase", "Room passphrase (empty = public)", format_passphrase},
    {"ldn", "disable_p2p", "Disable P2P proxy (0/1)", format_disable_p2p},
    {"debug", "enabled", "Write a log to config/potluck/potluck.log (0/1)", format_debug_enabled},
    {"debug", "level", "Log level (0=errors, 1=warnings, 2=info, 3=verbose)", format_debug_level},
};
constexpr size_t KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

/// Bounded append into a caller buffer; any overflow poisons the result
class TextWriter {
public:
    TextWriter(char* buffer, size_t size) : m_buffer(buffer), m_size(size) {}

    void Append(const char* text, size_t length) {
        if (m_overflow || m_length + length >= m_size) {
            m_overflow = true;
            return;
        }
        std::memcpy(m_buffer + m_length, text, length);
        m_length += length;
        m_buffer[m_length] = '\0';
    }

    void Append(const char* text) { Append(text, std::strlen(text)); }

    /// Start a new line when the copied text ended without one
    void EnsureLineBreak() {
        if (m_length > 0 && m_buffer[m_length - 1] != '\n') Append("\n");
    }

    void KeyLine(const KeySpec& spec, const Config& config) {
        EnsureLineBreak();
        char value[MAX_HOST_LENGTH + 32];
        spec.format(config, value, sizeof(value));
        Append(spec.key);
        Append(" = ");
        Append(value);
        Append("\n");
    }

    void SectionHeader(const char* section) {
        EnsureLineBreak();
        Append("[");
        Append(section);
        Append("]\n");
    }

    /// Bytes written, or 0 when the buffer was too small
    size_t Result() const { return m_overflow ? 0 : m_length; }

private:
    char* m_buffer;
    size_t m_size;
    size_t m_length = 0;
    bool m_overflow = false;
};

int find_key(const char* section, const char* key) {
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (std::strcmp(KEYS[i].section, section) == 0 && std::strcmp(KEYS[i].key, key) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Append every key of `section` that the file did not have yet
void append_missing_keys(TextWriter& out, const char* section, const Config& config, bool* written) {
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (!written[i] && std::strcmp(KEYS[i].section, section) == 0) {
            out.KeyLine(KEYS[i], config);
            written[i] = true;
        }
    }
}

/// Copy the key name of a "key = value" line into `out`; false for other lines.
/// Trims before cutting to `size - 1` bytes, as the loader does.
bool extract_key(const char* trimmed, char* out, size_t size) {
    if (trimmed[0] == ';' || trimmed[0] == '#') return false;
    const char* eq = std::strchr(trimmed, '=');
    if (eq == nullptr) return false;
    size_t length = static_cast<size_t>(eq - trimmed);
    while (length > 0 && std::strchr(" \t\r\n", trimmed[length - 1]) != nullptr) length--;
    if (length > size - 1) length = size - 1;
    std::memcpy(out, trimmed, length);
    out[length] = '\0';
    trim_end(out);
    return out[0] != '\0';
}

/// The KEYS section name for a header parse_section() accepted, "" for any other
const char* section_name(Section section) {
    switch (section) {
        case Section::Server:  return "server";
        case Section::Network: return "network";
        case Section::Ldn:     return "ldn";
        case Section::Debug:   return "debug";
        default:               return "";
    }
}


bool is_blank(const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r') return false;
    }
    return true;
}

/// Longest line the updater classifies; the loader reads no further either
constexpr size_t MAX_LINE_LENGTH = 511;

/// Line-by-line state for update_config_text()
struct UpdateState {
    TextWriter& out;
    const Config& config;
    bool written[KEY_COUNT];
    Section section;
    const char* pending_blank_start;   ///< blank lines held back, copied byte for byte
    size_t pending_blank_length;
    bool unsafe;                       ///< a line the loaders read in a way we cannot match

    void FlushBlankLines() {
        out.Append(pending_blank_start, pending_blank_length);
        pending_blank_length = 0;
    }

    void Copy(const char* raw, size_t raw_length, bool has_newline) {
        FlushBlankLines();
        out.Append(raw, raw_length);
        if (has_newline) out.Append("\n");
    }

    /// raw excludes the '\n'; has_newline tells whether the file had one
    void Line(const char* raw, size_t raw_length, bool has_newline) {
        if (is_blank(raw, raw_length)) {
            // held so missing keys land before the gap
            if (pending_blank_length == 0) pending_blank_start = raw;
            pending_blank_length = static_cast<size_t>(raw + raw_length + (has_newline ? 1 : 0) - pending_blank_start);
            return;
        }
        if (raw_length > MAX_LINE_LENGTH) {
            // The Switch loader reads only its first 511 bytes and the host one
            // splits it; no rewrite can keep both readings, so refuse to save.
            unsafe = true;
            Copy(raw, raw_length, has_newline);
            return;
        }

        char line[MAX_LINE_LENGTH + 1];
        std::memcpy(line, raw, raw_length);
        line[raw_length] = '\0';
        trim_end(line);
        const char* trimmed = trim_start(line);

        if (trimmed[0] == '[') {
            // Same grammar as the loader: anything but an exact known header is Unknown
            append_missing_keys(out, section_name(section), config, written);
            section = parse_section(trimmed);
            Copy(raw, raw_length, has_newline);
            return;
        }

        char key[64];
        const int index = extract_key(trimmed, key, sizeof(key)) ? find_key(section_name(section), key) : -1;
        if (index < 0) {
            Copy(raw, raw_length, has_newline);
            return;
        }
        written[index] = true;

        if (HoldsSavedValue(index, section, key, std::strchr(trimmed, '=') + 1)) {
            Copy(raw, raw_length, has_newline);  // unchanged value: keep its spelling and line ending
        } else {
            FlushBlankLines();
            out.KeyLine(KEYS[index], config);
        }
    }

    /// True when the line's value already means what config would write
    bool HoldsSavedValue(int index, Section line_section, const char* key, const char* value_text) const {
        char value[MAX_LINE_LENGTH + 1];
        safe_strcpy(value, trim_start(value_text), MAX_LINE_LENGTH);
        trim_end(value);

        Config from_file = config;
        apply_key(line_section, key, value, from_file);
        char saved[MAX_HOST_LENGTH + 32];
        char current[MAX_HOST_LENGTH + 32];
        KEYS[index].format(config, saved, sizeof(saved));
        KEYS[index].format(from_file, current, sizeof(current));
        return std::strcmp(saved, current) == 0;
    }

    void Finish() {
        append_missing_keys(out, section_name(section), config, written);
        FlushBlankLines();
        for (size_t i = 0; i < KEY_COUNT; i++) {
            if (written[i]) continue;
            out.EnsureLineBreak();
            out.Append("\n");
            out.SectionHeader(KEYS[i].section);
            append_missing_keys(out, KEYS[i].section, config, written);
        }
    }
};

} // anonymous namespace

size_t format_config_content(char* buffer, size_t buffer_size, const Config& config) {
    TextWriter out(buffer, buffer_size);
    out.Append("; PotLuck configuration\n");
    out.Append("; Created on first boot. PotLuck changes only the keys it saves.\n");
    const char* section = "";
    for (const KeySpec& spec : KEYS) {
        if (std::strcmp(spec.section, section) != 0) {
            section = spec.section;
            out.Append("\n");
            out.SectionHeader(section);
        }
        out.Append("; ");
        out.Append(spec.comment);
        out.Append("\n");
        out.KeyLine(spec, config);
    }
    return out.Result();
}

size_t update_config_text(const char* existing, size_t existing_length, const Config& config,
                          char* buffer, size_t buffer_size) {
    TextWriter out(buffer, buffer_size);
    UpdateState state{out, config, {}, Section::None, existing, 0, false};

    size_t start = 0;
    for (size_t i = 0; i <= existing_length; i++) {
        if (i < existing_length && existing[i] != '\n') continue;
        const bool has_newline = i < existing_length;
        if (i > start || has_newline) {
            state.Line(existing + start, i - start, has_newline);
        }
        start = i + 1;
    }
    state.Finish();
    return state.unsafe ? 0 : out.Result();
}

} // namespace potluck::config
