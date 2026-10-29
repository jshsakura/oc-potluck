/**
 * @file config_ipc_service.cpp
 * @brief Standalone IPC service implementation for configuration (potluck)
 *
 * This file implements the potluck IPC service which allows the overlay
 * to communicate with the sysmodule independently of ldn:u MITM service.
 *
 * ## Architecture
 *
 * The potluck service is registered as a standalone service that runs alongside
 * the ldn:u MITM service. This allows:
 * - Overlay to always connect (even when no game is running)
 * - Configuration changes without requiring game restart
 * - Real-time status monitoring
 *
 * ## Thread Safety
 *
 * All configuration access is protected by g_config_mutex. The mutex is held
 * for the duration of each IPC call to ensure consistent reads/writes.
 *
 * ## IPC Protocol
 *
 * Commands are defined in config_ipc_service.hpp with the following conventions:
 * - Get* commands: Read configuration values (no side effects)
 * - Set* commands: Write configuration values (in-memory only until SaveConfig)
 * - SaveConfig: Persist current configuration to SD card
 * - ReloadConfig: Discard in-memory changes and reload from SD card
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "config_ipc_service.hpp"
#include "config.hpp"
#include "../debug/log.hpp"
#include "../ldn/ldn_shared_state.hpp"
#include "../network/socket.hpp"
#include "../network/server_probe.hpp"
#include "../network/relay_traffic.hpp"
#include "../protocol/relay_protocol.hpp"
#include <cstring>
extern "C" {
#include <switch/services/pm.h>
}

namespace potluck::ipc {

// =============================================================================
// Global Configuration State
// =============================================================================

/**
 * @brief Global configuration instance shared between MITM and IPC services
 *
 * This configuration is loaded once at startup and can be modified via IPC.
 * Changes are only persisted when SaveConfig is called.
 */
config::Config g_config;

/**
 * @brief Mutex protecting g_config from concurrent access
 *
 * Must be held when reading or writing any field of g_config.
 */
ams::os::SdkMutex g_config_mutex;
constinit std::atomic<runtime::Phase> g_runtime_phase{runtime::Phase::Initializing};
/// Set by PrepareStop so no new game session is intercepted while the module stops
constinit std::atomic<bool> g_new_clients_blocked{false};

/**
 * @brief Initialize global configuration from file
 *
 * Called once during sysmodule startup. Loads defaults first, then overwrites
 * with values from config.ini if it exists.
 *
 * Thread-safe: Acquires g_config_mutex.
 */
void InitializeConfig() {
    std::scoped_lock lk(g_config_mutex);

    // Load defaults first
    g_config = config::get_default_config();

    // Load from file (overwriting defaults with file values)
    config::load_config(config::CONFIG_PATH, g_config);

    LOG_INFO("Config IPC: Global config initialized");
}

// =============================================================================
// Internal Utilities
// =============================================================================

namespace {

/**
 * @brief Safe string copy with null-termination guarantee
 *
 * Copies up to max_len characters from src to dest, always null-terminating.
 * Unlike strncpy, this guarantees null-termination even if src is longer
 * than max_len.
 *
 * @param dest Destination buffer
 * @param src Source string (null-terminated)
 * @param max_len Maximum characters to copy (excluding null terminator)
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
 * @brief Last server connectivity check result (0=unknown/untested, 3=ready/online, 4=error/offline)
 */

} // anonymous namespace

// =============================================================================
// ConfigService Implementation - Version & Status
// =============================================================================

/**
 * @brief Get the sysmodule version string
 *
 * Returns the current version of the potluck sysmodule.
 * Format: "MAJOR.MINOR.PATCH" (e.g., "0.1.3")
 *
 * @param out Output buffer for version string (32 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::GetVersion(ams::sf::Out<std::array<char, 32>> out) {
#ifdef POTLUCK_VERSION
    static constexpr const char* VERSION = POTLUCK_VERSION;
#else
    static constexpr const char* VERSION = "1.0.0";
#endif

    // Clear output buffer
    std::memset(out->data(), 0, out->size());
    safe_strcpy(out->data(), VERSION, out->size() - 1);

    R_SUCCEED();
}

/**
 * @brief Get the current connection status
 *
 * Returns status code indicating the sysmodule's operational state.
 *
 * Status codes (matching PotluckConnectionStatus in potluck_ipc.h):
 * - 0: Disconnected / Idle
 * - 1: Connecting
 * - 2: Connected
 * - 3: Ready
 * - 4: Error
 *
 * @param out Output status code
 * @return Always succeeds
 */
ams::Result ConfigService::GetConnectionStatus(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    const auto ldn_state = shared_state.GetLdnState();

    switch (ldn_state) {
        case ams::mitm::ldn::CommState::AccessPointCreated:
        case ams::mitm::ldn::CommState::StationConnected:
            *out = 3; // PotluckStatus_Ready
            break;
        case ams::mitm::ldn::CommState::AccessPoint:
        case ams::mitm::ldn::CommState::Station:
            *out = 2; // PotluckStatus_Connected
            break;
        case ams::mitm::ldn::CommState::Initialized:
            *out = 1; // PotluckStatus_Connecting
            break;
        case ams::mitm::ldn::CommState::Error:
            *out = 4; // PotluckStatus_Error
            break;
        case ams::mitm::ldn::CommState::None:
        default:
            *out = 0;
            break;
    }

    R_SUCCEED();
}

/**
 * @brief Check if the IPC service is active
 *
 * Simple ping to verify the service is responding. If this call succeeds,
 * the sysmodule is loaded and the IPC service is operational.
 *
 * @param out Output: 1 = active, 0 = inactive (never returned)
 * @return Always succeeds
 */
ams::Result ConfigService::IsServiceActive(ams::sf::Out<u32> out) {
    // If we're executing this, the service is active
    *out = 1;

    LOG_VERBOSE("Config IPC: IsServiceActive -> 1");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - LDN Settings
// =============================================================================

/**
 * @brief Get the current room passphrase
 *
 * Returns the passphrase used to filter LDN rooms. Empty string means
 * public/no filtering.
 *
 * @param out Output buffer for passphrase (64 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::GetPassphrase(ams::sf::Out<std::array<char, 64>> out) {
    std::scoped_lock lk(g_config_mutex);

    std::memset(out->data(), 0, out->size());
    safe_strcpy(out->data(), g_config.ldn.passphrase, out->size() - 1);

    R_SUCCEED();
}

/**
 * @brief Set the room passphrase
 *
 * Changes the passphrase in memory. Call SaveConfig to persist.
 *
 * @param passphrase New passphrase (64 bytes, null-terminated)
 * @return Always succeeds
 */
ams::Result ConfigService::SetPassphrase(std::array<char, 64> passphrase) {
    std::scoped_lock lk(g_config_mutex);

    safe_strcpy(g_config.ldn.passphrase, passphrase.data(), config::MAX_PASSPHRASE_LENGTH);

    LOG_INFO("Config IPC: SetPassphrase -> '%s'", g_config.ldn.passphrase);
    R_SUCCEED();
}

bool IsInterceptAllowed() {
    return runtime::CanIntercept(!g_new_clients_blocked.load(), g_runtime_phase.load());
}

config::LdnMode GetLdnMode() {
    std::scoped_lock lk(g_config_mutex);
    return config::effective_ldn_mode(g_config);
}

/// The mode as chosen; the overlay shows remote without a server address as such
ams::Result ConfigService::GetLdnMode(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);
    *out = static_cast<u32>(g_config.ldn.mode);
    R_SUCCEED();
}

/**
 * @brief Choose remote, local or off for games started from now on
 *
 * Changes the setting in memory. Call SaveConfig to persist. A running game
 * keeps the mode it started with.
 */
ams::Result ConfigService::SetLdnMode(u32 mode) {
    R_UNLESS(mode <= static_cast<u32>(config::LdnMode::Off), ams::sf::ResultPreconditionViolation());
    std::scoped_lock lk(g_config_mutex);
    g_config.ldn.mode = static_cast<config::LdnMode>(mode);
    LOG_INFO("Config IPC: SetLdnMode -> %u (0 remote, 1 local, 2 off)", mode);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Server Settings
// =============================================================================

/**
 * @brief Get the server address (host and port)
 *
 * Returns the relay server address currently configured.
 *
 * @param out Output structure with host (64 bytes) and port (u16)
 * @return Always succeeds
 */
ams::Result ConfigService::GetServerAddress(ams::sf::Out<ServerAddressIpc> out) {
    std::scoped_lock lk(g_config_mutex);

    std::memset(&(*out), 0, sizeof(ServerAddressIpc));
    safe_strcpy(out->host, g_config.server.host, sizeof(out->host) - 1);
    out->port = g_config.server.port;

    R_SUCCEED();
}

/**
 * @brief Set the server address (host and port)
 *
 * Changes the server address in memory. Call SaveConfig to persist.
 * Requires restart/reconnect to take effect.
 *
 * @param address New server address structure
 * @return PreconditionViolation for a host that is not a hostname or IPv4
 *         address, or port 0; the stored address is then left unchanged
 */
ams::Result ConfigService::SetServerAddress(const ServerAddressIpc &address) {
    char host[sizeof(address.host)];
    safe_strcpy(host, address.host, sizeof(address.host) - 1);
    // An empty host clears the server: remote then plays as local
    if ((host[0] != '\0' && !potluck::config::is_valid_server_host(host)) || address.port == 0) {
        LOG_WARN("Config IPC: SetServerAddress rejected invalid %s:%u", host, address.port);
        return ams::sf::ResultPreconditionViolation();
    }

    std::scoped_lock lk(g_config_mutex);
    safe_strcpy(g_config.server.host, host, sizeof(host) - 1);
    g_config.server.port = address.port;

    LOG_INFO("Config IPC: SetServerAddress -> %s:%u", g_config.server.host, g_config.server.port);
    R_SUCCEED();
}

/**
 * @brief Check if TLS is enabled for server connection
 *
 * @param out Output: 1 = TLS enabled, 0 = plaintext
 * @return Always succeeds
 */
ams::Result ConfigService::GetUseTls(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.server.use_tls ? 1 : 0;

    R_SUCCEED();
}

/**
 * @brief Enable or disable TLS for server connection
 *
 * Changes the setting in memory. Call SaveConfig to persist.
 * Requires restart/reconnect to take effect.
 *
 * @param enabled 1 = enable TLS, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetUseTls(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.server.use_tls = (enabled != 0);

    LOG_INFO("Config IPC: SetUseTls -> %s", g_config.server.use_tls ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Debug Settings
// =============================================================================

/**
 * @brief Check if debug logging is enabled
 *
 * @param out Output: 1 = enabled, 0 = disabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetDebugEnabled(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.enabled ? 1 : 0;

    R_SUCCEED();
}

/**
 * @brief Enable or disable debug logging
 *
 * @param enabled 1 = enable, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetDebugEnabled(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.enabled = (enabled != 0);
    debug::g_logger.apply(g_config.debug.enabled, g_config.debug.level);

    LOG_INFO("Config IPC: SetDebugEnabled -> %s", g_config.debug.enabled ? "true" : "false");
    R_SUCCEED();
}

/**
 * @brief Get the current debug log level
 *
 * Log levels:
 * - 0: Error only
 * - 1: Warning and above
 * - 2: Info and above
 * - 3: Verbose (all messages)
 *
 * @param out Output log level (0-3)
 * @return Always succeeds
 */
ams::Result ConfigService::GetDebugLevel(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.level;

    R_SUCCEED();
}

/**
 * @brief Set the debug log level
 *
 * @param level New log level (0-3)
 * @return Always succeeds
 */
ams::Result ConfigService::SetDebugLevel(u32 level) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.level = level;
    debug::g_logger.apply(g_config.debug.enabled, g_config.debug.level);

    LOG_INFO("Config IPC: SetDebugLevel -> %u", level);
    R_SUCCEED();
}

/**
 * @brief Check if file logging is enabled
 *
 * When enabled, logs are written to SD card at /config/potluck/potluck.log
 *
 * @param out Output: 1 = enabled, 0 = disabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetLogToFile(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.debug.log_to_file ? 1 : 0;

    R_SUCCEED();
}

/**
 * @brief Enable or disable file logging
 *
 * @param enabled 1 = enable, 0 = disable
 * @return Always succeeds
 */
ams::Result ConfigService::SetLogToFile(u32 enabled) {
    std::scoped_lock lk(g_config_mutex);

    g_config.debug.log_to_file = (enabled != 0);

    LOG_INFO("Config IPC: SetLogToFile -> %s", g_config.debug.log_to_file ? "true" : "false");
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Network Timeouts
// =============================================================================

/**
 * @brief Get the connection timeout in milliseconds
 *
 * Maximum time to wait when establishing connection to server.
 *
 * @param out Output timeout in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetConnectTimeout(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.network.connect_timeout_ms;

    R_SUCCEED();
}

/**
 * @brief Set the connection timeout in milliseconds
 *
 * @param timeout_ms New timeout value
 * @return Always succeeds
 */
ams::Result ConfigService::SetConnectTimeout(u32 timeout_ms) {
    std::scoped_lock lk(g_config_mutex);

    g_config.network.connect_timeout_ms = timeout_ms;

    LOG_INFO("Config IPC: SetConnectTimeout -> %u ms", timeout_ms);
    R_SUCCEED();
}

/**
 * @brief Get the ping interval in milliseconds
 *
 * How often to send keepalive pings to the server.
 *
 * @param out Output interval in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetPingInterval(ams::sf::Out<u32> out) {
    std::scoped_lock lk(g_config_mutex);

    *out = g_config.network.ping_interval_ms;

    R_SUCCEED();
}

/**
 * @brief Set the ping interval in milliseconds
 *
 * @param interval_ms New interval value
 * @return Always succeeds
 */
ams::Result ConfigService::SetPingInterval(u32 interval_ms) {
    std::scoped_lock lk(g_config_mutex);

    g_config.network.ping_interval_ms = interval_ms;

    LOG_INFO("Config IPC: SetPingInterval -> %u ms", interval_ms);
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - File Operations
// =============================================================================

/**
 * @brief Save current configuration to SD card
 *
 * Writes the current in-memory configuration to /atmosphere/contents/4200000000504C4B/config.ini.
 * This persists any changes made via Set* commands.
 *
 * @param out Output result code:
 *            - Success (0): Config saved successfully
 *            - FileNotFound (1): Unexpected (directory creation failed)
 *            - ParseError (2): Not applicable for save
 *            - IoError (3): Write failed
 *            - InvalidValue (4): Not applicable for save
 * @return Always succeeds (check out for actual result)
 */
ams::Result ConfigService::SaveConfig(ams::sf::Out<ConfigResult> out) {
    std::scoped_lock lk(g_config_mutex);

    config::ConfigResult result = config::save_config(config::CONFIG_PATH, g_config);
    *out = static_cast<ConfigResult>(static_cast<u32>(result));

    LOG_INFO("Config IPC: SaveConfig -> result=%u", static_cast<u32>(*out));
    R_SUCCEED();
}

/**
 * @brief Reload configuration from SD card
 *
 * Discards any unsaved in-memory changes and reloads from config.ini.
 * Useful to revert changes or pick up external modifications.
 *
 * @param out Output result code:
 *            - Success (0): Config reloaded successfully
 *            - FileNotFound (1): Config file doesn't exist
 *            - ParseError (2): Config file has syntax errors
 *            - IoError (3): Read failed
 *            - InvalidValue (4): Invalid config value
 * @return Always succeeds (check out for actual result)
 */
ams::Result ConfigService::ReloadConfig(ams::sf::Out<ConfigResult> out) {
    // Read into a copy: a file that cannot be read must not wipe the live settings
    static config::Config loaded;
    loaded = config::get_default_config();
    const config::ConfigResult result = config::load_config(config::CONFIG_PATH, loaded);
    if (result == config::ConfigResult::Success || result == config::ConfigResult::FileNotFound) {
        const auto check = config::validate_server_config(loaded.server);
        if (check.host_reset || check.port_reset) LOG_WARN("Config IPC: ReloadConfig replaced an invalid server address");
        std::scoped_lock lk(g_config_mutex);
        g_config = loaded;
        debug::g_logger.apply(g_config.debug.enabled, g_config.debug.level);
    }
    *out = static_cast<ConfigResult>(static_cast<u32>(result));

    LOG_INFO("Config IPC: ReloadConfig -> result=%u", static_cast<u32>(*out));
    R_SUCCEED();
}

// =============================================================================
// ConfigService Implementation - Runtime LDN State
// =============================================================================

/**
 * @brief Check if a game is actively using LDN
 *
 * Returns 1 if a game has initialized the LDN service (ldn:u),
 * 0 otherwise. Used by overlay to determine what UI to show.
 *
 * @param out Output: 1 = game active, 0 = no game
 * @return Always succeeds
 */
ams::Result ConfigService::IsGameActive(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.IsGameActive() ? 1 : 0;

    LOG_VERBOSE("Config IPC: IsGameActive -> %u", *out);
    R_SUCCEED();
}

/**
 * @brief Get the current LDN communication state
 *
 * Returns the CommState enum value representing current LDN state:
 * - 0: None (not initialized)
 * - 1: Initialized
 * - 2: AccessPoint
 * - 3: AccessPointCreated
 * - 4: Station
 * - 5: StationConnected
 * - 6: Error
 *
 * @param out Output state value (0-6)
 * @return Always succeeds
 */
ams::Result ConfigService::GetLdnState(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = static_cast<u32>(shared_state.GetLdnState());

    R_SUCCEED();
}

/**
 * @brief Get session information
 *
 * Returns current session info: node count, max nodes, local node ID,
 * and whether this node is the host.
 *
 * @param out Output SessionInfoIpc structure
 * @return Always succeeds
 */
ams::Result ConfigService::GetSessionInfo(ams::sf::Out<SessionInfoIpc> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();

    // Get session info from SharedState
    ams::mitm::ldn::SessionInfo info = shared_state.GetSessionInfoStruct();

    // Copy to IPC structure
    out->node_count = info.node_count;
    out->max_nodes = info.node_count_max;
    out->local_node_id = info.local_node_id;
    out->is_host = info.is_host;
    std::memset(out->reserved, 0, sizeof(out->reserved));

    R_SUCCEED();
}

/**
 * @brief Get last measured RTT
 *
 * Returns the last round-trip time measurement in milliseconds.
 * 0 means no RTT has been measured yet.
 *
 * @param out Output RTT in milliseconds
 * @return Always succeeds
 */
ams::Result ConfigService::GetLastRtt(ams::sf::Out<u32> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.GetLastRtt();

    R_SUCCEED();
}

/**
 * @brief Request reconnection
 *
 * Sets a flag that the MITM service will check to trigger a reconnect.
 * Useful when network conditions change or connection is lost.
 *
 * @return Always succeeds
 */
ams::Result ConfigService::ForceReconnect() {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    shared_state.RequestReconnect();

    LOG_INFO("Config IPC: ForceReconnect requested");
    R_SUCCEED();
}

/**
 * @brief Get the process ID of the active game
 *
 * Returns the process ID of the game currently using LDN.
 * Useful for debugging and logging.
 *
 * @param out Output process ID (0 if no game active)
 * @return Always succeeds
 */
ams::Result ConfigService::GetActiveProcessId(ams::sf::Out<u64> out) {
    auto& shared_state = ams::mitm::ldn::SharedState::GetInstance();
    *out = shared_state.GetActiveProcessId();

    R_SUCCEED();
}

// ============================================================================
// P2P Proxy Control
// ============================================================================

/**
 * @brief Get P2P proxy disabled state
 *
 * Returns whether P2P proxy is disabled (like the reference client MultiplayerDisableP2p).
 *
 * @param out Output: 1 if disabled, 0 if enabled
 * @return Always succeeds
 */
ams::Result ConfigService::GetDisableP2p(ams::sf::Out<u32> out) {
    std::scoped_lock lock(g_config_mutex);
    *out = g_config.ldn.disable_p2p ? 1 : 0;

    R_SUCCEED();
}

/**
 * @brief Set P2P proxy disabled state
 *
 * Sets whether P2P proxy should be disabled (like the reference client MultiplayerDisableP2p).
 * When disabled, the server will not attempt P2P connections.
 *
 * @param disabled 1 to disable P2P, 0 to enable
 * @return Always succeeds
 */
ams::Result ConfigService::SetDisableP2p(u32 disabled) {
    LOG_INFO("Config IPC: SetDisableP2p(%u)", disabled);

    std::scoped_lock lock(g_config_mutex);
    g_config.ldn.disable_p2p = (disabled != 0);

    R_SUCCEED();
}

// ============================================================================
// Server Connectivity Probe
// ============================================================================

ams::Result ConfigService::TestServer(ams::sf::Out<ServerTestResultIpc> out) {
    char host[config::MAX_HOST_LENGTH + 1]{};
    u16 port = 0;
    char passphrase[config::MAX_PASSPHRASE_LENGTH + 1]{};

    {
        std::scoped_lock lock(g_config_mutex);
        safe_strcpy(host, g_config.server.host, config::MAX_HOST_LENGTH);
        port = g_config.server.port;
        safe_strcpy(passphrase, g_config.ldn.passphrase, config::MAX_PASSPHRASE_LENGTH);
    }

    ServerTestResultIpc res{};
    res.status = static_cast<u32>(ServerTestStatus::Error);
    res.rtt_ms = 0;

    if (host[0] == '\0' || port == 0) {
        res.status = static_cast<u32>(ServerTestStatus::InvalidAddress);
        *out = res;
        LOG_WARN("Config IPC: TestServer failed (empty host or port 0)");
        R_SUCCEED();
    }

    potluck::network::socket_init();

    const ams::os::Tick start_tick = ams::os::GetSystemTick();
    potluck::network::Socket sock;

    constexpr uint32_t CONNECT_TIMEOUT_MS = 2500;
    const auto conn_res = sock.connect(host, port, CONNECT_TIMEOUT_MS);
    if (conn_res != potluck::network::SocketResult::Success) {
        switch (conn_res) {
            case potluck::network::SocketResult::Timeout:
            case potluck::network::SocketResult::WouldBlock:
                res.status = static_cast<u32>(ServerTestStatus::Timeout);
                break;
            case potluck::network::SocketResult::ConnectionRefused:
            case potluck::network::SocketResult::ConnectionReset:
                res.status = static_cast<u32>(ServerTestStatus::Refused);
                break;
            case potluck::network::SocketResult::HostUnreachable:
            case potluck::network::SocketResult::NetworkDown:
                res.status = static_cast<u32>(ServerTestStatus::Unreachable);
                break;
            case potluck::network::SocketResult::InvalidAddress:
                res.status = static_cast<u32>(ServerTestStatus::InvalidAddress);
                break;
            default:
                res.status = static_cast<u32>(ServerTestStatus::Error);
                break;
        }
        *out = res;
        LOG_WARN("Config IPC: TestServer %s:%u connect failed (%s)",
                 host, port, potluck::network::socket_result_to_string(conn_res));
        R_SUCCEED();
    }

    sock.set_nodelay(true);

    // Send LDN Passphrase + Initialize probe packets to verify server protocol and measure RTT
    uint8_t pkt_buf[160]{};
    size_t pkt_size = 0;

    const size_t pass_len = std::strlen(passphrase);
    if (potluck::protocol::encode_passphrase(
            pkt_buf, sizeof(pkt_buf),
            reinterpret_cast<const uint8_t*>(passphrase), pass_len, pkt_size) !=
            potluck::protocol::EncodeResult::Success ||
        sock.send_all(pkt_buf, pkt_size) != potluck::network::SocketResult::Success) {
        sock.close();
        res.status = static_cast<u32>(ServerTestStatus::Error);
        *out = res;
        LOG_WARN("Config IPC: TestServer %s:%u failed sending Passphrase", host, port);
        R_SUCCEED();
    }

    potluck::protocol::SessionId probe_id{};
    for (size_t i = 0; i < sizeof(probe_id.data); i++) {
        probe_id.data[i] = static_cast<uint8_t>(i ^ 0x5A);
    }
    potluck::protocol::MacAddress probe_mac{{0x02, 0x00, 0x5E, 0x00, 0x53, 0x01}};

    if (potluck::protocol::encode_initialize(
            pkt_buf, sizeof(pkt_buf), probe_id, probe_mac, pkt_size) !=
            potluck::protocol::EncodeResult::Success ||
        sock.send_all(pkt_buf, pkt_size) != potluck::network::SocketResult::Success) {
        sock.close();
        res.status = static_cast<u32>(ServerTestStatus::Error);
        *out = res;
        LOG_WARN("Config IPC: TestServer %s:%u failed sending Initialize", host, port);
        R_SUCCEED();
    }

    const auto reply = potluck::network::ReceiveProbeReply(sock);
    sock.close();
    if (reply != potluck::network::ProbeReplyResult::Success) {
        res.status = static_cast<u32>(reply == potluck::network::ProbeReplyResult::Timeout
                                         ? ServerTestStatus::Timeout : ServerTestStatus::ProtocolError);
        *out = res;
        LOG_WARN("Config IPC: TestServer %s:%u incomplete or invalid reply", host, port);
        R_SUCCEED();
    }

    const s64 elapsed_ms =
        ams::os::ConvertToTimeSpan(ams::os::GetSystemTick() - start_tick).GetMilliSeconds();
    const u32 rtt_ms = (elapsed_ms > 0) ? static_cast<u32>(elapsed_ms) : 1u;

    res.status = static_cast<u32>(ServerTestStatus::Success);
    res.rtt_ms = rtt_ms;

    *out = res;
    LOG_INFO("Config IPC: TestServer %s:%u -> Online (%u ms)", host, port, rtt_ms);
    R_SUCCEED();
}

ams::Result ConfigService::GetRuntimePhase(ams::sf::Out<u32> out) {
    *out = static_cast<u32>(g_runtime_phase.load());
    R_SUCCEED();
}

ams::Result ConfigService::GetRelayTraffic(ams::sf::Out<RelayTrafficIpc> out) {
    const auto t = potluck::network::traffic::Snapshot();
    RelayTrafficIpc ipc{};
    ipc.data_sent = t.data_sent;
    ipc.data_received = t.data_received;
    ipc.control_sent = t.control_sent;
    ipc.control_received = t.control_received;
    ipc.data_bytes_sent = t.data_bytes_sent;
    ipc.data_bytes_received = t.data_bytes_received;
    ipc.last_received = t.last_received;
    *out = ipc;
    R_SUCCEED();
}

ams::Result ConfigService::PrepareStop(ams::sf::Out<u32> out) {
    struct Backend {
        bool BlockNewClients() { return !g_new_clients_blocked.exchange(true); }
        void RestoreEnabled(bool enabled) { g_new_clients_blocked = !enabled; }
        runtime::StopResult CheckGame() {
            if (ams::mitm::ldn::SharedState::GetInstance().IsGameActive()) return runtime::StopResult::GameActive;
            if (R_FAILED(::pmdmntInitialize())) return runtime::StopResult::RebootRequired;
            u64 pid = 0;
            const auto rc = ::pmdmntGetApplicationProcessId(&pid);
            ::pmdmntExit();
            if (rc == 0x20F || (R_SUCCEEDED(rc) && pid == 0)) return runtime::StopResult::Prepared;
            return R_SUCCEEDED(rc) ? runtime::StopResult::GameActive : runtime::StopResult::RebootRequired;
        }
        bool StopWorkers() { return ams::mitm::StopRuntimeMitmWorkers(); }
        ams::sm::ServiceName Name(int index) {
            return index == 0 ? ams::sm::ServiceName::Encode("ldn:u") : ams::sm::ServiceName::Encode("bsd:u");
        }
        bool Detach(int index) {
            bool installed = false;
            if (R_FAILED(ams::sm::mitm::HasMitm(&installed, Name(index)))) return false;
            return !installed || R_SUCCEEDED(ams::sm::mitm::UninstallMitm(Name(index)));
        }
        bool IsDetached(int index) {
            bool installed = true;
            return R_SUCCEEDED(ams::sm::mitm::HasMitm(&installed, Name(index))) && !installed;
        }
    } backend;
    const auto initialized = ams::sm::Initialize();
    if (R_FAILED(initialized)) {
        *out = static_cast<u32>(runtime::StopResult::RebootRequired);
        R_SUCCEED();
    }
    *out = static_cast<u32>(runtime::PrepareStop(g_runtime_phase, backend));
    R_DISCARD(ams::sm::Finalize());
    R_SUCCEED();
}

} // namespace potluck::ipc
