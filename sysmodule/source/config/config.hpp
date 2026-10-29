/**
 * @file config.hpp
 * @brief Configuration Manager for potluck
 *
 * This module handles loading and parsing of INI configuration files.
 * It provides all runtime settings for the sysmodule including server
 * connection details, network timeouts, and debug options.
 *
 * ## Design Principles
 *
 * 1. **No Dynamic Allocation**: All strings use fixed-size buffers suitable
 *    for embedded/sysmodule use on Nintendo Switch.
 *
 * 2. **Safe Defaults**: If config file is missing or malformed, sensible
 *    defaults are used so the sysmodule can still function.
 *
 * 3. **Simple INI Format**: Standard INI syntax with [sections] and key=value
 *    pairs. Comments start with ; or #.
 *
 * ## Configuration File Location
 *
 * On Nintendo Switch: `/atmosphere/contents/4200000000504C4B/config.ini`
 *
 * ## INI File Format
 *
 * ```ini
 * ; Comment line
 * [section]
 * key = value
 * another_key = another value
 * ```
 *
 * ## Supported Sections
 *
 * - `[server]`: Server hostname, port, TLS settings
 * - `[network]`: Timeouts, reconnect behavior
 * - `[ldn]`: remote or local mode, passphrase
 * - `[debug]`: Logging configuration
 *
 * ## Usage Example
 *
 * @code
 * #include "config/config.hpp"
 *
 * using namespace potluck::config;
 *
 * // Get defaults first
 * Config config = get_default_config();
 *
 * // Try to load from file (keeps defaults if file missing)
 * ConfigResult result = load_config("/atmosphere/contents/4200000000504C4B/config.ini", config);
 *
 * if (result == ConfigResult::Success) {
 *     printf("Loaded config, server: %s:%d\n", config.server.host, config.server.port);
 * } else if (result == ConfigResult::FileNotFound) {
 *     printf("Using default config\n");
 * }
 * @endcode
 *
 * @see format_config_content() for every key and the file written on first boot
 * @see Epic 2, Story 2.1 for requirements
 */

#pragma once
#include "potluck_paths.h"

#include <cstdint>
#include <cstddef>

namespace potluck::config {

// =============================================================================
// Constants
// =============================================================================

/**
 * @brief Maximum length of server hostname/IP (excluding null terminator)
 *
 * 128 characters is sufficient for domain names (max 253 chars in DNS,
 * but practical limit is much lower) and IPv4/IPv6 addresses.
 */
constexpr size_t MAX_HOST_LENGTH = 128;

/** @brief Longest DNS label in a hostname */
constexpr size_t MAX_HOST_LABEL_LENGTH = 63;

/** @brief Highest TCP port */
constexpr uint32_t MAX_PORT = 65535;

/**
 * @brief Maximum length of room passphrase (excluding null terminator)
 *
 * Matches the protocol's PassphraseMessage limit of 64 bytes.
 */
constexpr size_t MAX_PASSPHRASE_LENGTH = 64;

/**
 * @brief Maximum length of network interface name (excluding null terminator)
 *
 * Linux interface names are typically max 15 chars (IFNAMSIZ).
 */
constexpr size_t MAX_INTERFACE_LENGTH = 32;

/**
 * @brief Configuration file, directory and log on the SD card
 *
 * All of them live in the module's own folder (potluck_paths.h).
 */
constexpr const char* CONFIG_PATH = POTLUCK_CONFIG_PATH;
constexpr const char* CONFIG_DIR = POTLUCK_MODULE_DIR;
constexpr const char* LOG_PATH = POTLUCK_LOG_PATH;

/**
 * @brief Settings file of the module before it was renamed to PotLuck
 */
constexpr const char* LEGACY_CONFIG_PATH = POTLUCK_LEGACY_CONFIG_PATH;

// -----------------------------------------------------------------------------
// Default Values - Server
// -----------------------------------------------------------------------------

/**
 * @brief Default server host: none
 *
 * There is no public relay server. Until the user sets one, remote mode
 * plays as local mode (see effective_ldn_mode()).
 */
constexpr const char* DEFAULT_HOST = "";

/** @brief Default server port */
constexpr uint16_t DEFAULT_PORT = 30456;

/** @brief Default TLS setting — NOT IMPLEMENTED, always plain TCP regardless of this value */
constexpr bool DEFAULT_USE_TLS = false;

// -----------------------------------------------------------------------------
// Default Values - Network
// -----------------------------------------------------------------------------

/** @brief Default connection timeout (5 seconds) */
constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MS = 5000;

/** @brief Default ping/keepalive interval (10 seconds) */
constexpr uint32_t DEFAULT_PING_INTERVAL_MS = 10000;

/** @brief Default initial reconnect delay (3 seconds) */
constexpr uint32_t DEFAULT_RECONNECT_DELAY_MS = 3000;

/** @brief Default maximum reconnection attempts (0 = disable auto-reconnect) */
constexpr uint32_t DEFAULT_MAX_RECONNECT_ATTEMPTS = 5;

// -----------------------------------------------------------------------------
// Default Values - LDN
// -----------------------------------------------------------------------------

/**
 * @brief Where LDN rooms are hosted
 *
 * Remote goes through the relay server; Local hosts and joins rooms on the
 * same Wi-Fi network with ldn_mitm's LAN protocol. Off leaves games alone:
 * new games stop going through the module at once, and from the next boot
 * the MITMs are not registered at all. A game keeps the mode that was set
 * when it first opened bsd:u or ldn:u.
 */
enum class LdnMode : uint32_t {
    Remote = 0,
    Local = 1,
    Off = 2,
};

/** @brief Default LDN mode */
constexpr LdnMode DEFAULT_LDN_MODE = LdnMode::Remote;

/** @brief Default P2P proxy disabled state: relay only, P2P off */
constexpr bool DEFAULT_DISABLE_P2P = true;

// -----------------------------------------------------------------------------
// Default Values - Debug
// -----------------------------------------------------------------------------

/** @brief Default debug logging state */
constexpr bool DEFAULT_DEBUG_ENABLED = false;

/** @brief Default debug log level (1 = warnings) */
constexpr uint32_t DEFAULT_DEBUG_LEVEL = 1;

/** @brief Default file logging state */
constexpr bool DEFAULT_LOG_TO_FILE = false;

// =============================================================================
// Result Codes
// =============================================================================

/**
 * @brief Result codes for configuration operations
 */
enum class ConfigResult {
    Success = 0,       ///< Configuration loaded successfully
    FileNotFound,      ///< Configuration file does not exist
    ParseError,        ///< File exists but contains syntax errors
    IoError            ///< File I/O error (permissions, disk full, etc.)
};

// =============================================================================
// Configuration Structures
// =============================================================================

/**
 * @brief Server connection settings
 *
 * Configuration for connecting to the potluck server.
 * Corresponds to the [server] section in config.ini.
 *
 * ## INI Keys
 * - `host`: Server hostname or IP address
 * - `port`: Server port number
 * - `use_tls`: Enable TLS encryption (0/1)
 */
struct ServerConfig {
    char host[MAX_HOST_LENGTH + 1];  ///< Server hostname/IP (null-terminated)
    uint16_t port;                    ///< Server port number
    bool use_tls;                     ///< Use TLS/SSL encryption
};

/**
 * @brief Network behavior settings
 *
 * Configuration for network timeouts and reconnection behavior.
 * Corresponds to the [network] section in config.ini.
 *
 * ## INI Keys
 * - `connect_timeout`: Connection timeout in milliseconds
 * - `ping_interval`: Keepalive ping interval in milliseconds
 * - `reconnect_delay`: Initial delay before reconnection attempt
 * - `max_reconnect_attempts`: Maximum reconnect attempts (0 = disable auto-reconnect)
 */
struct NetworkConfig {
    uint32_t connect_timeout_ms;       ///< TCP connection timeout
    uint32_t ping_interval_ms;         ///< Keepalive ping interval
    uint32_t reconnect_delay_ms;       ///< Initial reconnect delay
    uint32_t max_reconnect_attempts;   ///< Max reconnect attempts (0 disables auto-reconnect)
};

/**
 * @brief LDN emulation settings
 *
 * Configuration for LDN (Local Wireless) emulation behavior.
 * Corresponds to the [ldn] section in config.ini.
 *
 * ## INI Keys
 * - `mode`: `remote` (relay server), `local` (same Wi-Fi network) or `off`
 * - `passphrase`: Passphrase for private rooms (max 64 chars)
 * - `interface`: Preferred network interface (empty = auto)
 * - `disable_p2p`: Disable P2P proxy (0/1) - like the reference client MultiplayerDisableP2p
 */
struct LdnConfig {
    LdnMode mode;                                    ///< Remote rooms, local rooms or off
    char passphrase[MAX_PASSPHRASE_LENGTH + 1];      ///< Room passphrase (null-terminated)
    char interface_name[MAX_INTERFACE_LENGTH + 1];   ///< Network interface (null-terminated)
    bool disable_p2p;                                ///< Disable P2P proxy (the reference client's MultiplayerDisableP2p)
};

/**
 * @brief Debug and logging settings
 *
 * Configuration for debugging and logging behavior.
 * Corresponds to the [debug] section in config.ini.
 *
 * ## INI Keys
 * - `enabled`: Enable debug logging (0/1)
 * - `level`: Log verbosity (0=errors, 1=warnings, 2=info, 3=verbose)
 * - `log_to_file`: Also write logs to file (0/1)
 *
 * ## Log Levels
 * - 0: Errors only (critical issues)
 * - 1: Warnings (potential problems)
 * - 2: Info (normal operation events)
 * - 3: Verbose (detailed debugging)
 */
struct DebugConfig {
    bool enabled;       ///< Enable debug logging
    uint32_t level;     ///< Log level (0-3)
    bool log_to_file;   ///< Write logs to file
};

/**
 * @brief Complete configuration
 *
 * Aggregates all configuration sections into a single structure.
 * Use get_default_config() to initialize with defaults, then
 * load_config() to override with file settings.
 */
struct Config {
    ServerConfig server;    ///< Server connection settings
    NetworkConfig network;  ///< Network behavior settings
    LdnConfig ldn;          ///< LDN emulation settings
    DebugConfig debug;      ///< Debug/logging settings
};

// =============================================================================
// Functions
// =============================================================================

/**
 * @brief Get configuration with all default values
 *
 * Returns a Config struct populated with sensible defaults.
 * Use this as a starting point before calling load_config().
 *
 * @return Config struct with default values on production release
 *
 * ## Default Values
 * - server.host: "" (no server: remote plays as local)
 * - server.port: 30456
 * - server.use_tls: true
 * - network.connect_timeout_ms: 5000
 * - network.ping_interval_ms: 10000
 * - network.reconnect_delay_ms: 3000
 * - network.max_reconnect_attempts: 5
 * - ldn.mode: remote
 * - ldn.passphrase: "" (empty)
 * - ldn.interface_name: "" (empty = auto-detect)
 * - ldn.disable_p2p: false
 * - debug.enabled: false
 * - debug.level: 1 (warnings)
 * - debug.log_to_file: false
 *
 * ## Not Yet Wired
 * - server.use_tls: parsed and stored but not consumed (no TLS implementation)
 * - network.ping_interval_ms: parsed and stored but forced to 0 in client (server drives pings)
 * - ldn.interface_name: parsed and stored but not consumed (always uses auto-detect)
 *
 * ## Bool Parsing
 * Accepts: 0, f, F, n, N → false; anything else → true (1, true, yes, etc.)
 */
Config get_default_config();

/**
 * @brief True for a hostname or dotted-quad IPv4 address
 *
 * A hostname is dot-separated labels of 1 to 63 letters, digits or hyphens
 * that do not start or end with a hyphen. A string of only digits and dots
 * must be a valid IPv4 address. Empty, spaced or over-long values fail.
 */
bool is_valid_server_host(const char* host);

/** @brief Which [server] values validate_server_config() replaced */
struct ServerCheck {
    bool host_reset;  ///< host was invalid and is now DEFAULT_HOST
    bool port_reset;  ///< port was missing or outside 1-65535 and is now DEFAULT_PORT
};

/**
 * @brief Replace an invalid server host or port with the default
 *
 * load_config() stores port 0 for a value that is not 1 to 65535 and an
 * empty host for an over-long one, so both fall back here.
 */
ServerCheck validate_server_config(ServerConfig& server);

/**
 * @brief The mode games actually play in
 *
 * Remote needs a relay server; with no [server] host it falls back to local,
 * so a fresh install works on the same Wi-Fi before any server is set.
 */
LdnMode effective_ldn_mode(const Config& config);

/**
 * @brief Load configuration from INI file
 *
 * Parses an INI file and populates the config structure.
 * Unknown sections and keys are silently ignored.
 * If file doesn't exist, config is unchanged (use defaults).
 *
 * @param path Absolute path to configuration file
 * @param[in,out] config Configuration to populate (should be initialized first)
 * @return ConfigResult indicating success or failure type
 *
 * ## Typical Usage
 * @code
 * Config config = get_default_config();
 * load_config("/atmosphere/contents/4200000000504C4B/config.ini", config);
 * // config now has file values, or defaults if file missing
 * @endcode
 *
 * ## Error Handling
 * - FileNotFound: File doesn't exist - config unchanged
 * - ParseError: File has syntax errors - partial config may be loaded
 * - IoError: Read error - config unchanged
 */
/// @gdb{tag="CONFIG:PARSE", msg="Parsing config content"}
ConfigResult load_config(const char* path, Config& config);

/**
 * @brief Save configuration to INI file
 *
 * Writes the config structure to an INI file.
 * Creates parent directories if they don't exist.
 *
 * @param path Absolute path to configuration file
 * @param config Configuration to save
 * @return ConfigResult indicating success or failure type
 */
ConfigResult save_config(const char* path, const Config& config);

/**
 * @brief Write a fresh config.ini (comments and every key) into buffer
 * @return Bytes written, or 0 when buffer_size is too small
 */
size_t format_config_content(char* buffer, size_t buffer_size, const Config& config);

/**
 * @brief Rewrite existing config.ini text with the values in config
 *
 * Only the lines of keys whose value changed are replaced. Every other
 * byte, including user comments, unknown keys, blank lines and line endings,
 * is copied as is.
 * A key the file lacks is added at the end of its section, and a missing
 * section is added at the end of the file.
 *
 * @return Bytes written, or 0 when buffer_size is too small or a line is
 *         longer than 511 bytes (the loaders read such a line in ways no
 *         rewrite can keep, so the file is left alone)
 */
size_t update_config_text(const char* existing, size_t existing_length, const Config& config,
                          char* buffer, size_t buffer_size);

/**
 * @brief Ensure configuration file exists, create with defaults if not
 *
 * Checks if config file exists. If not, creates it with default values.
 * This should be called on sysmodule startup.
 *
 * @param path Absolute path to configuration file
 * @return ConfigResult indicating success or failure type
 */
ConfigResult ensure_config_exists(const char* path);

/**
 * @brief Like ensure_config_exists, but carry settings over from legacy_path
 *
 * When path is missing and legacy_path parses, the new file is written from
 * the legacy values (server, room code, debug options and all), so a console
 * updated from the old module keeps its settings. The legacy file is left in
 * place for the installer to remove.
 *
 * @param out_migrated Set to true when the settings came from legacy_path
 */
ConfigResult ensure_config_exists(const char* path, const char* legacy_path, bool* out_migrated);

/**
 * @brief Convert ConfigResult to human-readable string
 *
 * @param result ConfigResult enum value
 * @return Static string describing the result
 */
inline const char* config_result_to_string(ConfigResult result) {
    switch (result) {
        case ConfigResult::Success:      return "Success";
        case ConfigResult::FileNotFound: return "FileNotFound";
        case ConfigResult::ParseError:   return "ParseError";
        case ConfigResult::IoError:      return "IoError";
        default:                         return "Unknown";
    }
}

} // namespace potluck::config
