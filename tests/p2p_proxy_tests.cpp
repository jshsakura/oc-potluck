/**
 * @file p2p_proxy_tests.cpp
 * @brief Unit & regression tests for P2P Proxy Server / P2pProxySession stream reassembly
 *
 * Verifies:
 * 1. Reference client interoperability constants and address/token helpers
 * 2. Production P2pStreamBuffer + ProcessSessionStreamData stream reassembly:
 *    - 12 x 1432B ProxyData stream (17,184 bytes) across 64 KiB, 16 KiB, and
 *      arbitrary small chunks (both in-place WritePtr() socket reads and
 *      external chunk reads) with zero disconnects and zero dynamic allocations
 *    - Split LdnHeader and split payload across multiple reads
 *    - Coalesced multi-packet reads across all session packet types
 *      (ExternalProxy, ProxyData, ProxyConnect, ProxyConnectReply, ProxyDisconnect)
 *    - Large packets (> 16 KiB inline buffer: 32 KiB, 64 KiB, MAX_PACKET_SIZE)
 *      and immediate release of temporary large packet buffer
 *    - Corrupt stream handling (invalid magic, negative data_size, oversized
 *      data_size, truncated payloads, ProxyDataHeader length mismatch, mid-stream
 *      session disconnect)
 *    - Connection reuse / Reset() cleanup
 *    - P2pProxyDataFrame single-encode broadcast framing
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "../sysmodule/source/p2p/p2p_packet_stream.hpp"

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

#define ASSERT_NE(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a == _b) { \
            printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
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

// ============================================================================
// P2P Proxy Constants (must match the reference client)
// ============================================================================

namespace p2p_proxy {
    constexpr uint16_t PRIVATE_PORT_BASE = 39990;
    constexpr int PRIVATE_PORT_RANGE = 10;
    constexpr uint16_t PUBLIC_PORT_BASE = 39990;
    constexpr int PUBLIC_PORT_RANGE = 10;

    constexpr int PORT_LEASE_LENGTH = 60;   // seconds
    constexpr int PORT_LEASE_RENEW = 50;    // seconds

    constexpr int AUTH_WAIT_SECONDS = 1;
    constexpr int MAX_PLAYERS = 8;

    constexpr uint32_t SUBNET_MASK = 0xFFFF0000;  // /16 subnet
    constexpr uint32_t BROADCAST_SUFFIX = 0x000000FF;  // .255 for broadcast

    inline uint32_t calculate_broadcast(uint32_t ip, uint32_t mask) {
        return (ip & mask) | (~mask);
    }

    inline bool is_broadcast(uint32_t ip, uint32_t broadcast) {
        return ip == broadcast || ip == 0xFFFFFFFFU;
    }

    inline bool is_private_ip(const uint8_t physical_ip[16]) {
        for (int i = 0; i < 16; i++) {
            if (physical_ip[i] != 0) {
                return false;
            }
        }
        return true;
    }

    inline uint32_t extract_ipv4(const uint8_t physical_ip[16]) {
        return (static_cast<uint32_t>(physical_ip[0]) << 24) |
               (static_cast<uint32_t>(physical_ip[1]) << 16) |
               (static_cast<uint32_t>(physical_ip[2]) << 8)  |
               static_cast<uint32_t>(physical_ip[3]);
    }
}

// ============================================================================
// Production Session Stream Test Harness
// ============================================================================

namespace {

using namespace potluck::protocol;
using namespace ams::mitm::p2p;

struct SessionRecorder {
    P2pStreamBuffer stream;
    bool connected = true;
    bool stop_on_external_proxy = false;
    bool stop_on_proxy_data = false;
    bool payload_valid_during_callback = false;
    int disconnected_count = 0;
    int external_proxy_count = 0;
    int proxy_data_count = 0;
    int proxy_connect_count = 0;
    int proxy_connect_reply_count = 0;
    int proxy_disconnect_count = 0;
    size_t last_payload_len = 0;
    uint8_t first_payload_byte = 0;
    uint8_t last_payload_byte = 0;
    uint32_t last_dest_ip = 0;

    P2pSessionDispatchHandlers MakeHandlers() {
        return P2pSessionDispatchHandlers{
            this,
            [](void* ctx, const ExternalProxyConfig&) {
                auto* self = static_cast<SessionRecorder*>(ctx);
                self->external_proxy_count++;
                if (self->stop_on_external_proxy) {
                    self->connected = false;
                    self->disconnected_count++;
                }
            },
            [](void* ctx, const ProxyDataHeader& hdr, const uint8_t* payload, size_t len) {
                auto* self = static_cast<SessionRecorder*>(ctx);
                self->proxy_data_count++;
                self->last_payload_len = len;
                self->last_dest_ip = hdr.info.dest_ipv4;
                if (len > 0 && payload != nullptr) {
                    self->first_payload_byte = payload[0];
                    self->last_payload_byte = payload[len - 1];
                }
                if (self->stop_on_proxy_data) {
                    // Simulate concurrent Disconnect(true) from master thread during callback
                    // while receive thread is still reading `payload`.
                    self->connected = false;
                    bool all_match = true;
                    for (size_t i = 0; i < len; ++i) {
                        if (payload[i] != self->first_payload_byte) {
                            all_match = false;
                            break;
                        }
                    }
                    self->payload_valid_during_callback = all_match;
                }
            },
            [](void* ctx, const ProxyConnectRequest&) {
                static_cast<SessionRecorder*>(ctx)->proxy_connect_count++;
            },
            [](void* ctx, const ProxyConnectResponse&) {
                static_cast<SessionRecorder*>(ctx)->proxy_connect_reply_count++;
            },
            [](void* ctx, const ProxyDisconnectMessage&) {
                static_cast<SessionRecorder*>(ctx)->proxy_disconnect_count++;
            },
            [](void* ctx) {
                auto* self = static_cast<SessionRecorder*>(ctx);
                self->connected = false;
                self->disconnected_count++;
            },
            [](void* ctx) -> bool {
                return static_cast<const SessionRecorder*>(ctx)->connected;
            }
        };
    }

    bool FeedExternalChunks(const std::vector<uint8_t>& bytes, size_t chunk_size) {
        auto handlers = MakeHandlers();
        for (size_t offset = 0; offset < bytes.size(); offset += chunk_size) {
            const size_t n = std::min(chunk_size, bytes.size() - offset);
            if (!ProcessSessionStreamData(stream, bytes.data() + offset, n, handlers)) {
                return false;
            }
        }
        return true;
    }

    bool FeedSocketReads(const std::vector<uint8_t>& bytes, size_t max_read_size) {
        auto handlers = MakeHandlers();
        size_t offset = 0;
        while (offset < bytes.size() && connected) {
            uint8_t* write_ptr = stream.WritePtr();
            const size_t writable = stream.WritableSize();
            ASSERT_TRUE(writable > 0);
            const size_t n = std::min({max_read_size, writable, bytes.size() - offset});
            std::memcpy(write_ptr, bytes.data() + offset, n);
            offset += n;
            if (!ProcessSessionStreamData(stream, write_ptr, n, handlers)) {
                return false;
            }
        }
        return true;
    }
};

std::vector<uint8_t> BuildProxyDataStream(size_t count, size_t payload_len, uint8_t fill_byte = 0xA5) {
    const size_t packet_size = sizeof(LdnHeader) + sizeof(ProxyDataHeader) + payload_len;
    std::vector<uint8_t> stream(count * packet_size, fill_byte);
    for (size_t i = 0; i < count; ++i) {
        LdnHeader h{};
        h.magic = PROTOCOL_MAGIC;
        h.version = PROTOCOL_VERSION;
        h.type = static_cast<uint8_t>(PacketId::ProxyData);
        h.data_size = static_cast<int32_t>(sizeof(ProxyDataHeader) + payload_len);
        std::memcpy(stream.data() + i * packet_size, &h, sizeof(h));

        ProxyDataHeader ph{};
        ph.info.source_ipv4 = 0x0A720002;
        ph.info.source_port = 49152;
        ph.info.dest_ipv4 = 0x0A720001;
        ph.info.dest_port = 49152;
        ph.info.protocol = ProtocolType::Udp;
        ph.data_length = static_cast<uint32_t>(payload_len);
        std::memcpy(stream.data() + i * packet_size + sizeof(h), &ph, sizeof(ph));
    }
    return stream;
}

template <typename T>
void AppendEncodedPacket(std::vector<uint8_t>& out, PacketId id, const T& payload) {
    uint8_t buf[512];
    size_t encoded = 0;
    const auto res = encode(buf, sizeof(buf), id, payload, encoded);
    ASSERT_EQ(static_cast<int>(res), static_cast<int>(EncodeResult::Success));
    out.insert(out.end(), buf, buf + encoded);
}

} // namespace

// ============================================================================
// Constants Tests - Verify Reference Client Compatibility
// ============================================================================

TEST(constants_private_port_base_matches_reference) {
    ASSERT_EQ(p2p_proxy::PRIVATE_PORT_BASE, 39990U);
}

TEST(constants_private_port_range_matches_reference) {
    ASSERT_EQ(p2p_proxy::PRIVATE_PORT_RANGE, 10);
}

TEST(constants_public_port_base_matches_reference) {
    ASSERT_EQ(p2p_proxy::PUBLIC_PORT_BASE, 39990U);
}

TEST(constants_public_port_range_matches_reference) {
    ASSERT_EQ(p2p_proxy::PUBLIC_PORT_RANGE, 10);
}

TEST(constants_port_lease_length_matches_reference) {
    ASSERT_EQ(p2p_proxy::PORT_LEASE_LENGTH, 60);
}

TEST(constants_port_lease_renew_matches_reference) {
    ASSERT_EQ(p2p_proxy::PORT_LEASE_RENEW, 50);
}

TEST(constants_auth_wait_matches_reference) {
    ASSERT_EQ(p2p_proxy::AUTH_WAIT_SECONDS, 1);
}

TEST(constants_max_players_matches_reference) {
    ASSERT_EQ(p2p_proxy::MAX_PLAYERS, 8);
}

TEST(constants_subnet_mask_is_class_b) {
    ASSERT_EQ(p2p_proxy::SUBNET_MASK, 0xFFFF0000U);
}

TEST(constants_inline_recv_buffer_is_16kib) {
    ASSERT_EQ(P2pStreamBuffer::INLINE_BUFFER_SIZE, 0x4000U);
    ASSERT_EQ(P2pStreamBuffer::INLINE_BUFFER_SIZE, 16384U);
}

TEST(lease_timing_correct) {
    ASSERT_TRUE(p2p_proxy::PORT_LEASE_RENEW < p2p_proxy::PORT_LEASE_LENGTH);
    int margin = p2p_proxy::PORT_LEASE_LENGTH - p2p_proxy::PORT_LEASE_RENEW;
    ASSERT_EQ(margin, 10);
}

TEST(port_range_valid) {
    uint16_t max_private = p2p_proxy::PRIVATE_PORT_BASE + p2p_proxy::PRIVATE_PORT_RANGE - 1;
    uint16_t max_public = p2p_proxy::PUBLIC_PORT_BASE + p2p_proxy::PUBLIC_PORT_RANGE - 1;

    ASSERT_TRUE(max_private < 65536);
    ASSERT_TRUE(max_public < 65536);
    ASSERT_EQ(max_private, 39999U);
    ASSERT_EQ(max_public, 39999U);
}

// ============================================================================
// Broadcast & IP Helper Tests
// ============================================================================

TEST(broadcast_calculation_class_b) {
    uint32_t ip = 0x0A720001U;
    uint32_t mask = 0xFFFF0000U;
    uint32_t broadcast = p2p_proxy::calculate_broadcast(ip, mask);
    ASSERT_EQ(broadcast, 0x0A72FFFFU);
}

TEST(broadcast_calculation_class_c) {
    uint32_t ip = 0xC0A80164U;
    uint32_t mask = 0xFFFFFF00U;
    uint32_t broadcast = p2p_proxy::calculate_broadcast(ip, mask);
    ASSERT_EQ(broadcast, 0xC0A801FFU);
}

TEST(is_broadcast_exact_match) {
    uint32_t broadcast = 0x0A72FFFFU;
    ASSERT_TRUE(p2p_proxy::is_broadcast(broadcast, broadcast));
}

TEST(is_broadcast_global) {
    uint32_t broadcast = 0x0A72FFFFU;
    ASSERT_TRUE(p2p_proxy::is_broadcast(0xFFFFFFFFU, broadcast));
}

TEST(is_broadcast_normal_ip_false) {
    uint32_t broadcast = 0x0A72FFFFU;
    uint32_t normal_ip = 0x0A720001U;
    ASSERT_FALSE(p2p_proxy::is_broadcast(normal_ip, broadcast));
}

TEST(is_private_ip_all_zeros) {
    uint8_t physical_ip[16] = {0};
    ASSERT_TRUE(p2p_proxy::is_private_ip(physical_ip));
}

TEST(is_private_ip_has_bytes) {
    uint8_t physical_ip[16] = {192, 168, 1, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ASSERT_FALSE(p2p_proxy::is_private_ip(physical_ip));
}

TEST(is_private_ip_last_byte_set) {
    uint8_t physical_ip[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    ASSERT_FALSE(p2p_proxy::is_private_ip(physical_ip));
}

TEST(extract_ipv4_standard) {
    uint8_t physical_ip[16] = {192, 168, 1, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ASSERT_EQ(p2p_proxy::extract_ipv4(physical_ip), 0xC0A80164U);
}

TEST(extract_ipv4_localhost) {
    uint8_t physical_ip[16] = {127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ASSERT_EQ(p2p_proxy::extract_ipv4(physical_ip), 0x7F000001U);
}

TEST(extract_ipv4_ldn_network) {
    uint8_t physical_ip[16] = {10, 114, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ASSERT_EQ(p2p_proxy::extract_ipv4(physical_ip), 0x0A720001U);
}

TEST(extract_ipv4_zeros) {
    uint8_t physical_ip[16] = {0};
    ASSERT_EQ(p2p_proxy::extract_ipv4(physical_ip), 0U);
}

TEST(token_compare_match) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    ASSERT_EQ(std::memcmp(token1, token2, 16), 0);
}

TEST(token_compare_mismatch_first_byte) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0xFF, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    ASSERT_NE(std::memcmp(token1, token2, 16), 0);
}

TEST(token_compare_mismatch_last_byte) {
    uint8_t token1[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t token2[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF};
    ASSERT_NE(std::memcmp(token1, token2, 16), 0);
}

TEST(virtual_ip_in_ldn_range) {
    uint32_t base = 0x0A720000U;
    uint32_t mask = p2p_proxy::SUBNET_MASK;
    ASSERT_EQ(0x0A720001U & mask, base);
    ASSERT_EQ(0x0A720002U & mask, base);
    ASSERT_EQ(0x0A720008U & mask, base);
}

// ============================================================================
// Production Stream Reassembly Regression Tests (P2pProxySession path)
// ============================================================================

TEST(session_repro_12x1432_packets_64k_and_16k_chunks) {
    // Exact reproduction of parser_repro.cpp: 12 ProxyData packets of 1432 bytes
    // (17,184 bytes total). Previously, 16 KiB chunks delivered only 11 packets
    // and triggered Disconnect(false) on the 12th packet's continuation bytes.
    const auto wire = BuildProxyDataStream(12, 1400);
    ASSERT_EQ(wire.size(), 17184U);

    // 1. 64 KiB external chunk
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 0x10000));
        ASSERT_EQ(rec.proxy_data_count, 12);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        ASSERT_EQ(rec.stream.BytesCopied(), 0U);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 2. 16 KiB external chunks
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 12);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 3. 16 KiB in-place socket recv() into WritePtr()
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 12);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        // Only the 632-byte partial prefix of packet #12 at offset 15752 is memmoved!
        ASSERT_EQ(rec.stream.BytesCopied(), 632U);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }
}

TEST(session_reassembly_split_header_and_byte_by_byte) {
    const auto wire = BuildProxyDataStream(4, 300, 0x5C);

    // 1-byte chunks via external buffer
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 1));
        ASSERT_EQ(rec.proxy_data_count, 4);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.last_payload_len, 300U);
        ASSERT_EQ(rec.first_payload_byte, 0x5CU);
        ASSERT_EQ(rec.last_payload_byte, 0x5CU);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
    }

    // 5-byte chunks via in-place WritePtr() (splits 12-byte LdnHeader across 3 reads)
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 5));
        ASSERT_EQ(rec.proxy_data_count, 4);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.last_payload_len, 300U);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
    }
}

TEST(session_coalesced_mixed_packet_types_and_unknown_type) {
    std::vector<uint8_t> wire;

    ExternalProxyConfig ext{};
    ext.proxy_port = 39990;
    ext.address_family = 2;
    AppendEncodedPacket(wire, PacketId::ExternalProxy, ext);

    ProxyConnectRequest conn_req{};
    conn_req.info.source_ipv4 = 0x0A720002;
    conn_req.info.dest_ipv4 = 0x0A720001;
    AppendEncodedPacket(wire, PacketId::ProxyConnect, conn_req);

    ProxyConnectResponse conn_resp{};
    conn_resp.info = conn_req.info;
    AppendEncodedPacket(wire, PacketId::ProxyConnectReply, conn_resp);

    ProxyDisconnectMessage disc_msg{};
    disc_msg.info = conn_req.info;
    disc_msg.disconnect_reason = 1;
    AppendEncodedPacket(wire, PacketId::ProxyDisconnect, disc_msg);

    // Unknown packet type (Ping = 18, not handled by P2pProxySession)
    PingMessage ping{};
    ping.requester = 1;
    ping.id = 42;
    AppendEncodedPacket(wire, PacketId::Ping, ping);

    const auto data_part = BuildProxyDataStream(2, 128, 0x77);
    wire.insert(wire.end(), data_part.begin(), data_part.end());

    // Deliver all coalesced in a single read, and also in 19-byte fragments
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.external_proxy_count, 1);
        ASSERT_EQ(rec.proxy_connect_count, 1);
        ASSERT_EQ(rec.proxy_connect_reply_count, 1);
        ASSERT_EQ(rec.proxy_disconnect_count, 1);
        ASSERT_EQ(rec.proxy_data_count, 2);
        ASSERT_EQ(rec.disconnected_count, 0);
    }
    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 19));
        ASSERT_EQ(rec.external_proxy_count, 1);
        ASSERT_EQ(rec.proxy_connect_count, 1);
        ASSERT_EQ(rec.proxy_connect_reply_count, 1);
        ASSERT_EQ(rec.proxy_disconnect_count, 1);
        ASSERT_EQ(rec.proxy_data_count, 2);
        ASSERT_EQ(rec.disconnected_count, 0);
    }
}

TEST(session_large_packets_32k_64k_and_max_packet_size) {
    // 1. 32 KiB payload + 64 KiB payload + normal packet in one continuous stream
    std::vector<uint8_t> wire;
    const auto pkt_32k = BuildProxyDataStream(1, 32768, 0x31);
    const auto pkt_64k = BuildProxyDataStream(1, 65500, 0x62);
    const auto pkt_small = BuildProxyDataStream(1, 512, 0x93);
    wire.insert(wire.end(), pkt_32k.begin(), pkt_32k.end());
    wire.insert(wire.end(), pkt_64k.begin(), pkt_64k.end());
    wire.insert(wire.end(), pkt_small.begin(), pkt_small.end());

    {
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 3);
        ASSERT_EQ(rec.disconnected_count, 0);
        ASSERT_EQ(rec.last_payload_len, 512U);
        ASSERT_EQ(rec.first_payload_byte, 0x93U);
        ASSERT_FALSE(rec.stream.IsAccumulatingLargePacket());
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 2U);
    }

    // 2. Maximum protocol payload: MAX_PACKET_SIZE - sizeof(ProxyDataHeader) = 131052 bytes
    const size_t max_data_len = MAX_PACKET_SIZE - sizeof(ProxyDataHeader);
    const auto pkt_max = BuildProxyDataStream(1, max_data_len, 0xE4);
    {
        SessionRecorder rec;
        // Feed 7 bytes first (split header before transitioning to large packet), then 16 KiB chunks
        auto handlers = rec.MakeHandlers();
        ASSERT_TRUE(ProcessSessionStreamData(rec.stream, pkt_max.data(), 7, handlers));
        ASSERT_EQ(rec.stream.BufferedBytes(), 7U);
        for (size_t off = 7; off < pkt_max.size(); off += 0x4000) {
            const size_t n = std::min<size_t>(0x4000, pkt_max.size() - off);
            ASSERT_TRUE(ProcessSessionStreamData(rec.stream, pkt_max.data() + off, n, handlers));
        }
        ASSERT_EQ(rec.proxy_data_count, 1);
        ASSERT_EQ(rec.last_payload_len, max_data_len);
        ASSERT_EQ(rec.first_payload_byte, 0xE4U);
        ASSERT_EQ(rec.last_payload_byte, 0xE4U);
        ASSERT_FALSE(rec.stream.IsAccumulatingLargePacket());
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }
}

TEST(session_invalid_magic_negative_and_oversized_length_disconnect) {
    // 1. Invalid magic (in-place read)
    {
        SessionRecorder rec;
        LdnHeader bad{};
        bad.magic = 0xDEADBEEF;
        bad.version = PROTOCOL_VERSION;
        bad.type = static_cast<uint8_t>(PacketId::ProxyData);
        bad.data_size = 32;
        uint8_t* w = rec.stream.WritePtr();
        std::memcpy(w, &bad, sizeof(bad));
        auto handlers = rec.MakeHandlers();
        ASSERT_FALSE(ProcessSessionStreamData(rec.stream, w, sizeof(bad), handlers));
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.disconnected_count, 1);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 2. Negative data_size (-12, which previously caused packet_size == 0 infinite loop!)
    {
        SessionRecorder rec;
        LdnHeader neg{};
        neg.magic = PROTOCOL_MAGIC;
        neg.version = PROTOCOL_VERSION;
        neg.type = static_cast<uint8_t>(PacketId::ProxyData);
        neg.data_size = -12;
        auto handlers = rec.MakeHandlers();
        ASSERT_FALSE(ProcessSessionStreamData(
            rec.stream, reinterpret_cast<const uint8_t*>(&neg), sizeof(neg), handlers));
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.disconnected_count, 1);
    }

    // 3. Oversized data_size (> MAX_PACKET_SIZE, split across two reads)
    {
        SessionRecorder rec;
        LdnHeader huge{};
        huge.magic = PROTOCOL_MAGIC;
        huge.version = PROTOCOL_VERSION;
        huge.type = static_cast<uint8_t>(PacketId::ProxyData);
        huge.data_size = static_cast<int32_t>(MAX_PACKET_SIZE + 1);
        const auto* raw = reinterpret_cast<const uint8_t*>(&huge);
        auto handlers = rec.MakeHandlers();
        ASSERT_TRUE(ProcessSessionStreamData(rec.stream, raw, 5, handlers));
        ASSERT_FALSE(ProcessSessionStreamData(rec.stream, raw + 5, sizeof(huge) - 5, handlers));
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.disconnected_count, 1);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 4. Null data pointer with non-zero length
    {
        SessionRecorder rec;
        auto handlers = rec.MakeHandlers();
        ASSERT_TRUE(ProcessSessionStreamData(rec.stream, nullptr, 0, handlers));
        ASSERT_FALSE(ProcessSessionStreamData(rec.stream, nullptr, 16, handlers));
        ASSERT_EQ(rec.disconnected_count, 1);
    }
}

TEST(session_truncated_payloads_and_Disconnect_mid_stream_and_reset) {
    // 1. Truncated ProxyDataHeader (data_size = 8 < sizeof(ProxyDataHeader))
    //    and ProxyDataHeader claiming data_length > payload_len
    {
        SessionRecorder rec;
        std::vector<uint8_t> wire;

        LdnHeader h1{};
        h1.magic = PROTOCOL_MAGIC;
        h1.version = PROTOCOL_VERSION;
        h1.type = static_cast<uint8_t>(PacketId::ProxyData);
        h1.data_size = 8; // < sizeof(ProxyDataHeader)
        wire.insert(wire.end(), reinterpret_cast<uint8_t*>(&h1), reinterpret_cast<uint8_t*>(&h1) + sizeof(h1));
        wire.insert(wire.end(), 8, 0);

        LdnHeader h2{};
        h2.magic = PROTOCOL_MAGIC;
        h2.version = PROTOCOL_VERSION;
        h2.type = static_cast<uint8_t>(PacketId::ProxyData);
        h2.data_size = sizeof(ProxyDataHeader) + 4;
        ProxyDataHeader ph2{};
        ph2.data_length = 100; // > 4 bytes actually present
        wire.insert(wire.end(), reinterpret_cast<uint8_t*>(&h2), reinterpret_cast<uint8_t*>(&h2) + sizeof(h2));
        wire.insert(wire.end(), reinterpret_cast<uint8_t*>(&ph2), reinterpret_cast<uint8_t*>(&ph2) + sizeof(ph2));
        wire.insert(wire.end(), 4, 0xAA);

        const auto valid_pkt = BuildProxyDataStream(1, 64, 0x42);
        wire.insert(wire.end(), valid_pkt.begin(), valid_pkt.end());

        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 1);
        ASSERT_EQ(rec.last_payload_len, 64U);
        ASSERT_EQ(rec.disconnected_count, 0);
    }

    // 2. Mid-stream session disconnect (e.g., ExternalProxy auth fails) stops
    //    processing subsequent packets in the same buffer and resets state
    {
        SessionRecorder rec;
        rec.stop_on_external_proxy = true;
        std::vector<uint8_t> wire;
        ExternalProxyConfig ext{};
        AppendEncodedPacket(wire, PacketId::ExternalProxy, ext);
        const auto trailing = BuildProxyDataStream(2, 64);
        wire.insert(wire.end(), trailing.begin(), trailing.end());

        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.external_proxy_count, 1);
        ASSERT_EQ(rec.proxy_data_count, 0);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);

        // Also verify mid-stream disconnect on external chunk path and large-packet path
        SessionRecorder rec2;
        rec2.stop_on_external_proxy = true;
        ASSERT_TRUE(rec2.FeedExternalChunks(wire, 0x4000));
        ASSERT_EQ(rec2.external_proxy_count, 1);
        ASSERT_EQ(rec2.proxy_data_count, 0);
        ASSERT_EQ(rec2.stream.BufferedBytes(), 0U);
    }

    // 3. Connection reuse: Reset() clears partial inline or partial large packet state
    {
        SessionRecorder rec;
        const auto wire = BuildProxyDataStream(2, 500, 0x19);
        auto handlers = rec.MakeHandlers();
        // Feed partial packet (100 bytes), then Reset() as on Disconnect/reconnect
        ASSERT_TRUE(ProcessSessionStreamData(rec.stream, wire.data(), 100, handlers));
        ASSERT_EQ(rec.stream.BufferedBytes(), 100U);
        rec.stream.Reset();
        rec.stream.ResetStats();
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);

        // Now feed complete stream on reused buffer
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 2);
        ASSERT_EQ(rec.disconnected_count, 0);
    }
}

TEST(proxy_data_frame_broadcast_single_encode_and_large_zero_copy) {
    // 1. Small frame (<= 2 KiB): encoded once and sent to 8 broadcast recipients
    std::vector<uint8_t> payload(1400, 0x3C);
    ProxyDataHeader hdr{};
    hdr.info.source_ipv4 = 0x0A720001;
    hdr.info.source_port = 49152;
    hdr.info.dest_ipv4 = 0x0A72FFFF;
    hdr.info.dest_port = 49152;
    hdr.info.protocol = ProtocolType::Udp;
    hdr.data_length = static_cast<uint32_t>(payload.size());

    P2pProxyDataFrame frame;
    ASSERT_TRUE(frame.Prepare(hdr, payload.data(), payload.size()));
    ASSERT_TRUE(frame.IsPrepared());
    ASSERT_TRUE(frame.IsSmallFrame());
    ASSERT_EQ(frame.EncodeCount(), 1U);
    ASSERT_EQ(frame.WireSize(), 1432U);

    for (int peer = 0; peer < 8; ++peer) {
        std::vector<uint8_t> peer_wire;
        int write_calls = 0;
        ASSERT_TRUE(frame.SendTo([&](const void* buf, size_t len) {
            write_calls++;
            const auto* b = static_cast<const uint8_t*>(buf);
            peer_wire.insert(peer_wire.end(), b, b + len);
            return true;
        }));
        ASSERT_EQ(write_calls, 1);
        SessionRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(peer_wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 1);
        ASSERT_EQ(rec.last_payload_len, 1400U);
        ASSERT_EQ(rec.last_dest_ip, 0x0A72FFFFU);
    }
    ASSERT_EQ(frame.EncodeCount(), 1U);

    // 2. Large frame (> 2 KiB): 32-byte header prefix + zero-copy payload
    std::vector<uint8_t> large_payload(32768, 0x7A);
    hdr.data_length = static_cast<uint32_t>(large_payload.size());
    P2pProxyDataFrame large_frame;
    ASSERT_TRUE(large_frame.Prepare(hdr, large_payload.data(), large_payload.size()));
    ASSERT_TRUE(large_frame.IsPrepared());
    ASSERT_FALSE(large_frame.IsSmallFrame());
    ASSERT_EQ(large_frame.WireSize(), 32U + 32768U);

    std::vector<uint8_t> large_wire;
    const void* second_write_ptr = nullptr;
    int write_calls = 0;
    ASSERT_TRUE(large_frame.SendTo([&](const void* buf, size_t len) {
        write_calls++;
        if (write_calls == 2) {
            second_write_ptr = buf;
        }
        const auto* b = static_cast<const uint8_t*>(buf);
        large_wire.insert(large_wire.end(), b, b + len);
        return true;
    }));
    ASSERT_EQ(write_calls, 2);
    // Verify zero-copy: second write pointer is the caller's exact buffer!
    ASSERT_EQ(second_write_ptr, static_cast<const void*>(large_payload.data()));

    SessionRecorder rec;
    ASSERT_TRUE(rec.FeedSocketReads(large_wire, 0x4000));
    ASSERT_EQ(rec.proxy_data_count, 1);
    ASSERT_EQ(rec.last_payload_len, 32768U);

    // 3. Error cases: null payload with non-zero length, or oversized length
    P2pProxyDataFrame bad_frame;
    ASSERT_FALSE(bad_frame.SendTo([](const void*, size_t) { return true; }));
    ASSERT_FALSE(bad_frame.Prepare(hdr, nullptr, 100));
    ASSERT_FALSE(bad_frame.Prepare(hdr, large_payload.data(), MAX_PACKET_SIZE));
}

TEST(session_async_disconnect_during_large_packet_accumulation_and_callback) {
    // 1. Async disconnect while accumulating a 64 KiB large packet:
    //    Disconnect() only flips `connected = false` without touching `stream`;
    //    the receive thread then safely releases the large buffer on its own turn.
    {
        SessionRecorder rec;
        const auto pkt_64k = BuildProxyDataStream(1, 65500, 0x5A);
        auto handlers = rec.MakeHandlers();

        // Feed first 16 KiB via in-place socket read
        uint8_t* write_ptr = rec.stream.WritePtr();
        std::memcpy(write_ptr, pkt_64k.data(), 0x4000);
        ASSERT_TRUE(ProcessSessionStreamData(rec.stream, write_ptr, 0x4000, handlers));
        ASSERT_TRUE(rec.stream.IsAccumulatingLargePacket());
        ASSERT_EQ(rec.stream.BufferedBytes(), 0x4000U);

        // Master thread marks session disconnected without resetting `rec.stream`
        rec.connected = false;
        ASSERT_TRUE(rec.stream.IsAccumulatingLargePacket());

        // Receive thread sees !is_session_active on next step and releases large buffer safely
        ASSERT_FALSE(ProcessSessionStreamData(rec.stream, pkt_64k.data() + 0x4000, 1024, handlers));
        ASSERT_FALSE(rec.stream.IsAccumulatingLargePacket());
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
        ASSERT_EQ(rec.proxy_data_count, 0);
    }

    // 2. Async disconnect mid-callback on a completed 32 KiB large packet:
    //    Payload remains valid for the entire callback, large buffer is freed on
    //    return, and trailing packet in the same chunk is not dispatched.
    {
        SessionRecorder rec;
        rec.stop_on_proxy_data = true;
        std::vector<uint8_t> wire = BuildProxyDataStream(1, 32768, 0xC3);
        const auto trailing = BuildProxyDataStream(1, 512, 0xD4);
        wire.insert(wire.end(), trailing.begin(), trailing.end());

        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.proxy_data_count, 1);
        ASSERT_EQ(rec.last_payload_len, 32768U);
        ASSERT_TRUE(rec.payload_valid_during_callback);
        ASSERT_FALSE(rec.connected);
        ASSERT_FALSE(rec.stream.IsAccumulatingLargePacket());
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 3. Large packet allocation failure across all 3 entry paths triggers
    //    on_protocol_error (Disconnect) and resets buffer cleanly.
    {
        const auto pkt_32k = BuildProxyDataStream(1, 32768, 0x7E);

        // Path A: In-place WritePtr()
        SessionRecorder rec_a;
        rec_a.stream.SetFailLargeAllocForTesting(true);
        ASSERT_FALSE(rec_a.FeedSocketReads(pkt_32k, 0x4000));
        ASSERT_FALSE(rec_a.connected);
        ASSERT_EQ(rec_a.disconnected_count, 1);
        ASSERT_EQ(rec_a.stream.BufferedBytes(), 0U);

        // Path B: Partial inline header promoted to large packet
        SessionRecorder rec_b;
        auto handlers_b = rec_b.MakeHandlers();
        ASSERT_TRUE(ProcessSessionStreamData(rec_b.stream, pkt_32k.data(), 6, handlers_b));
        rec_b.stream.SetFailLargeAllocForTesting(true);
        ASSERT_FALSE(ProcessSessionStreamData(rec_b.stream, pkt_32k.data() + 6, 100, handlers_b));
        ASSERT_FALSE(rec_b.connected);
        ASSERT_EQ(rec_b.disconnected_count, 1);
        ASSERT_EQ(rec_b.stream.BufferedBytes(), 0U);

        // Path C: External chunk path
        SessionRecorder rec_c;
        rec_c.stream.SetFailLargeAllocForTesting(true);
        ASSERT_FALSE(rec_c.FeedExternalChunks(pkt_32k, 0x4000));
        ASSERT_FALSE(rec_c.connected);
        ASSERT_EQ(rec_c.disconnected_count, 1);
        ASSERT_EQ(rec_c.stream.BufferedBytes(), 0U);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== P2P Proxy Server Tests ===\n\n");

    for (int i = 0; i < g_test_count; i++) {
        g_tests_run++;
        printf("Running: %s ... ", g_tests[i].name);

        try {
            g_tests[i].func();
            printf("PASS\n");
            g_tests_passed++;
        } catch (const std::exception& e) {
            g_tests_failed++;
        }
    }

    printf("\n=== Results ===\n");
    printf("Total:  %d\n", g_tests_run);
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
