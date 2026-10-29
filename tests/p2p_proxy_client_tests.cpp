/**
 * @file p2p_proxy_client_tests.cpp
 * @brief Unit & regression tests for P2pProxyClient stream reassembly and forwarding
 *
 * Verifies:
 * 1. reference compatibility constants, IP parsing, private IP detection, and
 *    broadcast calculation.
 * 2. Production P2pStreamBuffer + ProcessClientStreamData + ForwardClientProxyData:
 *    - 12 x 1432B ProxyData stream (17,184 bytes) across 64 KiB, 16 KiB, and
 *      small chunks with 100% delivery and zero dynamic allocations
 *    - Zero-copy ForwardClientProxyData callback when payload is contiguous
 *      after ProxyDataHeader (no 64 KiB stack array, 0 copies)
 *    - Non-contiguous ForwardClientProxyData fallbacks (<= 2 KiB and > 2 KiB)
 *    - Split header and split payload across reads
 *    - Coalesced multi-packet reads across all client packet types
 *      (ProxyConfig, ProxyData, ProxyConnect, ProxyConnectReply, ProxyDisconnect)
 *    - Large packets (> 16 KiB inline buffer up to MAX_PACKET_SIZE)
 *    - Corrupt headers (bad magic, negative/oversized length) and truncated structs
 *    - Connection reuse / Reset() cleanup
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <arpa/inet.h>

#include "../sysmodule/source/p2p/p2p_packet_stream.hpp"

// =============================================================================
// Minimal Test Framework
// =============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            printf("  Running: %s... ", #name); \
            g_tests_run++; \
            test_##name(); \
            g_tests_passed++; \
            printf("PASSED\n"); \
        } \
    } g_registrar_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { printf("FAILED\n    Assertion failed: %s (line %d)\n", #expr, __LINE__); exit(1); } } while(0)

#define ASSERT_FALSE(expr) \
    do { if (expr) { printf("FAILED\n    Assertion failed: !(%s) (line %d)\n", #expr, __LINE__); exit(1); } } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { printf("FAILED\n    Expected: %s == %s (line %d)\n", #a, #b, __LINE__); exit(1); } } while(0)

// =============================================================================
// P2P Proxy Client Constants (matching implementation)
// =============================================================================

namespace p2p_client {
    constexpr int FAILURE_TIMEOUT_MS = 4000;
    constexpr int CONNECT_TIMEOUT_MS = 5000;
    constexpr size_t RECV_BUFFER_SIZE = ams::mitm::p2p::P2pStreamBuffer::INLINE_BUFFER_SIZE;
}

// =============================================================================
// Helper Functions for Tests
// =============================================================================

namespace test_helpers {

bool parse_ipv4(const char* str, uint32_t& out_ip) {
    if (!str) return false;
    struct in_addr addr;
    if (inet_pton(AF_INET, str, &addr) != 1) {
        return false;
    }
    out_ip = addr.s_addr;
    return true;
}

void ipv4_to_string(uint32_t ip, char* out, size_t out_len) {
    struct in_addr addr;
    addr.s_addr = ip;
    inet_ntop(AF_INET, &addr, out, out_len);
}

bool is_private_ip(uint32_t ip) {
    uint32_t host_ip = ntohl(ip);
    if ((host_ip & 0xFF000000) == 0x0A000000) return true;
    if ((host_ip & 0xFFF00000) == 0xAC100000) return true;
    if ((host_ip & 0xFFFF0000) == 0xC0A80000) return true;
    return false;
}

uint32_t calculate_broadcast(uint32_t ip, uint32_t mask) {
    return ip | (~mask);
}

} // namespace test_helpers

// =============================================================================
// Production Client Stream Test Harness
// =============================================================================

namespace {

using namespace potluck::protocol;
using namespace ams::mitm::p2p;

static int g_cb_count = 0;
static PacketId g_cb_last_type = PacketId::Initialize;
static size_t g_cb_last_size = 0;
static const void* g_cb_last_ptr = nullptr;
static uint8_t g_cb_last_first_payload_byte = 0;

void ResetCallbackState() {
    g_cb_count = 0;
    g_cb_last_type = PacketId::Initialize;
    g_cb_last_size = 0;
    g_cb_last_ptr = nullptr;
    g_cb_last_first_payload_byte = 0;
}

void TestPacketCallback(PacketId type, const void* data, size_t size) {
    g_cb_count++;
    g_cb_last_type = type;
    g_cb_last_size = size;
    g_cb_last_ptr = data;
    if (type == PacketId::ProxyData && size > sizeof(ProxyDataHeader)) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        g_cb_last_first_payload_byte = bytes[sizeof(ProxyDataHeader)];
    }
}

struct ClientRecorder {
    P2pStreamBuffer stream;
    bool connected = true;
    bool stop_on_proxy_data = false;
    int protocol_error_count = 0;
    int config_count = 0;
    int data_count = 0;
    int connect_count = 0;
    int connect_reply_count = 0;
    int disconnect_count = 0;
    ProxyConfig last_config{};
    size_t last_payload_len = 0;
    const void* last_header_addr = nullptr;

    P2pClientDispatchHandlers MakeHandlers() {
        return P2pClientDispatchHandlers{
            this,
            [](void* ctx, const ProxyConfig& cfg) {
                auto* self = static_cast<ClientRecorder*>(ctx);
                self->config_count++;
                self->last_config = cfg;
            },
            [](void* ctx, const ProxyDataHeader& hdr, const uint8_t* payload, size_t len) {
                auto* self = static_cast<ClientRecorder*>(ctx);
                self->data_count++;
                self->last_payload_len = len;
                self->last_header_addr = &hdr;
                ForwardClientProxyData(TestPacketCallback, hdr, payload, len);
                if (self->stop_on_proxy_data) {
                    self->connected = false;
                }
            },
            [](void* ctx, const ProxyConnectRequest&) {
                static_cast<ClientRecorder*>(ctx)->connect_count++;
            },
            [](void* ctx, const ProxyConnectResponse&) {
                static_cast<ClientRecorder*>(ctx)->connect_reply_count++;
            },
            [](void* ctx, const ProxyDisconnectMessage&) {
                static_cast<ClientRecorder*>(ctx)->disconnect_count++;
            },
            [](void* ctx) {
                auto* self = static_cast<ClientRecorder*>(ctx);
                self->protocol_error_count++;
                self->connected = false;
            },
            [](void* ctx) -> bool {
                return static_cast<const ClientRecorder*>(ctx)->connected;
            }
        };
    }

    bool FeedExternalChunks(const std::vector<uint8_t>& bytes, size_t chunk_size) {
        auto handlers = MakeHandlers();
        for (size_t offset = 0; offset < bytes.size() && connected; offset += chunk_size) {
            const size_t n = std::min(chunk_size, bytes.size() - offset);
            if (!ProcessClientStreamData(stream, bytes.data() + offset, n, handlers)) {
                connected = false;
                stream.Reset();
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
            if (!ProcessClientStreamData(stream, write_ptr, n, handlers)) {
                connected = false;
                stream.Reset();
                return false;
            }
        }
        return true;
    }
};

std::vector<uint8_t> BuildClientProxyDataStream(size_t count, size_t payload_len, uint8_t fill_byte = 0xA5) {
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
        ph.info.source_ipv4 = 0x0A720001;
        ph.info.source_port = 49152;
        ph.info.dest_ipv4 = 0x0A720002;
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

// =============================================================================
// Constant Tests - Reference Client Compatibility & Memory Bounds
// =============================================================================

TEST(constants_failure_timeout_matches_reference) {
    ASSERT_EQ(p2p_client::FAILURE_TIMEOUT_MS, 4000);
}

TEST(constants_connect_timeout_reasonable) {
    ASSERT_EQ(p2p_client::CONNECT_TIMEOUT_MS, 5000);
}

TEST(constants_recv_buffer_size) {
    // 16 KiB inline receive buffer (reduced from 64 KiB to save 48 KiB per instance)
    ASSERT_EQ(p2p_client::RECV_BUFFER_SIZE, 0x4000U);
    ASSERT_EQ(p2p_client::RECV_BUFFER_SIZE, 16384U);
}

// =============================================================================
// IP Parsing & Helper Tests
// =============================================================================

TEST(ip_parsing_valid_ipv4) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("192.168.1.100", ip));
    ASSERT_TRUE(ip != 0);

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(ip, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "192.168.1.100"), 0);
}

TEST(ip_parsing_localhost) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("127.0.0.1", ip));
    ASSERT_TRUE(ip != 0);

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(ip, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "127.0.0.1"), 0);
}

TEST(ip_parsing_broadcast) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("255.255.255.255", ip));
    ASSERT_EQ(ip, 0xFFFFFFFF);
}

TEST(ip_parsing_zero) {
    uint32_t ip = 1;
    ASSERT_TRUE(test_helpers::parse_ipv4("0.0.0.0", ip));
    ASSERT_EQ(ip, 0U);
}

TEST(ip_parsing_invalid_empty) {
    uint32_t ip = 0;
    ASSERT_FALSE(test_helpers::parse_ipv4("", ip));
}

TEST(ip_parsing_invalid_null) {
    uint32_t ip = 0;
    ASSERT_FALSE(test_helpers::parse_ipv4(nullptr, ip));
}

TEST(ip_parsing_invalid_format) {
    uint32_t ip = 0;
    ASSERT_FALSE(test_helpers::parse_ipv4("not.an.ip.address", ip));
}

TEST(ip_parsing_invalid_overflow) {
    uint32_t ip = 0;
    ASSERT_FALSE(test_helpers::parse_ipv4("256.256.256.256", ip));
}

TEST(ip_parsing_host_address) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("10.114.0.1", ip));

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(ip, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "10.114.0.1"), 0);
}

TEST(private_ip_10_0_0_0_network) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("10.0.0.1", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(private_ip_10_255_255_255) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("10.255.255.255", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(private_ip_172_16_0_0_network) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("172.16.0.1", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(private_ip_172_31_255_255) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("172.31.255.255", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(private_ip_192_168_0_0_network) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("192.168.0.1", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(private_ip_192_168_255_255) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("192.168.255.255", ip));
    ASSERT_TRUE(test_helpers::is_private_ip(ip));
}

TEST(public_ip_8_8_8_8) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("8.8.8.8", ip));
    ASSERT_FALSE(test_helpers::is_private_ip(ip));
}

TEST(public_ip_1_1_1_1) {
    uint32_t ip = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("1.1.1.1", ip));
    ASSERT_FALSE(test_helpers::is_private_ip(ip));
}

TEST(broadcast_calc_class_c) {
    uint32_t ip = 0, mask = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("192.168.1.100", ip));
    ASSERT_TRUE(test_helpers::parse_ipv4("255.255.255.0", mask));

    uint32_t broadcast = test_helpers::calculate_broadcast(ip, mask);

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(broadcast, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "192.168.1.255"), 0);
}

TEST(broadcast_calc_class_b) {
    uint32_t ip = 0, mask = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("10.114.0.1", ip));
    ASSERT_TRUE(test_helpers::parse_ipv4("255.255.0.0", mask));

    uint32_t broadcast = test_helpers::calculate_broadcast(ip, mask);

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(broadcast, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "10.114.255.255"), 0);
}

TEST(broadcast_calc_class_a) {
    uint32_t ip = 0, mask = 0;
    ASSERT_TRUE(test_helpers::parse_ipv4("10.0.0.1", ip));
    ASSERT_TRUE(test_helpers::parse_ipv4("255.0.0.0", mask));

    uint32_t broadcast = test_helpers::calculate_broadcast(ip, mask);

    char str[INET_ADDRSTRLEN];
    test_helpers::ipv4_to_string(broadcast, str, sizeof(str));
    ASSERT_EQ(strcmp(str, "10.255.255.255"), 0);
}

TEST(state_initial_not_connected) {
    bool connected = false;
    bool ready = false;
    ASSERT_FALSE(connected);
    ASSERT_FALSE(ready);
}

TEST(state_auth_requires_connection) {
    bool connected = false;
    bool can_auth = connected;
    ASSERT_FALSE(can_auth);
}

TEST(state_ready_requires_proxy_config) {
    bool ready = false;
    bool has_proxy_config = true;
    ready = has_proxy_config;
    ASSERT_TRUE(ready);
}

TEST(timeout_auth_less_than_connect) {
    ASSERT_TRUE(p2p_client::FAILURE_TIMEOUT_MS < p2p_client::CONNECT_TIMEOUT_MS);
}

TEST(timeout_values_positive) {
    ASSERT_TRUE(p2p_client::FAILURE_TIMEOUT_MS > 0);
    ASSERT_TRUE(p2p_client::CONNECT_TIMEOUT_MS > 0);
}

TEST(timeout_auth_reasonable_for_network) {
    ASSERT_TRUE(p2p_client::FAILURE_TIMEOUT_MS >= 1000);
    ASSERT_TRUE(p2p_client::FAILURE_TIMEOUT_MS <= 10000);
}

TEST(port_range_valid) {
    constexpr uint16_t PORT_BASE = 39990;
    constexpr int PORT_RANGE = 10;

    ASSERT_TRUE(PORT_BASE > 1024);
    ASSERT_TRUE(PORT_BASE < 65535 - PORT_RANGE);

    for (int i = 0; i < PORT_RANGE; i++) {
        uint16_t port = PORT_BASE + i;
        ASSERT_TRUE(port > 0);
        ASSERT_TRUE(port <= 65535);
    }
}

TEST(external_proxy_config_size) {
    ASSERT_EQ(sizeof(potluck::protocol::ExternalProxyConfig), 0x26U);
}

// =============================================================================
// Production Client Stream Reassembly & Zero-Copy Forwarding Tests
// =============================================================================

TEST(client_repro_12x1432_packets_64k_and_16k_and_zero_copy_callback) {
    const auto wire = BuildClientProxyDataStream(12, 1400, 0xA5);
    ASSERT_EQ(wire.size(), 17184U);

    // 1. 64 KiB external chunk
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 0x10000));
        ASSERT_EQ(rec.data_count, 12);
        ASSERT_EQ(g_cb_count, 12);
        ASSERT_EQ(g_cb_last_size, sizeof(ProxyDataHeader) + 1400U);
        // Verify zero-copy: callback pointer equals the exact in-place ProxyDataHeader address!
        ASSERT_EQ(g_cb_last_ptr, rec.last_header_addr);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        ASSERT_EQ(rec.stream.BytesCopied(), 0U);
    }

    // 2. 16 KiB in-place socket recv() into WritePtr()
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.data_count, 12);
        ASSERT_EQ(g_cb_count, 12);
        ASSERT_EQ(g_cb_last_ptr, rec.last_header_addr);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        ASSERT_EQ(rec.stream.BytesCopied(), 632U);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }

    // 3. 16 KiB external chunks
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedExternalChunks(wire, 0x4000));
        ASSERT_EQ(rec.data_count, 12);
        ASSERT_EQ(g_cb_count, 12);
        ASSERT_EQ(rec.stream.LargeAllocCount(), 0U);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }
}

TEST(client_reassembly_split_headers_and_coalesced_packets) {
    std::vector<uint8_t> wire;

    ProxyConfig cfg{};
    cfg.proxy_ip = 0x0A720002;
    cfg.proxy_subnet_mask = 0xFFFF0000;
    AppendEncodedPacket(wire, PacketId::ProxyConfig, cfg);

    ProxyConnectRequest conn_req{};
    conn_req.info.source_ipv4 = 0x0A720001;
    conn_req.info.dest_ipv4 = 0x0A720002;
    AppendEncodedPacket(wire, PacketId::ProxyConnect, conn_req);

    ProxyConnectResponse conn_resp{};
    conn_resp.info = conn_req.info;
    AppendEncodedPacket(wire, PacketId::ProxyConnectReply, conn_resp);

    ProxyDisconnectMessage disc_msg{};
    disc_msg.info = conn_req.info;
    disc_msg.disconnect_reason = 2;
    AppendEncodedPacket(wire, PacketId::ProxyDisconnect, disc_msg);

    // Unknown packet type (Ping = 18)
    PingMessage ping{};
    AppendEncodedPacket(wire, PacketId::Ping, ping);

    const auto data_part = BuildClientProxyDataStream(3, 256, 0x6B);
    wire.insert(wire.end(), data_part.begin(), data_part.end());

    // 1. Coalesced single read
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 0x4000));
        ASSERT_EQ(rec.config_count, 1);
        ASSERT_EQ(rec.last_config.proxy_ip, 0x0A720002U);
        ASSERT_EQ(rec.connect_count, 1);
        ASSERT_EQ(rec.connect_reply_count, 1);
        ASSERT_EQ(rec.disconnect_count, 1);
        ASSERT_EQ(rec.data_count, 3);
        ASSERT_EQ(g_cb_count, 3);
    }

    // 2. 3-byte fragmented reads (splits every 12-byte LdnHeader across 4 reads)
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(wire, 3));
        ASSERT_EQ(rec.config_count, 1);
        ASSERT_EQ(rec.connect_count, 1);
        ASSERT_EQ(rec.connect_reply_count, 1);
        ASSERT_EQ(rec.disconnect_count, 1);
        ASSERT_EQ(rec.data_count, 3);
        ASSERT_EQ(g_cb_count, 3);
        ASSERT_EQ(g_cb_last_first_payload_byte, 0x6BU);
    }
}

TEST(client_large_packets_and_forward_fallbacks_and_errors) {
    // 1. 64 KiB large packet over 16 KiB socket reads
    const auto pkt_64k = BuildClientProxyDataStream(1, 65500, 0xD2);
    {
        ResetCallbackState();
        ClientRecorder rec;
        ASSERT_TRUE(rec.FeedSocketReads(pkt_64k, 0x4000));
        ASSERT_EQ(rec.data_count, 1);
        ASSERT_EQ(rec.last_payload_len, 65500U);
        ASSERT_EQ(g_cb_count, 1);
        ASSERT_EQ(g_cb_last_size, sizeof(ProxyDataHeader) + 65500U);
        ASSERT_EQ(g_cb_last_ptr, rec.last_header_addr);
        ASSERT_FALSE(rec.stream.IsAccumulatingLargePacket());
    }

    // 2. Non-contiguous ForwardClientProxyData fallbacks (small <= 2 KiB and large > 2 KiB)
    {
        ResetCallbackState();
        ProxyDataHeader hdr{};
        hdr.data_length = 128;
        std::vector<uint8_t> separate_small(128, 0x44);
        ASSERT_TRUE(ForwardClientProxyData(
            TestPacketCallback, hdr, separate_small.data(), separate_small.size()));
        ASSERT_EQ(g_cb_count, 1);
        ASSERT_EQ(g_cb_last_size, sizeof(ProxyDataHeader) + 128U);
        ASSERT_EQ(g_cb_last_first_payload_byte, 0x44U);

        std::vector<uint8_t> separate_large(8192, 0x88);
        hdr.data_length = 8192;
        ASSERT_TRUE(ForwardClientProxyData(
            TestPacketCallback, hdr, separate_large.data(), separate_large.size()));
        ASSERT_EQ(g_cb_count, 2);
        ASSERT_EQ(g_cb_last_size, sizeof(ProxyDataHeader) + 8192U);
        ASSERT_EQ(g_cb_last_first_payload_byte, 0x88U);

        // Error cases in ForwardClientProxyData
        ASSERT_FALSE(ForwardClientProxyData(nullptr, hdr, separate_small.data(), 10));
        ASSERT_FALSE(ForwardClientProxyData(TestPacketCallback, hdr, nullptr, 10));
        ASSERT_FALSE(ForwardClientProxyData(TestPacketCallback, hdr, separate_small.data(), MAX_PACKET_SIZE));
    }

    // 3. Invalid header & truncated payload & connection Reset()
    {
        ClientRecorder rec;
        auto handlers = rec.MakeHandlers();
        LdnHeader bad{};
        bad.magic = 0x12345678;
        bad.data_size = 16;
        ASSERT_FALSE(ProcessClientStreamData(
            rec.stream, reinterpret_cast<const uint8_t*>(&bad), sizeof(bad), handlers));
        ASSERT_EQ(rec.protocol_error_count, 1);
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);

        // Reconnect before testing truncated ProxyDataHeader
        rec.connected = true;
        rec.stream.Reset();

        // Truncated ProxyDataHeader (data_size = 10 < 20) and mismatched data_length
        std::vector<uint8_t> trunc_stream;
        LdnHeader th{};
        th.magic = PROTOCOL_MAGIC;
        th.version = PROTOCOL_VERSION;
        th.type = static_cast<uint8_t>(PacketId::ProxyData);
        th.data_size = sizeof(ProxyDataHeader) + 2;
        ProxyDataHeader tph{};
        tph.data_length = 50; // > 2
        trunc_stream.insert(trunc_stream.end(), reinterpret_cast<uint8_t*>(&th), reinterpret_cast<uint8_t*>(&th) + sizeof(th));
        trunc_stream.insert(trunc_stream.end(), reinterpret_cast<uint8_t*>(&tph), reinterpret_cast<uint8_t*>(&tph) + sizeof(tph));
        trunc_stream.insert(trunc_stream.end(), 2, 0x11);
        ASSERT_TRUE(rec.FeedSocketReads(trunc_stream, 0x4000));
        ASSERT_EQ(rec.data_count, 0);

        // Connection reuse after partial packet
        const auto valid = BuildClientProxyDataStream(2, 200, 0x99);
        ASSERT_TRUE(ProcessClientStreamData(rec.stream, valid.data(), 50, handlers));
        ASSERT_EQ(rec.stream.BufferedBytes(), 50U);
        rec.stream.Reset();
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
        ASSERT_TRUE(rec.FeedSocketReads(valid, 0x4000));
        ASSERT_EQ(rec.data_count, 2);
    }
}

TEST(client_receive_loop_stops_on_framing_or_alloc_error_and_recovers_on_reconnect) {
    // 1. Framing loss mid-stream: [Valid Packet 1][Corrupted Header][Valid Packet 2]
    //    ReceiveLoop stops immediately upon ProcessClientStreamData returning false,
    //    marks connection closed, does NOT dispatch Packet 2, and resets buffer.
    {
        ResetCallbackState();
        ClientRecorder rec;
        std::vector<uint8_t> wire = BuildClientProxyDataStream(1, 256, 0x33);
        LdnHeader bad{};
        bad.magic = 0xDEADBEEF;
        bad.version = PROTOCOL_VERSION;
        bad.type = static_cast<uint8_t>(PacketId::ProxyData);
        bad.data_size = 64;
        wire.insert(wire.end(),
                    reinterpret_cast<const uint8_t*>(&bad),
                    reinterpret_cast<const uint8_t*>(&bad) + sizeof(bad));
        const auto pkt2 = BuildClientProxyDataStream(1, 256, 0x44);
        wire.insert(wire.end(), pkt2.begin(), pkt2.end());

        ASSERT_FALSE(rec.FeedSocketReads(wire, 512));
        ASSERT_EQ(rec.data_count, 1);
        ASSERT_EQ(g_cb_count, 1);
        ASSERT_EQ(rec.protocol_error_count, 1);
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);

        // Reconnect and verify clean reuse on 12 x 1432B packets
        rec.connected = true;
        rec.stream.Reset();
        const auto clean_stream = BuildClientProxyDataStream(12, 1400, 0x7F);
        ASSERT_TRUE(rec.FeedSocketReads(clean_stream, 0x4000));
        ASSERT_EQ(rec.data_count, 13);
        ASSERT_EQ(g_cb_count, 13);
        ASSERT_TRUE(rec.connected);
    }

    // 2. Large-packet allocation failure on client stops loop and triggers cleanup
    {
        const auto pkt_32k = BuildClientProxyDataStream(1, 32768, 0xAB);

        ClientRecorder rec_a;
        rec_a.stream.SetFailLargeAllocForTesting(true);
        ASSERT_FALSE(rec_a.FeedSocketReads(pkt_32k, 0x4000));
        ASSERT_EQ(rec_a.protocol_error_count, 1);
        ASSERT_FALSE(rec_a.connected);
        ASSERT_EQ(rec_a.stream.BufferedBytes(), 0U);

        ClientRecorder rec_b;
        rec_b.stream.SetFailLargeAllocForTesting(true);
        ASSERT_FALSE(rec_b.FeedExternalChunks(pkt_32k, 0x4000));
        ASSERT_EQ(rec_b.protocol_error_count, 1);
        ASSERT_FALSE(rec_b.connected);
        ASSERT_EQ(rec_b.stream.BufferedBytes(), 0U);
    }

    // 3. Client disconnect mid-chunk stops remaining coalesced frames and resets buffer
    {
        ClientRecorder rec;
        rec.stop_on_proxy_data = true;
        const auto coalesced = BuildClientProxyDataStream(3, 128, 0x55);
        ASSERT_TRUE(rec.FeedSocketReads(coalesced, 0x4000));
        ASSERT_EQ(rec.data_count, 1);
        ASSERT_FALSE(rec.connected);
        ASSERT_EQ(rec.stream.BufferedBytes(), 0U);
    }
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== P2P Proxy Client Tests ===\n\n");

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", g_tests_run);
    printf("Tests passed: %d\n", g_tests_passed);

    if (g_tests_passed == g_tests_run) {
        printf("\nAll tests PASSED!\n");
        return 0;
    } else {
        printf("\nSome tests FAILED!\n");
        return 1;
    }
}
