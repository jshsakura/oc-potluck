#pragma once
#include <chrono>
#include <cstring>
#include "socket.hpp"
#include "../protocol/types.hpp"

namespace potluck::network {
enum class ProbeReplyResult { Success, Timeout, ProtocolError };

/** @brief Receive and validate one complete Initialize response within a deadline. */
inline ProbeReplyResult ReceiveProbeReply(Socket& socket, uint32_t timeout_ms = 2500) {
    constexpr size_t reply_size = sizeof(protocol::LdnHeader) + sizeof(protocol::InitializeMessage);
    uint8_t reply[reply_size]{};
    size_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (received < reply_size) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return ProbeReplyResult::Timeout;
        size_t chunk = 0;
        const auto result = socket.recv(reply + received, reply_size - received, chunk,
                                        static_cast<int32_t>(remaining));
        if (result == SocketResult::Timeout || result == SocketResult::WouldBlock) return ProbeReplyResult::Timeout;
        if (result != SocketResult::Success || chunk == 0) return ProbeReplyResult::ProtocolError;
        received += chunk;
        if (received >= sizeof(protocol::LdnHeader)) {
            protocol::LdnHeader header{};
            std::memcpy(&header, reply, sizeof(header));
            if (header.magic != protocol::PROTOCOL_MAGIC || header.version != protocol::PROTOCOL_VERSION ||
                header.type != static_cast<uint8_t>(protocol::PacketId::Initialize) ||
                header.data_size != static_cast<int32_t>(sizeof(protocol::InitializeMessage))) {
                return ProbeReplyResult::ProtocolError;
            }
        }
    }
    return ProbeReplyResult::Success;
}
}
