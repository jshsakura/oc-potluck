#pragma once
/**
 * @file relay_traffic.hpp
 * @brief What the module and the relay server have sent each other.
 *
 * Every frame on the relay connection passes through Record*, which counts it
 * and logs control packets one per line ("-> server: Scan (48 B)"). Game data
 * (ProxyData) is only counted, and LogWindow writes one line for it every few
 * seconds, so a busy match does not flood the log. The counters are lock-free
 * so the send and receive paths never wait on them, and the overlay reads them
 * through the config service.
 */

#include <cstddef>
#include <cstdint>
#include "../protocol/types.hpp"

namespace potluck::network::traffic {

/** @brief Totals since the relay connection was opened. */
struct Totals {
    uint32_t control_sent;      ///< Control packets to the server (everything but game data)
    uint32_t control_received;  ///< Control packets from the server
    uint32_t data_sent;         ///< Game data packets to the server
    uint32_t data_received;     ///< Game data packets from the server
    uint64_t data_bytes_sent;   ///< Game data frame bytes to the server
    uint64_t data_bytes_received;
    uint8_t  last_received;     ///< PacketId of the newest control packet from the server, 0xFF for none
};

/** @brief Start counting a new relay connection from zero. */
void Reset();

/** @brief A frame went to the server. */
void RecordSent(protocol::PacketId id, size_t frame_bytes);

/** @brief A frame came from the server. */
void RecordReceived(protocol::PacketId id, size_t frame_bytes);

/** @brief The totals as they stand. */
Totals Snapshot();

/**
 * @brief Log the game data of the last window, at most once per window.
 *
 * Writes nothing when no game data moved in the window.
 * @param now_ms A monotonic clock in milliseconds.
 */
void LogWindow(uint64_t now_ms);

/** @brief Window length of LogWindow. */
constexpr uint64_t WindowMs = 5000;

} // namespace potluck::network::traffic
