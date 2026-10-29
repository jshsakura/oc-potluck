/**
 * @file overlay_tests.cpp
 * @brief Unit tests for the overlay logic
 *
 * Tests the overlay helper functions and IPC structures without
 * requiring the actual Switch environment. Uses mocks for libnx types.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include "../overlay/source/game_list.hpp"
#include "../overlay/source/ui_text.hpp"
#include "../overlay/source/keypad.hpp"

//=============================================================================
// Mock Switch/libnx types for testing
//=============================================================================

// Basic types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res)    ((res) != 0)

// Mock Service structure
typedef struct {
    u32 handle;
} Service;

//=============================================================================
// Include overlay types (copy from potluck_ipc.h for testing)
//=============================================================================

typedef enum {
    PotluckStatus_Disconnected = 0,
    PotluckStatus_Connecting = 1,
    PotluckStatus_Connected = 2,
    PotluckStatus_Ready = 3,
    PotluckStatus_Error = 4,
} PotluckConnectionStatus;

typedef enum {
    PotluckLdnState_None = 0,
    PotluckLdnState_Initialized = 1,
    PotluckLdnState_AccessPoint = 2,
    PotluckLdnState_AccessPointCreated = 3,
    PotluckLdnState_Station = 4,
    PotluckLdnState_StationConnected = 5,
    PotluckLdnState_Error = 6,
} PotluckLdnState;

/**
 * @brief Session information structure (matches IPC definition)
 *
 * 8 bytes total, same as SessionInfoIpc in sysmodule.
 */
typedef struct {
    u8 node_count;      ///< Current number of nodes in session
    u8 max_nodes;       ///< Maximum nodes allowed in session
    u8 local_node_id;   ///< This node's ID in the session
    u8 is_host;         ///< 1 if this node is the host, 0 otherwise
    u8 reserved[4];     ///< Reserved for future use
} PotluckSessionInfo;

typedef struct {
    Service s;
} PotluckService;

//=============================================================================
// Helper functions from main.cpp (extracted for testing)
//=============================================================================

const char* ConnectionStatusToString(PotluckConnectionStatus status) {
    switch (status) {
        case PotluckStatus_Disconnected: return "Disconnected";
        case PotluckStatus_Connecting:   return "Connecting...";
        case PotluckStatus_Connected:    return "Connected";
        case PotluckStatus_Ready:        return "Ready";
        case PotluckStatus_Error:        return "Error";
        default:                        return "Unknown";
    }
}

const char* LdnStateToString(PotluckLdnState state) {
    switch (state) {
        case PotluckLdnState_None:              return "None";
        case PotluckLdnState_Initialized:       return "Initialized";
        case PotluckLdnState_AccessPoint:       return "Access Point";
        case PotluckLdnState_AccessPointCreated:return "Hosting";
        case PotluckLdnState_Station:           return "Station";
        case PotluckLdnState_StationConnected:  return "Connected";
        case PotluckLdnState_Error:             return "Error";
        default:                            return "Unknown";
    }
}

// Format session info string
void FormatSessionInfo(const PotluckSessionInfo* info, char* buf, size_t bufSize) {
    if (info->node_count == 0) {
        snprintf(buf, bufSize, "Not in session");
    } else {
        snprintf(buf, bufSize, "%d/%d players (%s)",
                 info->node_count, info->max_nodes,
                 info->is_host ? "Host" : "Client");
    }
}

// Format server address string
void FormatServerAddress(const char* host, u16 port, char* buf, size_t bufSize) {
    snprintf(buf, bufSize, "%s:%u", host, port);
}

// Format latency string
void FormatLatency(u32 rtt_ms, char* buf, size_t bufSize) {
    if (rtt_ms == 0) {
        snprintf(buf, bufSize, "N/A");
    } else {
        snprintf(buf, bufSize, "%u ms", rtt_ms);
    }
}

//=============================================================================
// Test Framework
//=============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [TEST] %s...", #name); \
    name(); \
    g_tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STREQ(a, b) ASSERT(strcmp((a), (b)) == 0)

//=============================================================================
// Connection Status Tests
//=============================================================================

TEST(status_disconnected_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(PotluckStatus_Disconnected), "Disconnected");
}

TEST(status_connecting_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(PotluckStatus_Connecting), "Connecting...");
}

TEST(status_connected_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(PotluckStatus_Connected), "Connected");
}

TEST(status_ready_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(PotluckStatus_Ready), "Ready");
}

TEST(status_error_to_string) {
    ASSERT_STREQ(ConnectionStatusToString(PotluckStatus_Error), "Error");
}

TEST(status_unknown_to_string) {
    ASSERT_STREQ(ConnectionStatusToString((PotluckConnectionStatus)99), "Unknown");
}

//=============================================================================
// LDN State Tests
//=============================================================================

TEST(ldn_state_none_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_None), "None");
}

TEST(ldn_state_initialized_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_Initialized), "Initialized");
}

TEST(ldn_state_access_point_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_AccessPoint), "Access Point");
}

TEST(ldn_state_access_point_created_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_AccessPointCreated), "Hosting");
}

TEST(ldn_state_station_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_Station), "Station");
}

TEST(ldn_state_station_connected_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_StationConnected), "Connected");
}

TEST(ldn_state_error_to_string) {
    ASSERT_STREQ(LdnStateToString(PotluckLdnState_Error), "Error");
}

TEST(ldn_state_unknown_to_string) {
    ASSERT_STREQ(LdnStateToString((PotluckLdnState)99), "Unknown");
}

//=============================================================================
// Session Info Formatting Tests
//=============================================================================

TEST(session_info_not_in_session) {
    PotluckSessionInfo info = {};
    info.node_count = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "Not in session");
}

TEST(session_info_host_single_player) {
    PotluckSessionInfo info = {};
    info.node_count = 1;
    info.max_nodes = 8;
    info.is_host = 1;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1/8 players (Host)");
}

TEST(session_info_client_multi_player) {
    PotluckSessionInfo info = {};
    info.node_count = 4;
    info.max_nodes = 8;
    info.is_host = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "4/8 players (Client)");
}

TEST(session_info_full_session) {
    PotluckSessionInfo info = {};
    info.node_count = 8;
    info.max_nodes = 8;
    info.is_host = 1;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "8/8 players (Host)");
}

TEST(session_info_two_players) {
    PotluckSessionInfo info = {};
    info.node_count = 2;
    info.max_nodes = 2;
    info.is_host = 0;
    char buf[64];
    FormatSessionInfo(&info, buf, sizeof(buf));
    ASSERT_STREQ(buf, "2/2 players (Client)");
}

//=============================================================================
// Server Address Formatting Tests
//=============================================================================

TEST(server_address_localhost) {
    char buf[96];
    FormatServerAddress("localhost", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "localhost:39990");
}

TEST(server_address_ip) {
    char buf[96];
    FormatServerAddress("192.168.1.100", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "192.168.1.100:39990");
}

TEST(server_address_hostname) {
    char buf[96];
    FormatServerAddress("relay.example.com", 39990, buf, sizeof(buf));
    ASSERT_STREQ(buf, "relay.example.com:39990");
}

TEST(server_address_custom_port) {
    char buf[96];
    FormatServerAddress("server.net", 12345, buf, sizeof(buf));
    ASSERT_STREQ(buf, "server.net:12345");
}

//=============================================================================
// Latency Formatting Tests
//=============================================================================

TEST(latency_zero_shows_na) {
    char buf[32];
    FormatLatency(0, buf, sizeof(buf));
    ASSERT_STREQ(buf, "N/A");
}

TEST(latency_small_value) {
    char buf[32];
    FormatLatency(5, buf, sizeof(buf));
    ASSERT_STREQ(buf, "5 ms");
}

TEST(latency_medium_value) {
    char buf[32];
    FormatLatency(42, buf, sizeof(buf));
    ASSERT_STREQ(buf, "42 ms");
}

TEST(latency_high_value) {
    char buf[32];
    FormatLatency(250, buf, sizeof(buf));
    ASSERT_STREQ(buf, "250 ms");
}

TEST(latency_very_high_value) {
    char buf[32];
    FormatLatency(1500, buf, sizeof(buf));
    ASSERT_STREQ(buf, "1500 ms");
}

//=============================================================================
// IPC Structure Tests
//=============================================================================

TEST(session_info_structure_size) {
    // Verify structure packing - must be exactly 8 bytes for IPC
    ASSERT_EQ(sizeof(PotluckSessionInfo), 8u);
}

TEST(session_info_zero_initialized) {
    PotluckSessionInfo info = {};
    ASSERT_EQ(info.node_count, 0);
    ASSERT_EQ(info.max_nodes, 0);
    ASSERT_EQ(info.local_node_id, 0);
    ASSERT_EQ(info.is_host, 0);
    ASSERT_EQ(info.reserved[0], 0);
}

TEST(connection_status_enum_values) {
    ASSERT_EQ(PotluckStatus_Disconnected, 0);
    ASSERT_EQ(PotluckStatus_Connecting, 1);
    ASSERT_EQ(PotluckStatus_Connected, 2);
    ASSERT_EQ(PotluckStatus_Ready, 3);
    ASSERT_EQ(PotluckStatus_Error, 4);
}

TEST(ldn_state_enum_values) {
    ASSERT_EQ(PotluckLdnState_None, 0);
    ASSERT_EQ(PotluckLdnState_Initialized, 1);
    ASSERT_EQ(PotluckLdnState_AccessPoint, 2);
    ASSERT_EQ(PotluckLdnState_AccessPointCreated, 3);
    ASSERT_EQ(PotluckLdnState_Station, 4);
    ASSERT_EQ(PotluckLdnState_StationConnected, 5);
    ASSERT_EQ(PotluckLdnState_Error, 6);
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST(format_session_buffer_small) {
    PotluckSessionInfo info = {};
    info.node_count = 8;
    info.max_nodes = 8;
    info.is_host = 1;
    char buf[10]; // Too small, but should not crash
    FormatSessionInfo(&info, buf, sizeof(buf));
    // Just verify it doesn't crash and produces something
    ASSERT(strlen(buf) < 10);
}

TEST(format_latency_max_u32) {
    char buf[32];
    FormatLatency(4294967295u, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0);
}

//=============================================================================
// Main
//=============================================================================

TEST(game_list_reads_bounded_pages_and_rejects_malformed_ids) {
    char path[] = "/tmp/potluck-games-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE* file = fdopen(fd, "w");
    ASSERT(file != nullptr);
    fprintf(file, "not-an-id\n0x0000000000000000\n01000000000000000\n0100000000000001garbage\n");
    fprintf(file, "0100000000000001%080d\n", 0);
    for (unsigned i = 1; i <= 19; ++i) fprintf(file, "0x%016llX\r\n", 0x0100000000000000ULL + i);
    ASSERT_EQ(fclose(file), 0);
    for (uint32_t offset = 0; offset <= 24; ++offset) {
        const auto page = overlay::ReadGameListPage(path, offset);
        ASSERT(page.readable);
        const uint32_t expected = offset >= 19 ? 0 : (19 - offset < 8 ? 19 - offset : 8);
        ASSERT_EQ(page.count, expected);
        ASSERT_EQ(page.has_next, offset < 11);
        for (uint32_t i = 0; i < 8; ++i) {
            ASSERT_EQ(page.ids[i], i < expected ? 0x0100000000000001ULL + offset + i : 0);
        }
    }
    ASSERT_EQ(overlay::ReadGameListPage(path, UINT32_MAX).count, 0u);
    ASSERT_EQ(unlink(path), 0);
    ASSERT(!overlay::ReadGameListPage(path, 0).readable);
}

TEST(game_names_stream_only_the_requested_page) {
    char path[] = "/tmp/potluck-names-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE* file = fdopen(fd, "w");
    ASSERT(file != nullptr);
    fprintf(file, "invalid\tIgnored\n0100000000000003\tOther game\n0100000000000001\t포켓몬\n0100000000000002\tMario\n");
    ASSERT_EQ(fclose(file), 0);
    overlay::GameListPage page{};
    page.count = 2;
    page.ids[0] = 0x0100000000000002ULL;
    page.ids[1] = 0x0100000000000001ULL;
    overlay::ReadGameNames(path, page);
    ASSERT_STREQ(page.names[0], "Mario");
    ASSERT_STREQ(page.names[1], "포켓몬");
    ASSERT_EQ(page.names[2][0], '\0');
    ASSERT_EQ(unlink(path), 0);
    overlay::ReadGameNames(path, page);
    ASSERT_STREQ(page.names[0], "Mario");
}

TEST(ui_automatically_selects_korean_or_english_without_a_catalog) {
    overlay::SelectLanguage(0x6f6b);
    ASSERT_STREQ(overlay::Text("On", "켜짐"), "켜짐");
    overlay::SelectLanguage(0x53552d6e65);
    ASSERT_STREQ(overlay::Text("On", "켜짐"), "On");
    overlay::SelectLanguage(0);
    ASSERT_STREQ(overlay::Text("On", "켜짐"), "On");
}

TEST(keypad_buffer_respects_limit_and_edits) {
    overlay::keypad::Buffer buffer(3);
    ASSERT(buffer.Push('1'));
    ASSERT(buffer.Push('2'));
    ASSERT(buffer.Push('3'));
    ASSERT(!buffer.Push('4'));
    ASSERT_STREQ(buffer.Text(), "123");
    ASSERT(buffer.Pop());
    ASSERT_STREQ(buffer.Text(), "12");
    buffer.Assign("98765");
    ASSERT_STREQ(buffer.Text(), "987");
    buffer.Clear();
    ASSERT_EQ(buffer.Size(), 0u);
    ASSERT(!buffer.Pop());
}

TEST(keypad_cursor_wraps_and_skips_blank_cells) {
    using namespace overlay::keypad;
    Cursor hex;
    hex.Move(-1, 0, HexLayout);
    ASSERT_EQ(hex.Selected(HexLayout), '3');
    hex.Move(0, -1, HexLayout);
    ASSERT_EQ(hex.Selected(HexLayout), 'f');

    Cursor port;
    port.Move(0, -1, PortLayout);           // column 0 of the last row is blank
    ASSERT_EQ(port.Selected(PortLayout), '0');
    port.Move(1, 0, PortLayout);            // the only key in that row
    ASSERT_EQ(port.Selected(PortLayout), '0');
    port.Move(0, 1, PortLayout);
    ASSERT_EQ(port.Selected(PortLayout), '2');
}

TEST(keypad_maps_touches_to_keys) {
    using namespace overlay::keypad;
    const Grid grid{10, 100, 30, 40, 5};  // keys at x 10..39, 45..74, ... rows at y 100..139, 145..
    int col = -1, row = -1;
    ASSERT(KeyAt(HostLayout, grid, 12, 102, &col, &row));
    ASSERT_EQ(HostLayout.At(col, row), '1');
    ASSERT(KeyAt(HostLayout, grid, 50, 150, &col, &row));
    ASSERT_EQ(HostLayout.At(col, row), 'w');
    ASSERT(!KeyAt(HostLayout, grid, 41, 102, &col, &row));   // the gap between keys
    ASSERT(!KeyAt(HostLayout, grid, 5, 102, &col, &row));    // left of the grid
    ASSERT(!KeyAt(HostLayout, grid, 12, 400, &col, &row));   // below the grid
    ASSERT(!KeyAt(HostLayout, grid, 10 + 8 * 35, 100 + 3 * 45, &col, &row));  // blank cell
    ASSERT(KeyAt(HostLayout, grid, 10 + 7 * 35, 100 + 3 * 45, &col, &row));
    ASSERT_EQ(HostLayout.At(col, row), '.');
}

TEST(keypad_validates_room_code_host_and_port) {
    using namespace overlay::keypad;
    ASSERT(IsValidRoomCode(""));
    ASSERT(IsValidRoomCode("0a1b2c3d"));
    ASSERT(!IsValidRoomCode("0a1b2c3"));
    ASSERT(!IsValidRoomCode("0a1b2c3g"));

    ASSERT(IsValidHost("127.0.0.1"));
    ASSERT(IsValidHost("ldn-01.example"));
    ASSERT(!IsValidHost(""));
    ASSERT(!IsValidHost(".1.2"));
    ASSERT(!IsValidHost("1..2"));
    ASSERT(!IsValidHost("host-"));

    uint16_t port = 0;
    ASSERT(ParsePort("30456", &port));
    ASSERT_EQ(port, 30456);
    ASSERT(!ParsePort("", &port));
    ASSERT(!ParsePort("0", &port));
    ASSERT(!ParsePort("65536", &port));
}

TEST(installed_supported_games_filters_and_pages) {
    char path[] = "/tmp/potluck-gamelist-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT(fd >= 0);
    FILE* file = fdopen(fd, "w");
    ASSERT(file != nullptr);
    fprintf(file, "0100000000000001\n0100000000000002\n0100000000000003\n");
    ASSERT_EQ(fclose(file), 0);

    uint64_t installed[3] = {0x0100000000000002ULL, 0x0100000000000099ULL, 0x0100000000000001ULL};
    auto page = overlay::GetInstalledSupportedGamesPage(path, installed, 3, 0);
    ASSERT(page.readable);
    ASSERT_EQ(page.total_matched, 2U);
    ASSERT_EQ(page.count, 2U);
    ASSERT_EQ(page.ids[0], 0x0100000000000001ULL);
    ASSERT_EQ(page.ids[1], 0x0100000000000002ULL);
    ASSERT_EQ(unlink(path), 0);
}

int main() {
    printf("\n========================================\n");
    printf("  Overlay Tests - potluck\n");
    printf("========================================\n\n");

    printf("--- Connection Status Tests ---\n");
    RUN_TEST(status_disconnected_to_string);
    RUN_TEST(status_connecting_to_string);
    RUN_TEST(status_connected_to_string);
    RUN_TEST(status_ready_to_string);
    RUN_TEST(status_error_to_string);
    RUN_TEST(status_unknown_to_string);

    printf("\n--- LDN State Tests ---\n");
    RUN_TEST(ldn_state_none_to_string);
    RUN_TEST(ldn_state_initialized_to_string);
    RUN_TEST(ldn_state_access_point_to_string);
    RUN_TEST(ldn_state_access_point_created_to_string);
    RUN_TEST(ldn_state_station_to_string);
    RUN_TEST(ldn_state_station_connected_to_string);
    RUN_TEST(ldn_state_error_to_string);
    RUN_TEST(ldn_state_unknown_to_string);

    printf("\n--- Session Info Formatting Tests ---\n");
    RUN_TEST(session_info_not_in_session);
    RUN_TEST(session_info_host_single_player);
    RUN_TEST(session_info_client_multi_player);
    RUN_TEST(session_info_full_session);
    RUN_TEST(session_info_two_players);

    printf("\n--- Server Address Formatting Tests ---\n");
    RUN_TEST(server_address_localhost);
    RUN_TEST(server_address_ip);
    RUN_TEST(server_address_hostname);
    RUN_TEST(server_address_custom_port);

    printf("\n--- Latency Formatting Tests ---\n");
    RUN_TEST(latency_zero_shows_na);
    RUN_TEST(latency_small_value);
    RUN_TEST(latency_medium_value);
    RUN_TEST(latency_high_value);
    RUN_TEST(latency_very_high_value);

    printf("\n--- IPC Structure Tests ---\n");
    RUN_TEST(session_info_structure_size);
    RUN_TEST(session_info_zero_initialized);
    RUN_TEST(connection_status_enum_values);
    RUN_TEST(ldn_state_enum_values);

    printf("\n--- Edge Cases ---\n");
    RUN_TEST(format_session_buffer_small);
    RUN_TEST(format_latency_max_u32);
    RUN_TEST(game_list_reads_bounded_pages_and_rejects_malformed_ids);
    RUN_TEST(game_names_stream_only_the_requested_page);
    RUN_TEST(ui_automatically_selects_korean_or_english_without_a_catalog);
    RUN_TEST(installed_supported_games_filters_and_pages);
    RUN_TEST(keypad_buffer_respects_limit_and_edits);
    RUN_TEST(keypad_cursor_wraps_and_skips_blank_cells);
    RUN_TEST(keypad_maps_touches_to_keys);
    RUN_TEST(keypad_validates_room_code_host_and_port);

    printf("\n========================================\n");
    printf("  Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
