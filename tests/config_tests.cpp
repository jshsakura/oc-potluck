/**
 * @file config_tests.cpp
 * @brief Unit tests for Configuration Manager
 *
 * Tests for parsing INI config files and default values.
 */

#include "config/config.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iterator>
#include <string>

using namespace potluck::config;

// ============================================================================
// Test Framework (minimal, no external dependencies)
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { register_test(#name, test_##name); } \
    } g_test_register_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a != _b) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            printf("    got: %lld vs %lld\n", (long long)_a, (long long)_b); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

#define ASSERT_STREQ(a, b) \
    do { \
        const char* _a = (a); \
        const char* _b = (b); \
        if (std::strcmp(_a, _b) != 0) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            printf("    got: \"%s\" vs \"%s\"\n", _a, _b); \
            throw std::runtime_error("Test assertion failed"); \
        } \
    } while(0)

struct TestEntry {
    const char* name;
    void (*func)();
};

static TestEntry g_tests[128];
static int g_test_count = 0;

static void register_test(const char* name, void (*func)()) {
    if (g_test_count < 128) {
        g_tests[g_test_count++] = {name, func};
    }
}

// Helper to create temp config file
class TempConfigFile {
public:
    TempConfigFile(const char* content) {
        snprintf(m_path, sizeof(m_path), "/tmp/test_config_%d.ini", rand());
        std::ofstream f(m_path);
        if (f.is_open()) {
            f << content;
            f.close();
        }
    }

    ~TempConfigFile() {
        std::remove(m_path);
    }

    const char* path() const { return m_path; }

private:
    char m_path[256];
};

// ============================================================================
// Default Values Tests
// ============================================================================

TEST(default_values) {
    Config config = get_default_config();

    // Server defaults
    ASSERT_STREQ(config.server.host, "");
    ASSERT_EQ(config.server.port, 30456);
    ASSERT_EQ(config.server.use_tls, false);

    // Network defaults
    ASSERT_EQ(config.network.connect_timeout_ms, 5000u);
    ASSERT_EQ(config.network.ping_interval_ms, 10000u);
    ASSERT_EQ(config.network.reconnect_delay_ms, 3000u);
    ASSERT_EQ(config.network.max_reconnect_attempts, 5u);

    // LDN defaults
    ASSERT_EQ(config.ldn.mode, LdnMode::Remote);
    ASSERT_STREQ(config.ldn.passphrase, "");

    // Debug defaults
    ASSERT_EQ(config.debug.enabled, false);
    ASSERT_EQ(config.debug.level, 1u);
    ASSERT_EQ(config.debug.log_to_file, false);
}

// ============================================================================
// Parse Tests
// ============================================================================

TEST(parse_empty_file) {
    TempConfigFile file("");
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    // Should have defaults
    ASSERT_STREQ(config.server.host, "");
}

TEST(parse_server_section) {
    const char* content =
        "[server]\n"
        "host = 192.168.1.100\n"
        "port = 12345\n"
        "use_tls = 0\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "192.168.1.100");
    ASSERT_EQ(config.server.port, 12345);
    ASSERT_EQ(config.server.use_tls, false);
}

static ConfigResult parse_config_string(const char* content, Config& config) {
    TempConfigFile file(content);
    return load_config(file.path(), config);
}

TEST(server_port_out_of_range_is_invalid) {
    const char* content =
        "[server]\n"
        "port = 70000\n";
    Config config = get_default_config();
    ASSERT_EQ(parse_config_string(content, config), ConfigResult::Success);
    ASSERT_EQ(config.server.port, 0);
    ServerCheck check = validate_server_config(config.server);
    ASSERT_TRUE(check.port_reset);
    ASSERT_FALSE(check.host_reset);
    ASSERT_EQ(config.server.port, DEFAULT_PORT);
}

TEST(server_port_with_junk_is_invalid) {
    const char* bad[] = {"0", "12a", "-1", "1.5", "99999999999"};
    for (const char* value : bad) {
        char content[64];
        snprintf(content, sizeof(content), "[server]\nport = %s\n", value);
        Config config = get_default_config();
        parse_config_string(content, config);
        ASSERT_TRUE(validate_server_config(config.server).port_reset);
    }
}

TEST(server_port_limits_are_valid) {
    Config config = get_default_config();
    parse_config_string("[server]\nport = 65535\n", config);
    ASSERT_EQ(config.server.port, 65535);
    parse_config_string("[server]\nport = 1\n", config);
    ASSERT_EQ(config.server.port, 1);
    ASSERT_FALSE(validate_server_config(config.server).port_reset);
}

TEST(server_host_validation) {
    const char* good[] = {"relay.example.com", "localhost", "192.168.1.100", "0.0.0.0",
                          "a-b.c1", "my-server"};
    for (const char* host : good) ASSERT_TRUE(is_valid_server_host(host));
    const char* bad[] = {"", "has space.com", "256.1.1.1", "1.2.3", "1.2.3.4.5", "1..2.3",
                         "-lead.com", "trail-.com", "a..b", ".com", "com.", "host:30456",
                         "under_score.com", "1234.1.1.1"};
    for (const char* host : bad) {
        if (is_valid_server_host(host)) printf("  unexpected valid: \"%s\"\n", host);
        ASSERT_FALSE(is_valid_server_host(host));
    }
    char label[80];
    memset(label, 'a', 64);
    label[64] = '\0';
    ASSERT_FALSE(is_valid_server_host(label));
    label[63] = '\0';
    ASSERT_TRUE(is_valid_server_host(label));
}

TEST(server_host_invalid_falls_back_to_default) {
    Config config = get_default_config();
    parse_config_string("[server]\nhost = not a host\nport = 1234\n", config);
    ServerCheck check = validate_server_config(config.server);
    ASSERT_TRUE(check.host_reset);
    ASSERT_FALSE(check.port_reset);
    ASSERT_STREQ(config.server.host, DEFAULT_HOST);
    ASSERT_EQ(config.server.port, 1234);
}

TEST(empty_host_means_no_server) {
    Config config = get_default_config();
    parse_config_string("[server]\nhost =\nport = 1234\n", config);
    ServerCheck check = validate_server_config(config.server);
    ASSERT_FALSE(check.host_reset);
    ASSERT_STREQ(config.server.host, "");
}

TEST(remote_without_a_server_plays_local) {
    Config config = get_default_config();
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Local);  // first install: no server yet

    std::strcpy(config.server.host, "relay.example.com");
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Remote);

    config.ldn.mode = LdnMode::Local;
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Local);
}

TEST(off_mode_parses_and_stays_off) {
    Config config = get_default_config();
    parse_config_string("[ldn]\nmode = off\n", config);
    ASSERT_TRUE(config.ldn.mode == LdnMode::Off);
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Off);  // no server is not a reason to turn on

    std::strcpy(config.server.host, "relay.example.com");
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Off);

    parse_config_string("[ldn]\nmode = sideways\n", config);
    ASSERT_TRUE(config.ldn.mode == LdnMode::Off);  // unknown words keep the current value
}

TEST(server_host_too_long_is_rejected_not_truncated) {
    char content[256];
    char host[MAX_HOST_LENGTH + 8];
    memset(host, 'a', sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    snprintf(content, sizeof(content), "[server]\nhost = %s\n", host);
    Config config = get_default_config();
    parse_config_string(content, config);
    // Never a truncated, different host: dropped to "no server", the default
    ASSERT_STREQ(config.server.host, "");
    ASSERT_FALSE(validate_server_config(config.server).host_reset);
    ASSERT_TRUE(effective_ldn_mode(config) == LdnMode::Local);
}

TEST(parse_network_section) {
    const char* content =
        "[network]\n"
        "connect_timeout = 10000\n"
        "ping_interval = 5000\n"
        "reconnect_delay = 1000\n"
        "max_reconnect_attempts = 10\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.network.connect_timeout_ms, 10000u);
    ASSERT_EQ(config.network.ping_interval_ms, DEFAULT_PING_INTERVAL_MS);  // removed key, ignored
    ASSERT_EQ(config.network.reconnect_delay_ms, 1000u);
    ASSERT_EQ(config.network.max_reconnect_attempts, 10u);
}

TEST(parse_ldn_section) {
    const char* content =
        "[ldn]\n"
        "mode = local\n"
        "passphrase = secret123\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.ldn.mode, LdnMode::Local);
    ASSERT_STREQ(config.ldn.passphrase, "secret123");
}

TEST(parse_debug_section) {
    const char* content =
        "[debug]\n"
        "enabled = 1\n"
        "level = 3\n"
        "log_to_file = 1\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.debug.enabled, true);
    ASSERT_EQ(config.debug.level, 3u);
    ASSERT_EQ(config.debug.log_to_file, true);
}

TEST(parse_comments_ignored) {
    const char* content =
        "; This is a comment\n"
        "[server]\n"
        "; Another comment\n"
        "host = test.server.com\n"
        "  ; Indented comment\n"
        "port = 9999\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "test.server.com");
    ASSERT_EQ(config.server.port, 9999);
}

TEST(parse_whitespace_handling) {
    const char* content =
        "[server]\n"
        "  host   =   spaced.server.com  \n"
        "port=12345\n";  // No spaces

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "spaced.server.com");
    ASSERT_EQ(config.server.port, 12345);
}

TEST(parse_unknown_section_ignored) {
    const char* content =
        "[unknown]\n"
        "foo = bar\n"
        "[server]\n"
        "port = 11111\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.server.port, 11111);
}

TEST(parse_unknown_key_ignored) {
    const char* content =
        "[server]\n"
        "unknown_key = value\n"
        "port = 22222\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(config.server.port, 22222);
}

TEST(file_not_found) {
    Config config = get_default_config();
    ConfigResult result = load_config("/nonexistent/path/config.ini", config);

    ASSERT_EQ(result, ConfigResult::FileNotFound);
    // Config should still have defaults
    ASSERT_STREQ(config.server.host, "");
}

TEST(passphrase_truncated) {
    // Passphrase > 64 chars should be truncated
    const char* content =
        "[ldn]\n"
        "passphrase = 12345678901234567890123456789012345678901234567890123456789012345678901234567890\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(strlen(config.ldn.passphrase), 64u);
}

TEST(full_config_example) {
    const char* content =
        "; Full example\n"
        "[server]\n"
        "host = custom.ldn.server\n"
        "port = 30000\n"
        "use_tls = 1\n"
        "\n"
        "[network]\n"
        "connect_timeout = 8000\n"
        "ping_interval = 15000\n"
        "reconnect_delay = 2000\n"
        "max_reconnect_attempts = 3\n"
        "\n"
        "[ldn]\n"
        "mode = remote\n"
        "passphrase = myroom\n"
        "\n"
        "[debug]\n"
        "enabled = 1\n"
        "level = 2\n"
        "log_to_file = 0\n";

    TempConfigFile file(content);
    Config config = get_default_config();
    ConfigResult result = load_config(file.path(), config);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(config.server.host, "custom.ldn.server");
    ASSERT_EQ(config.server.port, 30000);
    ASSERT_EQ(config.server.use_tls, false);  // removed key, ignored
    ASSERT_EQ(config.network.connect_timeout_ms, 8000u);
    ASSERT_EQ(config.network.ping_interval_ms, DEFAULT_PING_INTERVAL_MS);  // removed key, ignored
    ASSERT_EQ(config.network.reconnect_delay_ms, 2000u);
    ASSERT_EQ(config.network.max_reconnect_attempts, 3u);
    ASSERT_EQ(config.ldn.mode, LdnMode::Remote);
    ASSERT_STREQ(config.ldn.passphrase, "myroom");
    ASSERT_EQ(config.debug.enabled, true);
    ASSERT_EQ(config.debug.level, 2u);
    ASSERT_EQ(config.debug.log_to_file, false);
}

// ============================================================================
// Save Config Tests
// ============================================================================

TEST(save_config_creates_file) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_save_config_%d.ini", rand());

    Config config = get_default_config();
    ConfigResult result = save_config(path, config);

    ASSERT_EQ(result, ConfigResult::Success);

    // Verify file exists and can be loaded back
    Config loaded = get_default_config();
    loaded.server.port = 0;  // Change to verify it gets loaded
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(loaded.server.port, DEFAULT_PORT);

    std::remove(path);
}

static std::string update_text(const char* existing, const Config& config) {
    static char out[8192];
    const size_t n = update_config_text(existing, strlen(existing), config, out, sizeof(out));
    return std::string(out, n);
}

TEST(update_changes_only_saved_keys_and_keeps_comments) {
    const char* existing =
        "; my notes about this console\n"
        "[server]\n"
        "host = old.example.com   ; trailing note\n"
        "# keep this comment\n"
        "port = 1111\n"
        "custom_key = stays\n"
        "\n"
        "[ldn]\n"
        "passphrase =\n"
        "mode = remote\n"
        "disable_p2p = 1\n";
    Config config = get_default_config();
    strcpy(config.server.host, "new.example.com");
    config.server.port = 2222;
    config.ldn.mode = LdnMode::Local;

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("; my notes about this console\n[server]\nhost = new.example.com\n"
                         "# keep this comment\nport = 2222\ncustom_key = stays\n") == 0);
    ASSERT_TRUE(out.find("[ldn]\npassphrase =\nmode = local\ndisable_p2p = 1\n") != std::string::npos);
}

TEST(update_adds_missing_keys_at_section_end_before_blank_gap) {
    const char* existing =
        "[server]\n"
        "host = a.example.com\n"
        "\n"
        "[debug]\n"
        "level = 2\n";
    Config config = get_default_config();
    strcpy(config.server.host, "a.example.com");
    config.debug.level = 3;

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("[server]\nhost = a.example.com\nport = 30456\n\n[debug]\nlevel = 3\n"
                         "enabled = 0\n") == 0);
    // Sections the file lacked come after everything it had
    ASSERT_TRUE(out.find("\n[network]\n") != std::string::npos);
    ASSERT_TRUE(out.find("\n[ldn]\nmode = remote\npassphrase = \ndisable_p2p = 1\n") != std::string::npos);
}

TEST(update_round_trips_through_the_parser) {
    const char* existing = "[ldn]\r\nmode = local\r\n; keep\r\n[unknown]\r\nhost = not-ours\r\n";
    Config config = get_default_config();
    strcpy(config.ldn.passphrase, "abcd");

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("; keep\r\npassphrase = abcd\ndisable_p2p = 1\n[unknown]\r\nhost = not-ours\r\n") != std::string::npos);

    TempConfigFile file(out.c_str());
    Config loaded = get_default_config();
    ASSERT_EQ(load_config(file.path(), loaded), ConfigResult::Success);
    ASSERT_STREQ(loaded.ldn.passphrase, "abcd");
    ASSERT_EQ(loaded.ldn.mode, LdnMode::Remote);
    ASSERT_STREQ(loaded.server.host, DEFAULT_HOST);
}

TEST(update_keeps_unchanged_lines_verbatim) {
    const char* existing = "[server]\r\n  port=30456  \r\nhost = relay.example.com\r\n";
    Config config = get_default_config();
    strcpy(config.server.host, "other.example.com");

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("[server]\r\n  port=30456  \r\nhost = other.example.com\n") == 0);
}

TEST(update_uses_the_loader_section_grammar) {
    // The loader treats "[ldn] ; note" as an unknown section, so the updater must too
    const char* existing = "[ldn] ; note\nmode = remote\n";
    Config config = get_default_config();
    config.ldn.mode = LdnMode::Local;

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("[ldn] ; note\nmode = remote\n") == 0);
    TempConfigFile file(out.c_str());
    Config loaded = get_default_config();
    ASSERT_EQ(load_config(file.path(), loaded), ConfigResult::Success);
    ASSERT_EQ(loaded.ldn.mode, LdnMode::Local);
}

TEST(parse_ldn_mode_values) {
    Config config = get_default_config();
    ASSERT_EQ(parse_config_string("[ldn]\nmode = LOCAL\n", config), ConfigResult::Success);
    ASSERT_EQ(config.ldn.mode, LdnMode::Local);

    config = get_default_config();
    config.ldn.mode = LdnMode::Local;
    ASSERT_EQ(parse_config_string("[ldn]\nmode = remote\n", config), ConfigResult::Success);
    ASSERT_EQ(config.ldn.mode, LdnMode::Remote);

    // Anything else, and the removed on/off key, leave the default
    config = get_default_config();
    ASSERT_EQ(parse_config_string("[ldn]\nmode = wireless\nenabled = 0\n", config), ConfigResult::Success);
    ASSERT_EQ(config.ldn.mode, LdnMode::Remote);
}

TEST(update_matches_keys_padded_past_the_key_buffer) {
    // The loader trims the key before its 63-byte cut, so a padded key still counts
    const std::string existing = "[ldn]\nmode=remote\nmode" + std::string(64, ' ') + "=remote\n";
    Config config = get_default_config();
    config.ldn.mode = LdnMode::Local;

    const std::string out = update_text(existing.c_str(), config);
    TempConfigFile file(out.c_str());
    Config loaded = get_default_config();
    ASSERT_EQ(load_config(file.path(), loaded), ConfigResult::Success);
    ASSERT_EQ(loaded.ldn.mode, LdnMode::Local);
}

TEST(update_refuses_files_with_overlong_lines) {
    // The loaders read a line past 511 bytes differently, so any rewrite could
    // change what they see (reviewer repros: a shadowing key, a long header)
    const std::string shadowed = "[ldn]\nmode=remote\nmode=remote" + std::string(512, ' ') + "\n";
    const std::string header = "[server]\n[ldn]" + std::string(512, ' ') + "\n[debug]\n";
    static char out[8192];
    Config config = get_default_config();
    config.ldn.mode = LdnMode::Local;
    config.server.port = 43210;
    ASSERT_EQ(update_config_text(shadowed.c_str(), shadowed.size(), config, out, sizeof(out)), 0u);
    ASSERT_EQ(update_config_text(header.c_str(), header.size(), config, out, sizeof(out)), 0u);
}

TEST(save_config_leaves_unsafe_files_untouched) {
    const std::string original = "[server]\ncustom = " + std::string(600, 'x') + "\n";
    TempConfigFile file(original.c_str());
    Config config = get_default_config();
    config.server.port = 43210;
    ASSERT_EQ(save_config(file.path(), config), ConfigResult::IoError);
    std::ifstream in(file.path());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(text == original);
}

TEST(update_copies_blank_lines_and_last_line_verbatim) {
    const char* existing = "[server]\nport = 1\n\t \r\n\n[debug]\nlevel = 1";
    Config config = get_default_config();
    config.server.port = 2;  // the only change
    config.debug.level = 1;

    const std::string out = update_text(existing, config);
    ASSERT_TRUE(out.find("[server]\nport = 2\nhost = \n\t \r\n\n[debug]\nlevel = 1\nenabled = 0\n") == 0);
}

TEST(update_without_changes_returns_the_file) {
    const char* existing = "; notes\n[server]\r\nhost=\r\n  port = 30456\n\n";
    Config config = get_default_config();
    std::string out = update_text(existing, config);
    // Every other section is appended, but nothing before it changes
    ASSERT_TRUE(out.find(existing) == 0);
}

TEST(save_config_refuses_to_replace_an_unreadable_path) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/test_config_dir_%d.ini", rand());
    ASSERT_EQ(mkdir(path, 0755), 0);  // reading a directory fails like an I/O error
    Config config = get_default_config();
    ASSERT_EQ(save_config(path, config), ConfigResult::IoError);
    struct stat info{};
    ASSERT_EQ(stat(path, &info), 0);
    ASSERT_TRUE(S_ISDIR(info.st_mode));
    rmdir(path);
}

TEST(update_reports_overflow_instead_of_truncating) {
    char out[16];
    Config config = get_default_config();
    ASSERT_EQ(update_config_text("[server]\nhost = x\n", 18, config, out, sizeof(out)), 0u);
    ASSERT_EQ(format_config_content(out, sizeof(out), config), 0u);
}

TEST(template_has_no_removed_keys) {
    static char out[4096];
    Config config = get_default_config();
    const size_t n = format_config_content(out, sizeof(out), config);
    ASSERT_TRUE(n > 0);
    const std::string text(out, n);
    ASSERT_TRUE(text.find("use_tls") == std::string::npos);
    ASSERT_TRUE(text.find("ping_interval") == std::string::npos);
    ASSERT_TRUE(text.find("interface") == std::string::npos);
    ASSERT_TRUE(text.find("host = \n") != std::string::npos);
}

TEST(save_config_keeps_user_comments) {
    TempConfigFile file("; hand-written note\n[server]\nhost = keep.example.com\nport = 4000\n");
    Config config = get_default_config();
    ASSERT_EQ(load_config(file.path(), config), ConfigResult::Success);
    config.ldn.mode = LdnMode::Local;
    ASSERT_EQ(save_config(file.path(), config), ConfigResult::Success);

    std::ifstream in(file.path());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(text.find("; hand-written note\n[server]\nhost = keep.example.com\nport = 4000\n") == 0);
    ASSERT_TRUE(text.find("[ldn]\nmode = local\n") != std::string::npos);
}

TEST(save_config_preserves_values) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_save_preserve_%d.ini", rand());

    Config config = get_default_config();
    // Modify some values
    strcpy(config.server.host, "test.example.com");
    config.server.port = 12345;
    config.server.use_tls = false;
    config.network.connect_timeout_ms = 9999;
    config.ldn.mode = LdnMode::Local;
    strcpy(config.ldn.passphrase, "testpass");
    config.debug.enabled = true;
    config.debug.level = 3;

    ConfigResult result = save_config(path, config);
    ASSERT_EQ(result, ConfigResult::Success);

    // Load it back
    Config loaded = get_default_config();
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(loaded.server.host, "test.example.com");
    ASSERT_EQ(loaded.server.port, 12345);
    ASSERT_EQ(loaded.server.use_tls, false);
    ASSERT_EQ(loaded.network.connect_timeout_ms, 9999u);
    ASSERT_EQ(loaded.ldn.mode, LdnMode::Local);
    ASSERT_STREQ(loaded.ldn.passphrase, "testpass");
    ASSERT_EQ(loaded.debug.enabled, true);
    ASSERT_EQ(loaded.debug.level, 3u);

    std::remove(path);
}

// ============================================================================
// Ensure Config Exists Tests
// ============================================================================

TEST(ensure_config_exists_creates_new) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ensure_%d.ini", rand());

    // Make sure file doesn't exist
    std::remove(path);

    ConfigResult result = ensure_config_exists(path);
    ASSERT_EQ(result, ConfigResult::Success);

    // Verify file was created with defaults
    Config loaded = get_default_config();
    loaded.server.port = 0;  // Change to verify
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_EQ(loaded.server.port, DEFAULT_PORT);
    ASSERT_STREQ(loaded.server.host, DEFAULT_HOST);

    std::remove(path);
}

TEST(ensure_config_exists_keeps_existing) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_ensure_existing_%d.ini", rand());

    // Create a config with custom values
    const char* content =
        "[server]\n"
        "host = custom.host.com\n"
        "port = 54321\n";

    std::ofstream f(path);
    f << content;
    f.close();

    // ensure_config_exists should not overwrite
    ConfigResult result = ensure_config_exists(path);
    ASSERT_EQ(result, ConfigResult::Success);

    // Verify original values are preserved
    Config loaded = get_default_config();
    result = load_config(path, loaded);

    ASSERT_EQ(result, ConfigResult::Success);
    ASSERT_STREQ(loaded.server.host, "custom.host.com");
    ASSERT_EQ(loaded.server.port, 54321);

    std::remove(path);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== potluck Config Unit Tests ===\n\n");
    printf("Running %d tests...\n\n", g_test_count);

    for (int i = 0; i < g_test_count; i++) {
        g_tests_run++;
        printf("[%d/%d] %s...", i + 1, g_test_count, g_tests[i].name);
        fflush(stdout);

        try {
            g_tests[i].func();
            printf(" OK\n");
            g_tests_passed++;
        } catch (...) {
            g_tests_failed++;
        }
    }

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);

    if (g_tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("FAILED\n");
    }

    return g_tests_failed > 0 ? 1 : 0;
}
