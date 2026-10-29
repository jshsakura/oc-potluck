/**
 * @file p2p_packet_stream.hpp
 * @brief P2P TCP Stream Reassembly and Zero-Overflow Packet Framing
 *
 * Provides bounded-memory TCP stream reassembly and packet dispatch/framing
 * shared by P2pProxyClient and P2pProxySession:
 * - Preserves and reassembles fragmented LdnHeader and packet payloads across
 *   arbitrary TCP recv() boundaries.
 * - Parses complete packets in-place within a 16 KiB inline buffer with zero
 *   dynamic allocation on the normal packet path (<= 16 KiB).
 * - Supports valid oversized protocol packets (> 16 KiB up to MAX_PACKET_SIZE)
 *   via an on-demand temporary accumulation buffer that is released immediately
 *   after packet dispatch or stream reset.
 * - Eliminates 64 KiB stack buffers on 16 KiB/32 KiB threads during ProxyData
 *   receive callbacks and single/broadcast sends.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include "../protocol/types.hpp"
#include "../protocol/relay_protocol.hpp"

namespace ams::mitm::p2p {

/**
 * @brief Callback type for forwarding received proxy packets to BSD MITM
 */
using ProxyPacketCallback = void(*)(potluck::protocol::PacketId type,
                                     const void* data, size_t size);

/**
 * @brief Bounded TCP stream reassembly buffer for P2P proxy connections
 *
 * Maintains a 16 KiB inline buffer for zero-allocation normal operation while
 * supporting valid protocol frames up to sizeof(LdnHeader) + MAX_PACKET_SIZE.
 */
class P2pStreamBuffer {
public:
    /** @brief Inline receive buffer size (16 KiB, saves 48 KiB vs legacy 64 KiB) */
    static constexpr size_t INLINE_BUFFER_SIZE = 0x4000;

    /** @brief Maximum protocol payload size (128 KiB, matching protocol::MAX_PACKET_SIZE) */
    static constexpr size_t MAX_PAYLOAD_SIZE = potluck::protocol::MAX_PACKET_SIZE;

    /** @brief Maximum full wire frame size (LdnHeader + MAX_PAYLOAD_SIZE) */
    static constexpr size_t MAX_FRAME_SIZE =
        sizeof(potluck::protocol::LdnHeader) + MAX_PAYLOAD_SIZE;

    /**
     * @brief Packet dispatch callback signature
     * @param header Validated LdnHeader
     * @param payload Pointer to contiguous packet payload (header.data_size bytes)
     * @param payload_size Size of payload in bytes
     * @param user_data Opaque caller context
     * @return true to continue processing subsequent packets in the stream,
     *         false to stop and reset (e.g., if the session disconnected)
     */
    using FrameCallback = bool(*)(const potluck::protocol::LdnHeader& header,
                                  const uint8_t* payload,
                                  size_t payload_size,
                                  void* user_data);

    /**
     * @brief Stream error callback signature (invoked on corrupt magic/length)
     */
    using ErrorCallback = void(*)(void* user_data);

    P2pStreamBuffer() noexcept;
    ~P2pStreamBuffer() noexcept;

    P2pStreamBuffer(const P2pStreamBuffer&) = delete;
    P2pStreamBuffer& operator=(const P2pStreamBuffer&) = delete;

    /**
     * @brief Reset stream reassembly state and release any temporary large buffer
     */
    void Reset() noexcept;

    /**
     * @brief Reset instrumentation counters (allocations and copied bytes)
     */
    void ResetStats() noexcept;

    /**
     * @brief Writable pointer inside the inline buffer for direct socket recv()
     */
    uint8_t* WritePtr() noexcept {
        return m_inline_buffer + m_inline_used;
    }

    /**
     * @brief Available writable capacity at WritePtr() for direct socket recv()
     */
    size_t WritableSize() const noexcept {
        return INLINE_BUFFER_SIZE - m_inline_used;
    }

    /**
     * @brief Number of unconsumed partial bytes currently held by the reassembler
     */
    size_t BufferedBytes() const noexcept {
        return m_large_buffer != nullptr ? m_large_used : m_inline_used;
    }

    /**
     * @brief Whether the reassembler is currently accumulating a packet > INLINE_BUFFER_SIZE
     */
    bool IsAccumulatingLargePacket() const noexcept {
        return m_large_buffer != nullptr;
    }

    /**
     * @brief Number of on-demand large packet allocations performed since construction/ResetStats
     */
    size_t LargeAllocCount() const noexcept {
        return m_large_alloc_count;
    }

    /**
     * @brief Total bytes copied/memmoved into internal reassembly storage since construction/ResetStats
     */
    size_t BytesCopied() const noexcept {
        return m_bytes_copied;
    }

    /**
     * @brief Force large-packet heap allocation failure (for unit testing)
     */
    void SetFailLargeAllocForTesting(bool fail) noexcept {
        m_fail_large_alloc = fail;
    }

    /**
     * @brief Feed received TCP bytes into the stream reassembler
     *
     * Supports both:
     * 1. Zero-copy in-place reads where `data == WritePtr()` from `recv()`
     * 2. Arbitrary external buffers of any size
     *
     * @param data Pointer to newly received bytes
     * @param size Number of bytes received
     * @param on_frame Callback invoked for each complete frame
     * @param on_error Callback invoked if header validation or large allocation fails
     * @param user_data Opaque context passed to callbacks
     * @return true if stream remains valid, false if a protocol/allocation error occurred
     */
    bool Process(const uint8_t* data,
                 size_t size,
                 FrameCallback on_frame,
                 ErrorCallback on_error,
                 void* user_data);

    /**
     * @brief Validate an LdnHeader for P2P stream framing
     */
    static bool ValidateHeader(const potluck::protocol::LdnHeader& header) noexcept {
        if (header.magic != potluck::protocol::PROTOCOL_MAGIC) {
            return false;
        }
        if (header.data_size < 0 ||
            static_cast<size_t>(header.data_size) > MAX_PAYLOAD_SIZE) {
            return false;
        }
        return true;
    }

private:
    bool BeginLargePacket(const uint8_t* prefix, size_t prefix_len, size_t total_packet_size) noexcept;
    void ReleaseLargePacket() noexcept;

    uint8_t m_inline_buffer[INLINE_BUFFER_SIZE];
    size_t m_inline_used;

    uint8_t* m_large_buffer;
    size_t m_large_capacity;
    size_t m_large_used;

    size_t m_large_alloc_count;
    size_t m_bytes_copied;
    bool m_fail_large_alloc;
};

/**
 * @brief Callbacks for P2pProxyClient packet dispatch
 */
struct P2pClientDispatchHandlers {
    void* user_data = nullptr;
    void (*on_proxy_config)(void* user_data, const potluck::protocol::ProxyConfig& config) = nullptr;
    void (*on_proxy_data)(void* user_data,
                          const potluck::protocol::ProxyDataHeader& header,
                          const uint8_t* data,
                          size_t data_len) = nullptr;
    void (*on_proxy_connect)(void* user_data, const potluck::protocol::ProxyConnectRequest& request) = nullptr;
    void (*on_proxy_connect_reply)(void* user_data, const potluck::protocol::ProxyConnectResponse& response) = nullptr;
    void (*on_proxy_disconnect)(void* user_data, const potluck::protocol::ProxyDisconnectMessage& message) = nullptr;
    void (*on_protocol_error)(void* user_data) = nullptr;
    bool (*is_client_active)(void* user_data) = nullptr;
};

/**
 * @brief Callbacks for P2pProxySession packet dispatch
 */
struct P2pSessionDispatchHandlers {
    void* user_data = nullptr;
    void (*on_external_proxy)(void* user_data, const potluck::protocol::ExternalProxyConfig& config) = nullptr;
    void (*on_proxy_data)(void* user_data,
                          const potluck::protocol::ProxyDataHeader& header,
                          const uint8_t* data,
                          size_t data_len) = nullptr;
    void (*on_proxy_connect)(void* user_data, const potluck::protocol::ProxyConnectRequest& request) = nullptr;
    void (*on_proxy_connect_reply)(void* user_data, const potluck::protocol::ProxyConnectResponse& response) = nullptr;
    void (*on_proxy_disconnect)(void* user_data, const potluck::protocol::ProxyDisconnectMessage& message) = nullptr;
    void (*on_protocol_error)(void* user_data) = nullptr;
    bool (*is_session_active)(void* user_data) = nullptr;
};

/**
 * @brief Process received TCP bytes for P2pProxyClient using P2pStreamBuffer
 */
bool ProcessClientStreamData(P2pStreamBuffer& stream,
                             const uint8_t* data,
                             size_t size,
                             const P2pClientDispatchHandlers& handlers);

/**
 * @brief Process received TCP bytes for P2pProxySession using P2pStreamBuffer
 */
bool ProcessSessionStreamData(P2pStreamBuffer& stream,
                              const uint8_t* data,
                              size_t size,
                              const P2pSessionDispatchHandlers& handlers);

/**
 * @brief Forward ProxyData to P2pProxyClient's ProxyPacketCallback without 64 KiB stack buffers
 *
 * When `data` immediately follows `header` in memory (the standard in-place
 * receive path), forwards `&header` directly with zero copies and zero stack
 * allocation. Otherwise uses a 2 KiB stack buffer for normal packets or an
 * on-demand heap buffer for oversized non-contiguous packets.
 */
bool ForwardClientProxyData(ProxyPacketCallback callback,
                            const potluck::protocol::ProxyDataHeader& header,
                            const uint8_t* data,
                            size_t data_len);

/**
 * @brief Stack-safe encoder/transmitter for ProxyData frames
 *
 * Replaces 64 KiB stack arrays on 16 KiB/32 KiB threads with a fixed ~2.07 KiB
 * (`sizeof(P2pProxyDataFrame) == 2120` bytes, including `m_small_buffer[0x800]`
 * and `m_prefix[32]`) stack object and avoids re-encoding identical frames
 * across multiple broadcast recipients:
 * - Frames <= 2 KiB (covering all standard MTU UDP gameplay traffic) are
 *   encoded once into `m_small_buffer` (2 KiB) and sent in a single write per peer.
 * - Larger frames (> 2 KiB up to MAX_PACKET_SIZE) encode only the 32-byte
 *   `m_prefix` (LdnHeader + ProxyDataHeader) inside the 2120-byte stack object
 *   and transmit the payload directly from the caller's buffer without copying
 *   or heap allocation.
 */
class P2pProxyDataFrame {
public:
    static constexpr size_t PREFIX_SIZE =
        sizeof(potluck::protocol::LdnHeader) + sizeof(potluck::protocol::ProxyDataHeader);
    static constexpr size_t SMALL_BUFFER_SIZE = 0x800; // 2 KiB

    P2pProxyDataFrame() noexcept
        : m_large_payload(nullptr)
        , m_large_payload_len(0)
        , m_small_size(0)
        , m_is_small(false)
        , m_prepared(false)
        , m_encode_count(0) {}

    /**
     * @brief Prepare the frame for transmission (encodes at most once)
     */
    bool Prepare(const potluck::protocol::ProxyDataHeader& header,
                 const uint8_t* data,
                 size_t data_len) noexcept;

    /**
     * @brief Whether Prepare() succeeded
     */
    bool IsPrepared() const noexcept { return m_prepared; }

    /**
     * @brief Whether the frame uses the single-write small inline buffer
     */
    bool IsSmallFrame() const noexcept { return m_is_small; }

    /**
     * @brief Number of times Prepare() encoded the frame
     */
    size_t EncodeCount() const noexcept { return m_encode_count; }

    /**
     * @brief Total wire bytes of the prepared frame
     */
    size_t WireSize() const noexcept {
        if (!m_prepared) {
            return 0;
        }
        return m_is_small ? m_small_size : (PREFIX_SIZE + m_large_payload_len);
    }

    /**
     * @brief Transmit the prepared frame using a sink callable `bool(const void*, size_t)`
     */
    template <typename SendFn>
    bool SendTo(SendFn&& send_fn) const {
        if (!m_prepared) {
            return false;
        }
        if (m_is_small) {
            return send_fn(m_small_buffer, m_small_size);
        }
        if (!send_fn(m_prefix, PREFIX_SIZE)) {
            return false;
        }
        if (m_large_payload_len > 0) {
            return send_fn(m_large_payload, m_large_payload_len);
        }
        return true;
    }

private:
    uint8_t m_small_buffer[SMALL_BUFFER_SIZE];
    uint8_t m_prefix[PREFIX_SIZE];
    const uint8_t* m_large_payload;
    size_t m_large_payload_len;
    size_t m_small_size;
    bool m_is_small;
    bool m_prepared;
    size_t m_encode_count;
};

} // namespace ams::mitm::p2p
