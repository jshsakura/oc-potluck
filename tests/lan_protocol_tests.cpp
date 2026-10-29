/**
 * @file lan_protocol_tests.cpp
 * @brief Tests for the ldn_mitm compatible LAN packet format
 */

#include "lan/lan_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace potluck::lan;

static int g_failed = 0;
static int g_run = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

/// Bytes ldn_mitm itself puts on the wire for a Scan broadcast
static void scan_packet_matches_ldn_mitm() {
    uint8_t out[BUFFER_SIZE];
    const size_t n = encode_packet(PacketType::Scan, nullptr, 0, out, sizeof(out));
    const uint8_t expected[12] = {0x00, 0x14, 0x45, 0x11, 0x00, 0, 0, 0, 0, 0, 0, 0};
    CHECK(n == 12);
    CHECK(std::memcmp(out, expected, sizeof(expected)) == 0);
}

static void compress_round_trips_zero_runs() {
    std::vector<uint8_t> input(0x480, 0);
    input[0] = 7;
    input[300] = 0xAB;
    input[0x47F] = 1;
    uint8_t packed[0x480];
    size_t packed_size = sizeof(packed);
    CHECK(compress(input.data(), input.size(), packed, &packed_size) == 0);
    CHECK(packed_size < input.size());

    uint8_t unpacked[0x480];
    size_t unpacked_size = sizeof(unpacked);
    CHECK(decompress(packed, packed_size, unpacked, &unpacked_size) == 0);
    CHECK(unpacked_size == input.size());
    CHECK(std::memcmp(unpacked, input.data(), input.size()) == 0);
}

static void compress_splits_runs_longer_than_255() {
    std::vector<uint8_t> input(600, 0);
    uint8_t packed[16];
    size_t packed_size = sizeof(packed);
    CHECK(compress(input.data(), input.size(), packed, &packed_size) == 0);
    // 600 zeros: 0,255 (256) 0,255 (256) 0,87 (88)
    const uint8_t expected[] = {0, 255, 0, 255, 0, 87};
    CHECK(packed_size == sizeof(expected));
    CHECK(std::memcmp(packed, expected, sizeof(expected)) == 0);
}

static void compress_stops_at_input_end() {
    // A trailing zero must not read past the input to count its run
    uint8_t buffer[4] = {5, 0, 0, 0};
    uint8_t packed[8];
    size_t packed_size = sizeof(packed);
    CHECK(compress(buffer, 2, packed, &packed_size) == 0);
    const uint8_t expected[] = {5, 0, 0};
    CHECK(packed_size == sizeof(expected));
    CHECK(std::memcmp(packed, expected, sizeof(expected)) == 0);
}

static void compress_reports_overflow() {
    const uint8_t input[4] = {1, 2, 3, 4};
    uint8_t packed[3];
    size_t packed_size = sizeof(packed);
    CHECK(compress(input, sizeof(input), packed, &packed_size) == -1);

    const uint8_t zero[1] = {0};
    packed_size = 1;
    CHECK(compress(zero, sizeof(zero), packed, &packed_size) == -1);
}

static void decompress_rejects_bad_input() {
    uint8_t out[8];
    size_t out_size = sizeof(out);
    const uint8_t truncated[] = {1, 0};
    CHECK(decompress(truncated, sizeof(truncated), out, &out_size) == -1);

    out_size = sizeof(out);
    const uint8_t too_long[] = {0, 200};
    CHECK(decompress(too_long, sizeof(too_long), out, &out_size) == -1);

    out_size = 1;
    const uint8_t two[] = {1, 2};
    CHECK(decompress(two, sizeof(two), out, &out_size) == -1);
}

static void encode_keeps_incompressible_bodies_plain() {
    uint8_t body[64];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = static_cast<uint8_t>(i + 1);
    uint8_t out[BUFFER_SIZE];
    const size_t n = encode_packet(PacketType::Connect, body, sizeof(body), out, sizeof(out));
    CHECK(n == sizeof(PacketHeader) + sizeof(body));
    PacketHeader header;
    std::memcpy(&header, out, sizeof(header));
    CHECK(header.compressed == 0);
    CHECK(header.length == sizeof(body));
    CHECK(std::memcmp(out + sizeof(header), body, sizeof(body)) == 0);
}

static void encode_refuses_what_does_not_fit() {
    uint8_t out[BUFFER_SIZE];
    std::vector<uint8_t> big(MAX_BODY + 1, 1);
    CHECK(encode_packet(PacketType::SyncNetwork, big.data(), big.size(), out, sizeof(out)) == 0);
    CHECK(encode_packet(PacketType::Scan, nullptr, 0, out, 4) == 0);

    uint8_t plain[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(encode_packet(PacketType::Connect, plain, sizeof(plain), out, sizeof(PacketHeader) + 4) == 0);
}

static void reader_reassembles_split_and_joined_packets() {
    std::vector<uint8_t> info(0x480, 0);
    info[10] = 0x42;
    uint8_t first[BUFFER_SIZE];
    uint8_t second[BUFFER_SIZE];
    const size_t a = encode_packet(PacketType::SyncNetwork, info.data(), info.size(), first, sizeof(first));
    const size_t b = encode_packet(PacketType::Scan, nullptr, 0, second, sizeof(second));
    CHECK(a > 0 && b > 0);

    std::vector<uint8_t> stream(first, first + a);
    stream.insert(stream.end(), second, second + b);

    PacketReader reader;
    uint8_t body[MAX_BODY];
    size_t body_size = 0;
    PacketType type = PacketType::Scan;
    bool error = false;

    CHECK(reader.Append(stream.data(), 5));
    CHECK(!reader.Next(&type, body, &body_size, &error) && !error);
    CHECK(reader.Append(stream.data() + 5, stream.size() - 5));

    CHECK(reader.Next(&type, body, &body_size, &error));
    CHECK(type == PacketType::SyncNetwork);
    CHECK(body_size == info.size());
    CHECK(body[10] == 0x42);

    CHECK(reader.Next(&type, body, &body_size, &error));
    CHECK(type == PacketType::Scan && body_size == 0);
    CHECK(!reader.Next(&type, body, &body_size, &error) && !error);
    CHECK(reader.Buffered() == 0);
}

static void reader_drops_bad_magic_and_overflow() {
    PacketReader reader;
    uint8_t body[MAX_BODY];
    size_t body_size = 0;
    PacketType type;
    bool error = false;

    const uint8_t junk[12] = {1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(reader.Append(junk, sizeof(junk)));
    CHECK(!reader.Next(&type, body, &body_size, &error) && error);
    CHECK(reader.Buffered() == 0);

    PacketHeader header{};
    header.magic = MAGIC;
    header.length = static_cast<uint16_t>(BUFFER_SIZE);
    CHECK(reader.Append(&header, sizeof(header)));
    CHECK(!reader.Next(&type, body, &body_size, &error) && error);

    std::vector<uint8_t> big(BUFFER_SIZE + 1, 0);
    CHECK(!reader.Append(big.data(), big.size()));
    CHECK(reader.Buffered() == 0);
}

static void reader_rejects_length_mismatch() {
    PacketHeader header{};
    header.magic = MAGIC;
    header.type = PacketType::ScanResp;
    header.compressed = 1;
    header.length = 2;
    header.decompress_length = 10;  // the body decodes to 6 bytes
    const uint8_t payload[2] = {0, 5};

    PacketReader reader;
    CHECK(reader.Append(&header, sizeof(header)));
    CHECK(reader.Append(payload, sizeof(payload)));
    uint8_t body[MAX_BODY];
    size_t body_size = 0;
    PacketType type;
    bool error = false;
    CHECK(!reader.Next(&type, body, &body_size, &error) && error);
    CHECK(reader.Buffered() == 0);
}

int main() {
    std::printf("=== LAN protocol tests ===\n");
    void (*tests[])() = {
        scan_packet_matches_ldn_mitm,
        compress_round_trips_zero_runs,
        compress_splits_runs_longer_than_255,
        compress_stops_at_input_end,
        compress_reports_overflow,
        decompress_rejects_bad_input,
        encode_keeps_incompressible_bodies_plain,
        encode_refuses_what_does_not_fit,
        reader_reassembles_split_and_joined_packets,
        reader_drops_bad_magic_and_overflow,
        reader_rejects_length_mismatch,
    };
    for (auto test : tests) {
        ++g_run;
        test();
    }
    std::printf("%d tests, %d failures\n", g_run, g_failed);
    if (g_failed == 0) std::printf("All tests passed!\n");
    return g_failed == 0 ? 0 : 1;
}
