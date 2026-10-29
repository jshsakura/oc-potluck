/**
 * @file lan_protocol.hpp
 * @brief ldn_mitm compatible LAN packet format
 *
 * Local mode speaks the LAN protocol of ldn_mitm (spacemeowx2/ldn_mitm,
 * GPL-2.0), so a PotLuck console in local mode and a console running
 * ldn_mitm find and join each other. Every packet is a 12-byte header and a
 * body; bodies are run-length coded on their zero bytes when that is shorter.
 * Scan and ScanResp travel as UDP broadcasts on DefaultPort, Connect and
 * SyncNetwork over a TCP connection to the host on the same port.
 *
 * Nothing here touches sockets, so the format is tested on the host.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original protocol)
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace potluck::lan {

enum class PacketType : uint8_t {
    Scan = 0,         ///< Broadcast: who hosts a network?
    ScanResp = 1,     ///< Reply to Scan: the host's NetworkInfo
    Connect = 2,      ///< Station to host: the station's NodeInfo
    SyncNetwork = 3,  ///< Host to stations: the current NetworkInfo
};

struct PacketHeader {
    uint32_t magic;
    PacketType type;
    uint8_t compressed;
    uint16_t length;             ///< Body bytes on the wire
    uint16_t decompress_length;  ///< Body bytes after decoding (0 if not compressed)
    uint8_t reserved[2];
};
static_assert(sizeof(PacketHeader) == 12);

constexpr uint32_t MAGIC = 0x11451400;
constexpr uint16_t DEFAULT_PORT = 11452;
/// Largest packet on the wire, header included (ldn_mitm's buffer size)
constexpr size_t BUFFER_SIZE = 2048;
constexpr size_t MAX_BODY = BUFFER_SIZE - sizeof(PacketHeader);

/**
 * @brief Run-length code the zero bytes of `input`
 * @param output_size in: capacity of output, out: bytes written
 * @return 0 on success, -1 when the result would not fit
 */
int compress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size);

/**
 * @brief Undo compress()
 * @param output_size in: capacity of output, out: bytes written
 * @return 0 on success, -1 on truncated input or overflow
 */
int decompress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size);

/**
 * @brief Write one packet (header and body) into `out`
 *
 * The body is compressed when that makes it shorter, as ldn_mitm does.
 * @return Bytes written, or 0 when the packet does not fit
 */
size_t encode_packet(PacketType type, const void* body, size_t body_size, uint8_t* out, size_t out_capacity);

/**
 * @brief Reassembles packets from a byte stream (TCP) or single datagrams (UDP)
 *
 * Append received bytes, then call Next() until it returns false. A bad
 * magic or an impossible length drops what was buffered so a corrupt peer
 * cannot wedge the reader. Fixed buffers only.
 */
class PacketReader {
public:
    /// @return false when `size` bytes do not fit (the buffer is then reset)
    bool Append(const void* data, size_t size);

    /**
     * @brief Take the next complete packet, decoded
     * @param body Receives the decoded body, at least MAX_BODY bytes
     * @return true when a packet was produced; `*error` is set when bytes
     *         were dropped as malformed
     */
    bool Next(PacketType* type, uint8_t* body, size_t* body_size, bool* error);

    void Reset() { m_used = 0; }
    size_t Buffered() const { return m_used; }
    size_t Free() const { return BUFFER_SIZE - m_used; }

private:
    uint8_t m_buffer[BUFFER_SIZE];
    size_t m_used = 0;
};

} // namespace potluck::lan
