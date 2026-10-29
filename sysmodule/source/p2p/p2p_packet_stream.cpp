/**
 * @file p2p_packet_stream.cpp
 * @brief P2P TCP Stream Reassembly and Packet Dispatch Implementation
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "p2p_packet_stream.hpp"
#include "../debug/log.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace ams::mitm::p2p {

// =============================================================================
// P2pStreamBuffer Implementation
// =============================================================================

P2pStreamBuffer::P2pStreamBuffer() noexcept
    : m_inline_buffer{}
    , m_inline_used(0)
    , m_large_buffer(nullptr)
    , m_large_capacity(0)
    , m_large_used(0)
    , m_large_alloc_count(0)
    , m_bytes_copied(0)
    , m_fail_large_alloc(false) {
}

P2pStreamBuffer::~P2pStreamBuffer() noexcept {
    ReleaseLargePacket();
}

void P2pStreamBuffer::Reset() noexcept {
    m_inline_used = 0;
    ReleaseLargePacket();
}

void P2pStreamBuffer::ResetStats() noexcept {
    m_large_alloc_count = 0;
    m_bytes_copied = 0;
}

void P2pStreamBuffer::ReleaseLargePacket() noexcept {
    if (m_large_buffer != nullptr) {
        delete[] m_large_buffer;
        m_large_buffer = nullptr;
    }
    m_large_capacity = 0;
    m_large_used = 0;
}

bool P2pStreamBuffer::BeginLargePacket(const uint8_t* prefix,
                                       size_t prefix_len,
                                       size_t total_packet_size) noexcept {
    ReleaseLargePacket();

    if (m_fail_large_alloc) {
        LOG_ERROR("P2P stream: simulated large packet allocation failure (%zu bytes)",
                  total_packet_size);
        return false;
    }

    uint8_t* buf = new (std::nothrow) uint8_t[total_packet_size];
    if (buf == nullptr) {
        LOG_ERROR("P2P stream: failed to allocate %zu bytes for large packet", total_packet_size);
        return false;
    }

    m_large_alloc_count++;
    std::memcpy(buf, prefix, prefix_len);
    m_bytes_copied += prefix_len;
    m_large_buffer = buf;
    m_large_capacity = total_packet_size;
    m_large_used = prefix_len;
    return true;
}

bool P2pStreamBuffer::Process(const uint8_t* data,
                              size_t size,
                              FrameCallback on_frame,
                              ErrorCallback on_error,
                              void* user_data) {
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        Reset();
        if (on_error != nullptr) {
            on_error(user_data);
        }
        return false;
    }

    // =========================================================================
    // Fast Path: In-place socket recv() directly into WritePtr()
    // =========================================================================
    if (m_large_buffer == nullptr &&
        data == m_inline_buffer + m_inline_used &&
        size <= INLINE_BUFFER_SIZE - m_inline_used) {
        m_inline_used += size;
        size_t offset = 0;

        while (offset < m_inline_used) {
            const size_t avail = m_inline_used - offset;
            if (avail < sizeof(potluck::protocol::LdnHeader)) {
                LOG_VERBOSE("P2P stream: incomplete header (%zu bytes)", avail);
                break;
            }

            potluck::protocol::LdnHeader hdr{};
            std::memcpy(&hdr, m_inline_buffer + offset, sizeof(hdr));

            if (!ValidateHeader(hdr)) {
                LOG_WARN("P2P stream: invalid header (magic=0x%08X, data_size=%d)",
                         hdr.magic, hdr.data_size);
                Reset();
                if (on_error != nullptr) {
                    on_error(user_data);
                }
                return false;
            }

            const size_t payload_size = static_cast<size_t>(hdr.data_size);
            const size_t packet_size = sizeof(potluck::protocol::LdnHeader) + payload_size;

            if (avail >= packet_size) {
                const uint8_t* payload =
                    m_inline_buffer + offset + sizeof(potluck::protocol::LdnHeader);
                offset += packet_size;

                if (on_frame != nullptr && !on_frame(hdr, payload, payload_size, user_data)) {
                    Reset();
                    return true;
                }
                continue;
            }

            // Incomplete packet at the tail of m_inline_buffer
            if (packet_size > INLINE_BUFFER_SIZE) {
                if (!BeginLargePacket(m_inline_buffer + offset, avail, packet_size)) {
                    Reset();
                    if (on_error != nullptr) {
                        on_error(user_data);
                    }
                    return false;
                }
                offset = m_inline_used;
            } else {
                LOG_VERBOSE("P2P stream: incomplete packet (need %zu, have %zu)",
                            packet_size, avail);
            }
            break;
        }

        const size_t remaining = m_inline_used - offset;
        if (remaining > 0 && offset > 0) {
            std::memmove(m_inline_buffer, m_inline_buffer + offset, remaining);
            m_bytes_copied += remaining;
        }
        m_inline_used = remaining;
        return true;
    }

    // =========================================================================
    // General Path: Arbitrary external chunk or large-packet continuation
    // =========================================================================
    size_t pos = 0;
    while (pos < size) {
        // 1. Continue accumulating an in-progress oversized packet (> 16 KiB)
        if (m_large_buffer != nullptr) {
            const size_t need = m_large_capacity - m_large_used;
            const size_t take = std::min(size - pos, need);
            std::memcpy(m_large_buffer + m_large_used, data + pos, take);
            m_bytes_copied += take;
            m_large_used += take;
            pos += take;

            if (m_large_used == m_large_capacity) {
                potluck::protocol::LdnHeader hdr{};
                std::memcpy(&hdr, m_large_buffer, sizeof(hdr));
                const size_t payload_size = static_cast<size_t>(hdr.data_size);

                uint8_t* completed = m_large_buffer;
                m_large_buffer = nullptr;
                m_large_capacity = 0;
                m_large_used = 0;

                const bool keep_going =
                    (on_frame == nullptr) ||
                    on_frame(hdr,
                             completed + sizeof(potluck::protocol::LdnHeader),
                             payload_size,
                             user_data);
                delete[] completed;

                if (!keep_going) {
                    Reset();
                    return true;
                }
            }
            continue;
        }

        // 2. Complete any partial packet currently waiting in m_inline_buffer
        if (m_inline_used > 0) {
            if (m_inline_used < sizeof(potluck::protocol::LdnHeader)) {
                const size_t need_hdr = sizeof(potluck::protocol::LdnHeader) - m_inline_used;
                const size_t take = std::min(size - pos, need_hdr);
                std::memcpy(m_inline_buffer + m_inline_used, data + pos, take);
                m_bytes_copied += take;
                m_inline_used += take;
                pos += take;

                if (m_inline_used < sizeof(potluck::protocol::LdnHeader)) {
                    break;
                }
            }

            potluck::protocol::LdnHeader hdr{};
            std::memcpy(&hdr, m_inline_buffer, sizeof(hdr));

            if (!ValidateHeader(hdr)) {
                LOG_WARN("P2P stream: invalid reassembled header (magic=0x%08X, data_size=%d)",
                         hdr.magic, hdr.data_size);
                Reset();
                if (on_error != nullptr) {
                    on_error(user_data);
                }
                return false;
            }

            const size_t payload_size = static_cast<size_t>(hdr.data_size);
            const size_t packet_size = sizeof(potluck::protocol::LdnHeader) + payload_size;

            if (packet_size > INLINE_BUFFER_SIZE) {
                const size_t saved = m_inline_used;
                m_inline_used = 0;
                if (!BeginLargePacket(m_inline_buffer, saved, packet_size)) {
                    Reset();
                    if (on_error != nullptr) {
                        on_error(user_data);
                    }
                    return false;
                }
                continue;
            }

            const size_t need = packet_size - m_inline_used;
            const size_t take = std::min(size - pos, need);
            if (take > 0) {
                std::memcpy(m_inline_buffer + m_inline_used, data + pos, take);
                m_bytes_copied += take;
                m_inline_used += take;
                pos += take;
            }

            if (m_inline_used == packet_size) {
                m_inline_used = 0;
                if (on_frame != nullptr &&
                    !on_frame(hdr,
                              m_inline_buffer + sizeof(potluck::protocol::LdnHeader),
                              payload_size,
                              user_data)) {
                    Reset();
                    return true;
                }
            }
            continue;
        }

        // 3. Parse directly from `data + pos` with zero copies while complete frames exist
        const size_t avail = size - pos;
        if (avail < sizeof(potluck::protocol::LdnHeader)) {
            std::memmove(m_inline_buffer, data + pos, avail);
            m_bytes_copied += avail;
            m_inline_used = avail;
            break;
        }

        potluck::protocol::LdnHeader hdr{};
        std::memcpy(&hdr, data + pos, sizeof(hdr));

        if (!ValidateHeader(hdr)) {
            LOG_WARN("P2P stream: invalid header in chunk (magic=0x%08X, data_size=%d)",
                     hdr.magic, hdr.data_size);
            Reset();
            if (on_error != nullptr) {
                on_error(user_data);
            }
            return false;
        }

        const size_t payload_size = static_cast<size_t>(hdr.data_size);
        const size_t packet_size = sizeof(potluck::protocol::LdnHeader) + payload_size;

        if (avail >= packet_size) {
            const uint8_t* payload = data + pos + sizeof(potluck::protocol::LdnHeader);
            pos += packet_size;

            if (on_frame != nullptr && !on_frame(hdr, payload, payload_size, user_data)) {
                Reset();
                return true;
            }
            continue;
        }

        // Incomplete packet at the tail of `data`
        if (packet_size <= INLINE_BUFFER_SIZE) {
            std::memmove(m_inline_buffer, data + pos, avail);
            m_bytes_copied += avail;
            m_inline_used = avail;
        } else {
            if (!BeginLargePacket(data + pos, avail, packet_size)) {
                Reset();
                if (on_error != nullptr) {
                    on_error(user_data);
                }
                return false;
            }
        }
        break;
    }

    return true;
}

// =============================================================================
// Client & Session Stream Dispatch
// =============================================================================

namespace {

bool OnClientFrame(const potluck::protocol::LdnHeader& header,
                   const uint8_t* packet_data,
                   size_t data_size,
                   void* user_data) {
    const auto* handlers = static_cast<const P2pClientDispatchHandlers*>(user_data);
    if (handlers == nullptr) {
        return true;
    }
    if (handlers->is_client_active != nullptr && !handlers->is_client_active(handlers->user_data)) {
        return false;
    }

    switch (static_cast<potluck::protocol::PacketId>(header.type)) {
        case potluck::protocol::PacketId::ProxyConfig: {
            if (data_size >= sizeof(potluck::protocol::ProxyConfig) &&
                handlers->on_proxy_config != nullptr) {
                const auto* config =
                    reinterpret_cast<const potluck::protocol::ProxyConfig*>(packet_data);
                handlers->on_proxy_config(handlers->user_data, *config);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyData: {
            if (data_size >= sizeof(potluck::protocol::ProxyDataHeader) &&
                handlers->on_proxy_data != nullptr) {
                const auto* pheader =
                    reinterpret_cast<const potluck::protocol::ProxyDataHeader*>(packet_data);
                const uint8_t* payload =
                    packet_data + sizeof(potluck::protocol::ProxyDataHeader);
                const size_t payload_len =
                    data_size - sizeof(potluck::protocol::ProxyDataHeader);
                if (payload_len >= pheader->data_length) {
                    handlers->on_proxy_data(handlers->user_data, *pheader, payload, payload_len);
                } else {
                    LOG_WARN("P2P client: ProxyData length mismatch (payload=%zu, header=%u)",
                             payload_len, pheader->data_length);
                }
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyConnect: {
            if (data_size >= sizeof(potluck::protocol::ProxyConnectRequest) &&
                handlers->on_proxy_connect != nullptr) {
                const auto* request =
                    reinterpret_cast<const potluck::protocol::ProxyConnectRequest*>(packet_data);
                handlers->on_proxy_connect(handlers->user_data, *request);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyConnectReply: {
            if (data_size >= sizeof(potluck::protocol::ProxyConnectResponse) &&
                handlers->on_proxy_connect_reply != nullptr) {
                const auto* response =
                    reinterpret_cast<const potluck::protocol::ProxyConnectResponse*>(packet_data);
                handlers->on_proxy_connect_reply(handlers->user_data, *response);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyDisconnect: {
            if (data_size >= sizeof(potluck::protocol::ProxyDisconnectMessage) &&
                handlers->on_proxy_disconnect != nullptr) {
                const auto* message =
                    reinterpret_cast<const potluck::protocol::ProxyDisconnectMessage*>(packet_data);
                handlers->on_proxy_disconnect(handlers->user_data, *message);
            }
            break;
        }

        default:
            LOG_VERBOSE("P2P client: unknown packet type %u", header.type);
            break;
    }

    if (handlers->is_client_active != nullptr) {
        return handlers->is_client_active(handlers->user_data);
    }
    return true;
}

void OnClientError(void* user_data) {
    const auto* handlers = static_cast<const P2pClientDispatchHandlers*>(user_data);
    if (handlers != nullptr && handlers->on_protocol_error != nullptr) {
        handlers->on_protocol_error(handlers->user_data);
    }
}

bool OnSessionFrame(const potluck::protocol::LdnHeader& header,
                    const uint8_t* packet_data,
                    size_t data_size,
                    void* user_data) {
    const auto* handlers = static_cast<const P2pSessionDispatchHandlers*>(user_data);
    if (handlers == nullptr) {
        return true;
    }
    if (handlers->is_session_active != nullptr && !handlers->is_session_active(handlers->user_data)) {
        return false;
    }

    switch (static_cast<potluck::protocol::PacketId>(header.type)) {
        case potluck::protocol::PacketId::ExternalProxy: {
            if (data_size >= sizeof(potluck::protocol::ExternalProxyConfig) &&
                handlers->on_external_proxy != nullptr) {
                const auto* config =
                    reinterpret_cast<const potluck::protocol::ExternalProxyConfig*>(packet_data);
                handlers->on_external_proxy(handlers->user_data, *config);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyData: {
            if (data_size >= sizeof(potluck::protocol::ProxyDataHeader) &&
                handlers->on_proxy_data != nullptr) {
                const auto* pheader =
                    reinterpret_cast<const potluck::protocol::ProxyDataHeader*>(packet_data);
                const uint8_t* payload =
                    packet_data + sizeof(potluck::protocol::ProxyDataHeader);
                const size_t payload_len =
                    data_size - sizeof(potluck::protocol::ProxyDataHeader);
                if (payload_len >= pheader->data_length) {
                    handlers->on_proxy_data(handlers->user_data, *pheader, payload, payload_len);
                } else {
                    LOG_WARN("P2P session: ProxyData length mismatch (payload=%zu, header=%u)",
                             payload_len, pheader->data_length);
                }
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyConnect: {
            if (data_size >= sizeof(potluck::protocol::ProxyConnectRequest) &&
                handlers->on_proxy_connect != nullptr) {
                const auto* request =
                    reinterpret_cast<const potluck::protocol::ProxyConnectRequest*>(packet_data);
                handlers->on_proxy_connect(handlers->user_data, *request);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyConnectReply: {
            if (data_size >= sizeof(potluck::protocol::ProxyConnectResponse) &&
                handlers->on_proxy_connect_reply != nullptr) {
                const auto* response =
                    reinterpret_cast<const potluck::protocol::ProxyConnectResponse*>(packet_data);
                handlers->on_proxy_connect_reply(handlers->user_data, *response);
            }
            break;
        }

        case potluck::protocol::PacketId::ProxyDisconnect: {
            if (data_size >= sizeof(potluck::protocol::ProxyDisconnectMessage) &&
                handlers->on_proxy_disconnect != nullptr) {
                const auto* message =
                    reinterpret_cast<const potluck::protocol::ProxyDisconnectMessage*>(packet_data);
                handlers->on_proxy_disconnect(handlers->user_data, *message);
            }
            break;
        }

        default:
            LOG_WARN("P2P session: unhandled packet type %u", header.type);
            break;
    }

    if (handlers->is_session_active != nullptr) {
        return handlers->is_session_active(handlers->user_data);
    }
    return true;
}

void OnSessionError(void* user_data) {
    const auto* handlers = static_cast<const P2pSessionDispatchHandlers*>(user_data);
    if (handlers != nullptr && handlers->on_protocol_error != nullptr) {
        handlers->on_protocol_error(handlers->user_data);
    }
}

} // namespace

bool ProcessClientStreamData(P2pStreamBuffer& stream,
                             const uint8_t* data,
                             size_t size,
                             const P2pClientDispatchHandlers& handlers) {
    if (handlers.is_client_active != nullptr && !handlers.is_client_active(handlers.user_data)) {
        stream.Reset();
        return false;
    }
    const bool ok = stream.Process(
        data,
        size,
        OnClientFrame,
        OnClientError,
        const_cast<P2pClientDispatchHandlers*>(&handlers));
    if (!ok || (handlers.is_client_active != nullptr && !handlers.is_client_active(handlers.user_data))) {
        stream.Reset();
    }
    return ok;
}

bool ProcessSessionStreamData(P2pStreamBuffer& stream,
                              const uint8_t* data,
                              size_t size,
                              const P2pSessionDispatchHandlers& handlers) {
    if (handlers.is_session_active != nullptr && !handlers.is_session_active(handlers.user_data)) {
        stream.Reset();
        return false;
    }
    const bool ok = stream.Process(
        data,
        size,
        OnSessionFrame,
        OnSessionError,
        const_cast<P2pSessionDispatchHandlers*>(&handlers));
    if (!ok || (handlers.is_session_active != nullptr && !handlers.is_session_active(handlers.user_data))) {
        stream.Reset();
    }
    return ok;
}

// =============================================================================
// Stack-Safe ProxyData Forwarding & Framing
// =============================================================================

bool ForwardClientProxyData(ProxyPacketCallback callback,
                            const potluck::protocol::ProxyDataHeader& header,
                            const uint8_t* data,
                            size_t data_len) {
    if (callback == nullptr) {
        return false;
    }
    if (data_len > 0 && data == nullptr) {
        return false;
    }
    if (sizeof(header) + data_len > potluck::protocol::MAX_PACKET_SIZE) {
        return false;
    }

    const size_t total_size = sizeof(header) + data_len;
    const auto* header_bytes = reinterpret_cast<const uint8_t*>(&header);

    // Zero-copy fast path: when invoked from ProcessClientStreamData, `data`
    // immediately follows `header` in the receive buffer.
    if (data_len == 0 || data == header_bytes + sizeof(header)) {
        callback(potluck::protocol::PacketId::ProxyData, header_bytes, total_size);
        return true;
    }

    // Small stack buffer fallback for non-contiguous callers (<= 2 KiB)
    constexpr size_t kSmallCallbackBufferSize = 0x800;
    if (total_size <= kSmallCallbackBufferSize) {
        uint8_t small_buf[kSmallCallbackBufferSize];
        std::memcpy(small_buf, &header, sizeof(header));
        std::memcpy(small_buf + sizeof(header), data, data_len);
        callback(potluck::protocol::PacketId::ProxyData, small_buf, total_size);
        return true;
    }

    // Oversized non-contiguous fallback without overflowing 16 KiB thread stack
    uint8_t* large_buf = new (std::nothrow) uint8_t[total_size];
    if (large_buf == nullptr) {
        return false;
    }
    std::memcpy(large_buf, &header, sizeof(header));
    std::memcpy(large_buf + sizeof(header), data, data_len);
    callback(potluck::protocol::PacketId::ProxyData, large_buf, total_size);
    delete[] large_buf;
    return true;
}

bool P2pProxyDataFrame::Prepare(const potluck::protocol::ProxyDataHeader& header,
                                const uint8_t* data,
                                size_t data_len) noexcept {
    m_prepared = false;
    m_is_small = false;
    m_small_size = 0;
    m_large_payload = nullptr;
    m_large_payload_len = 0;

    if (data_len > 0 && data == nullptr) {
        return false;
    }

    const size_t payload_total = sizeof(potluck::protocol::ProxyDataHeader) + data_len;
    if (payload_total > potluck::protocol::MAX_PACKET_SIZE) {
        return false;
    }

    const size_t frame_total = PREFIX_SIZE + data_len;
    if (frame_total <= SMALL_BUFFER_SIZE) {
        size_t encoded_len = 0;
        const auto res = potluck::protocol::encode_with_data(
            m_small_buffer,
            sizeof(m_small_buffer),
            potluck::protocol::PacketId::ProxyData,
            header,
            data,
            data_len,
            encoded_len);
        if (res != potluck::protocol::EncodeResult::Success) {
            return false;
        }
        m_small_size = encoded_len;
        m_is_small = true;
        m_prepared = true;
        m_encode_count++;
        return true;
    }

    // Large frame (> 2 KiB up to MAX_PACKET_SIZE): encode only the 32-byte prefix
    potluck::protocol::LdnHeader ldn_hdr{};
    ldn_hdr.magic = potluck::protocol::PROTOCOL_MAGIC;
    ldn_hdr.type = static_cast<uint8_t>(potluck::protocol::PacketId::ProxyData);
    ldn_hdr.version = potluck::protocol::PROTOCOL_VERSION;
    ldn_hdr.reserved = 0;
    ldn_hdr.data_size = static_cast<int32_t>(payload_total);

    std::memcpy(m_prefix, &ldn_hdr, sizeof(ldn_hdr));
    std::memcpy(m_prefix + sizeof(ldn_hdr), &header, sizeof(header));
    m_large_payload = data;
    m_large_payload_len = data_len;
    m_is_small = false;
    m_prepared = true;
    m_encode_count++;
    return true;
}

} // namespace ams::mitm::p2p
