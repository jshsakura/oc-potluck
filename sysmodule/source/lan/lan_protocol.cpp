/**
 * @file lan_protocol.cpp
 * @brief ldn_mitm compatible LAN packet format
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @copyright Copyright (c) 2019 spacemeowx2 (ldn_mitm, original protocol)
 * @license GPL-2.0-or-later
 */

#include "lan_protocol.hpp"

#include <cstring>

namespace potluck::lan {

namespace {

constexpr uint8_t MAX_RUN = 0xFF;

} // namespace

int compress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size) {
    const uint8_t* in = input;
    const uint8_t* const in_end = input + input_size;
    uint8_t* out = output;
    uint8_t* const out_end = output + *output_size;

    while (in < in_end) {
        if (out == out_end) return -1;
        const uint8_t c = *in++;
        *out++ = c;
        if (c != 0) continue;

        // A zero is followed by how many more zeros it stands for
        uint8_t run = 0;
        while (in < in_end && *in == 0 && run < MAX_RUN) {
            ++run;
            ++in;
        }
        if (out == out_end) return -1;
        *out++ = run;
    }

    *output_size = static_cast<size_t>(out - output);
    return 0;
}

int decompress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size) {
    const uint8_t* in = input;
    const uint8_t* const in_end = input + input_size;
    uint8_t* out = output;
    uint8_t* const out_end = output + *output_size;

    while (in < in_end) {
        if (out == out_end) return -1;
        const uint8_t c = *in++;
        *out++ = c;
        if (c != 0) continue;

        if (in == in_end) return -1;
        const uint8_t run = *in++;
        if (static_cast<size_t>(out_end - out) < run) return -1;
        std::memset(out, 0, run);
        out += run;
    }

    *output_size = static_cast<size_t>(out - output);
    return 0;
}

size_t encode_packet(PacketType type, const void* body, size_t body_size, uint8_t* out, size_t out_capacity) {
    if (body == nullptr) body_size = 0;
    if (body_size > MAX_BODY || out_capacity < sizeof(PacketHeader)) return 0;

    PacketHeader header{};
    header.magic = MAGIC;
    header.type = type;

    uint8_t* const payload = out + sizeof(PacketHeader);
    const size_t room = out_capacity - sizeof(PacketHeader);
    size_t packed = room < body_size ? room : body_size;

    if (body_size > 0 && compress(static_cast<const uint8_t*>(body), body_size, payload, &packed) == 0 &&
        packed < body_size) {
        header.compressed = 1;
        header.length = static_cast<uint16_t>(packed);
        header.decompress_length = static_cast<uint16_t>(body_size);
    } else {
        if (body_size > room) return 0;
        if (body_size > 0) std::memcpy(payload, body, body_size);
        header.length = static_cast<uint16_t>(body_size);
    }

    std::memcpy(out, &header, sizeof(header));
    return sizeof(header) + header.length;
}

bool PacketReader::Append(const void* data, size_t size) {
    if (size > sizeof(m_buffer) - m_used) {
        m_used = 0;
        return false;
    }
    std::memcpy(m_buffer + m_used, data, size);
    m_used += size;
    return true;
}

bool PacketReader::Next(PacketType* type, uint8_t* body, size_t* body_size, bool* error) {
    *error = false;
    if (m_used < sizeof(PacketHeader)) return false;

    PacketHeader header;
    std::memcpy(&header, m_buffer, sizeof(header));
    const size_t total = sizeof(PacketHeader) + header.length;
    if (header.magic != MAGIC || total > sizeof(m_buffer)) {
        m_used = 0;
        *error = true;
        return false;
    }
    if (m_used < total) return false;

    const uint8_t* const payload = m_buffer + sizeof(PacketHeader);
    bool ok = true;
    if (header.compressed) {
        size_t decoded = MAX_BODY;
        ok = decompress(payload, header.length, body, &decoded) == 0 && decoded == header.decompress_length;
        *body_size = decoded;
    } else {
        std::memcpy(body, payload, header.length);
        *body_size = header.length;
    }
    *type = header.type;

    m_used -= total;
    std::memmove(m_buffer, m_buffer + total, m_used);

    if (!ok) {
        *error = true;
        return false;
    }
    return true;
}

} // namespace potluck::lan
