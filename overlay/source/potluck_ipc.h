/**
 * @file potluck_ipc.h
 * @brief IPC client for potluck sysmodule
 *
 * Provides functions to communicate with the potluck sysmodule
 * for retrieving status information and changing configuration.
 *
 * ## Service Name
 * The sysmodule exposes a standalone IPC service: potluck
 *
 * ## IPC Command IDs (potluck service)
 *
 * | ID | Command            | Description                       |
 * |----|--------------------|-----------------------------------|
 * | 0  | GetVersion         | Get sysmodule version string      |
 * | 1  | GetConnectionStatus| Get current connection state      |
 * | 2  | GetPassphrase      | Get room passphrase               |
 * | 3  | SetPassphrase      | Set room passphrase               |
 * | 4  | GetServerAddress   | Get server host and port          |
 * | 5  | SetServerAddress   | Set server host and port          |
 * | 6  | GetLdnMode         | Remote (0), local (1) or off (2)  |
 * | 7  | SetLdnMode         | Choose remote, local or off       |
 * | 8  | GetUseTls          | Check TLS encryption state        |
 * | 9  | SetUseTls          | Toggle TLS encryption             |
 * | 10 | GetDebugEnabled    | Check debug logging state         |
 * | 11 | SetDebugEnabled    | Toggle debug logging              |
 * | 12 | GetDebugLevel      | Get log verbosity (0-3)           |
 * | 13 | SetDebugLevel      | Set log verbosity                 |
 * | 14 | GetLogToFile       | Check file logging state          |
 * | 15 | SetLogToFile       | Toggle file logging               |
 * | 16 | SaveConfig         | Persist config to SD card         |
 * | 17 | ReloadConfig       | Reload config from SD card        |
 * | 18 | GetConnectTimeout  | Get connection timeout (ms)       |
 * | 19 | SetConnectTimeout  | Set connection timeout            |
 * | 20 | GetPingInterval    | Get keepalive interval (ms)       |
 * | 21 | SetPingInterval    | Set keepalive interval            |
 * | 22 | IsServiceActive    | Ping to check service is running  |
 * | 23 | IsGameActive       | Check if game is using LDN        |
 * | 24 | GetLdnState        | Get current LDN CommState (0-6)   |
 * | 25 | GetSessionInfo     | Get session info struct (8 bytes) |
 * | 26 | GetLastRtt         | Get last measured RTT (ms)        |
 * | 27 | ForceReconnect     | Request MITM to reconnect         |
 * | 28 | GetActiveProcessId | Get PID of active game (debug)    |
 * | 29 | GetDisableP2p      | Check if P2P proxy is disabled    |
 * | 30 | SetDisableP2p      | Toggle P2P proxy disabled state   |
 * | 31 | TestServer         | Probe server (TCP + Initialize)   |
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connection status enumeration
 */
typedef enum {
    PotluckStatus_Disconnected = 0,   ///< Not connected to server
    PotluckStatus_Connecting = 1,     ///< Connection in progress
    PotluckStatus_Connected = 2,      ///< Connected, handshake pending
    PotluckStatus_Ready = 3,          ///< Fully connected and ready
    PotluckStatus_Error = 4,          ///< Connection error
} PotluckConnectionStatus;

/**
 * @brief LDN communication state
 *
 * Mirrors the CommState enum from the sysmodule.
 */
typedef enum {
    PotluckLdnState_None = 0,               ///< Not initialized
    PotluckLdnState_Initialized = 1,        ///< Initialized, ready to open AP or Station
    PotluckLdnState_AccessPoint = 2,        ///< Access point mode, ready to create network
    PotluckLdnState_AccessPointCreated = 3, ///< Network created, accepting connections
    PotluckLdnState_Station = 4,            ///< Station mode, ready to scan/connect
    PotluckLdnState_StationConnected = 5,   ///< Connected to a network
    PotluckLdnState_Error = 6,              ///< Error state
} PotluckLdnState;

/**
 * @brief Session information structure
 *
 * Contains runtime information about the current LDN session.
 */
typedef struct {
    u8 node_count;      ///< Current number of nodes in session
    u8 max_nodes;       ///< Maximum nodes allowed in session
    u8 local_node_id;   ///< This node's ID in the session
    u8 is_host;         ///< 1 if this node is the host, 0 otherwise
    u8 reserved[4];     ///< Reserved for future use
} PotluckSessionInfo;

/**
 * @brief What the module and the relay server exchanged since the connection opened
 *
 * Game data is ProxyData; control is every other packet.
 */
typedef struct {
    u32 data_sent;
    u32 data_received;
    u32 control_sent;
    u32 control_received;
    u64 data_bytes_sent;
    u64 data_bytes_received;
    u8 last_received;   ///< PacketId of the newest control packet from the server, 0xFF for none
    u8 reserved[7];
} PotluckRelayTraffic;

/**
 * @brief Server connectivity check status codes
 */
typedef enum {
    PotluckServerTest_Success        = 0, ///< TCP + LDN Initialize reply OK
    PotluckServerTest_Timeout        = 1, ///< Connection or handshake timed out
    PotluckServerTest_Refused        = 2, ///< Connection refused or reset
    PotluckServerTest_Unreachable    = 3, ///< Network down or host unreachable
    PotluckServerTest_InvalidAddress = 4, ///< Invalid server host/IP
    PotluckServerTest_ProtocolError  = 5, ///< Connected, but invalid LDN reply
    PotluckServerTest_Error          = 6, ///< Generic socket error
} PotluckServerTestStatus;

/**
 * @brief Result structure for potluckTestServer
 */
typedef struct {
    u32 status;  ///< PotluckServerTestStatus code
    u32 rtt_ms;  ///< Round-trip latency in ms (when status == 0)
} PotluckServerTestResult;

/**
 * @brief Configuration service handle
 */
typedef struct {
    Service s;
} PotluckService;

/**
 * @brief Initialize connection to potluck service
 *
 * This opens a connection to the standalone potluck service.
 * Call potluckExit() when done.
 *
 * @return Result code (0 on success)
 */
Result potluckInitialize(void);

/**
 * @brief Close connection to potluck service
 */
void potluckExit(void);

/**
 * @brief Get the configuration service handle
 *
 * @return Pointer to internal service (valid after potluckInitialize)
 */
PotluckService* potluckGetService(void);

/**
 * @brief Check if sysmodule is active
 *
 * @param active Output: 1 if active, 0 otherwise
 * @return Result code
 */
Result potluckIsServiceActive(PotluckService* s, u32* active);

/**
 * @brief Get sysmodule version string
 *
 * @param s Configuration service
 * @param version Output buffer (at least 32 bytes)
 * @return Result code
 */
Result potluckGetVersion(PotluckService* s, char* version);

/**
 * @brief Get current connection status
 *
 * @param s Configuration service
 * @param status Output status
 * @return Result code
 */
Result potluckGetConnectionStatus(PotluckService* s, PotluckConnectionStatus* status);

/**
 * @brief Get configured server address
 *
 * @param s Configuration service
 * @param host Output host buffer (at least 64 bytes)
 * @param port Output port
 * @return Result code
 */
Result potluckGetServerAddress(PotluckService* s, char* host, u16* port);

/**
 * @brief Set server address
 *
 * @param s Configuration service
 * @param host Server hostname
 * @param port Server port
 * @return Result code
 */
Result potluckSetServerAddress(PotluckService* s, const char* host, u16 port);

/**
 * @brief Get debug logging state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result potluckGetDebugEnabled(PotluckService* s, u32* enabled);

/**
 * @brief Enable/disable debug logging
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result potluckSetDebugEnabled(PotluckService* s, u32 enabled);

//=============================================================================
// Configuration Commands
//=============================================================================

/**
 * @brief Config operation result
 */
typedef enum {
    PotluckResult_Success = 0,
    PotluckResult_FileNotFound = 1,
    PotluckResult_ParseError = 2,
    PotluckResult_IoError = 3,
    PotluckResult_InvalidValue = 4,
} PotluckResult;

/**
 * @brief Get passphrase
 *
 * @param s Configuration service
 * @param passphrase Output buffer (at least 64 bytes)
 * @return Result code
 */
Result potluckGetPassphrase(PotluckService* s, char* passphrase);

/**
 * @brief Set passphrase
 *
 * @param s Configuration service
 * @param passphrase Passphrase string (max 63 chars)
 * @return Result code
 */
Result potluckSetPassphrase(PotluckService* s, const char* passphrase);

/** @brief LDN modes for potluckGetLdnMode / potluckSetLdnMode */
typedef enum {
    PotluckLdnMode_Remote = 0,  ///< Rooms through the relay server
    PotluckLdnMode_Local = 1,   ///< Rooms on the same Wi-Fi network (ldn_mitm compatible)
    PotluckLdnMode_Off = 2,     ///< Games bypass PotLuck; fully off after a reboot
} PotluckLdnMode;

/**
 * @brief Get the LDN mode
 *
 * @param s Configuration service
 * @param mode Output: a PotluckLdnMode
 * @return Result code
 */
Result potluckGetLdnMode(PotluckService* s, u32* mode);

/**
 * @brief Set the LDN mode for games started from now on
 *
 * @param s Configuration service
 * @param mode A PotluckLdnMode; other values are refused
 * @return Result code
 */
Result potluckSetLdnMode(PotluckService* s, u32 mode);

/**
 * @brief Get TLS enabled state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result potluckGetUseTls(PotluckService* s, u32* enabled);

/**
 * @brief Set TLS enabled state
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result potluckSetUseTls(PotluckService* s, u32 enabled);

/**
 * @brief Get connect timeout in milliseconds
 *
 * @param s Configuration service
 * @param timeout_ms Output timeout value
 * @return Result code
 */
Result potluckGetConnectTimeout(PotluckService* s, u32* timeout_ms);

/**
 * @brief Set connect timeout in milliseconds
 *
 * @param s Configuration service
 * @param timeout_ms Timeout value
 * @return Result code
 */
Result potluckSetConnectTimeout(PotluckService* s, u32 timeout_ms);

/**
 * @brief Get ping interval in milliseconds
 *
 * @param s Configuration service
 * @param interval_ms Output interval value
 * @return Result code
 */
Result potluckGetPingInterval(PotluckService* s, u32* interval_ms);

/**
 * @brief Set ping interval in milliseconds
 *
 * @param s Configuration service
 * @param interval_ms Interval value
 * @return Result code
 */
Result potluckSetPingInterval(PotluckService* s, u32 interval_ms);

/**
 * @brief Get debug level (0-3)
 *
 * @param s Configuration service
 * @param level Output level (0=Error, 1=Warn, 2=Info, 3=Verbose)
 * @return Result code
 */
Result potluckGetDebugLevel(PotluckService* s, u32* level);

/**
 * @brief Set debug level (0-3)
 *
 * @param s Configuration service
 * @param level Level (0=Error, 1=Warn, 2=Info, 3=Verbose)
 * @return Result code
 */
Result potluckSetDebugLevel(PotluckService* s, u32 level);

/**
 * @brief Get log to file state
 *
 * @param s Configuration service
 * @param enabled Output: 1 if enabled, 0 if disabled
 * @return Result code
 */
Result potluckGetLogToFile(PotluckService* s, u32* enabled);

/**
 * @brief Set log to file state
 *
 * @param s Configuration service
 * @param enabled 1 to enable, 0 to disable
 * @return Result code
 */
Result potluckSetLogToFile(PotluckService* s, u32 enabled);

/**
 * @brief Save configuration to file
 *
 * @param s Configuration service
 * @param result Output operation result
 * @return Result code
 */
Result potluckSaveConfig(PotluckService* s, PotluckResult* result);

/**
 * @brief Reload configuration from file
 *
 * @param s Configuration service
 * @param result Output operation result
 * @return Result code
 */
Result potluckReloadConfig(PotluckService* s, PotluckResult* result);

//=============================================================================
// Runtime LDN State Commands (23-28)
//=============================================================================

/**
 * @brief Check if a game is actively using LDN
 *
 * @param s Configuration service
 * @param active Output: 1 if game is active, 0 otherwise
 * @return Result code
 */
Result potluckIsGameActive(PotluckService* s, u32* active);

/**
 * @brief Get current LDN communication state
 *
 * @param s Configuration service
 * @param state Output LDN state
 * @return Result code
 */
Result potluckGetLdnState(PotluckService* s, PotluckLdnState* state);

/**
 * @brief Get session information
 *
 * @param s Configuration service
 * @param info Output session info structure
 * @return Result code
 */
Result potluckGetSessionInfo(PotluckService* s, PotluckSessionInfo* info);

/**
 * @brief Get last measured round-trip time
 *
 * @param s Configuration service
 * @param rtt_ms Output RTT in milliseconds
 * @return Result code
 */
Result potluckGetLastRtt(PotluckService* s, u32* rtt_ms);

/**
 * @brief Request the MITM service to reconnect
 *
 * @param s Configuration service
 * @return Result code
 */
Result potluckForceReconnect(PotluckService* s);

/**
 * @brief Get process ID of the active game
 *
 * @param s Configuration service
 * @param pid Output process ID (0 if no game active)
 * @return Result code
 */
Result potluckGetActiveProcessId(PotluckService* s, u64* pid);

/**
 * @brief Actively probe the configured the relay server
 *
 * Opens a short-lived TCP connection to the configured server, performs a
 * LDN Initialize handshake, measures round-trip time, and closes the socket.
 *
 * @param s Configuration service
 * @param result Output probe status and RTT in milliseconds
 * @return Result code
 */
Result potluckTestServer(PotluckService* s, PotluckServerTestResult* result);

/** @brief 1 when the P2P proxy is off (relay only), 0 when it is on */
Result potluckGetDisableP2p(PotluckService* s, u32* disabled);

/** @brief Turn the P2P proxy off (1) or on (0) for rooms created from now on */
Result potluckSetDisableP2p(PotluckService* s, u32 disabled);
/** @brief Prepare this module for safe external termination; status0 means detached. */
Result potluckPrepareStop(PotluckService* s, u32* status);
/** @brief Query the module's runtime phase (initializing0, ready1, detached3). */
Result potluckGetRuntimePhase(PotluckService* s, u32* phase);
/** @brief What the module and the relay server exchanged since the connection opened. */
Result potluckGetRelayTraffic(PotluckService* s, PotluckRelayTraffic* traffic);

/**
 * @brief Convert LDN state to human-readable string
 *
 * @param state LDN state value
 * @return Static string describing the state
 */
const char* potluckStateToString(PotluckLdnState state);

#ifdef __cplusplus
}
#endif
