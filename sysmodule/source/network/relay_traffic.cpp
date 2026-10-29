/**
 * @file relay_traffic.cpp
 * @brief Counting and logging of relay connection frames.
 */

#include "relay_traffic.hpp"
#include "../debug/log.hpp"
#include "../protocol/relay_protocol.hpp"
#include <atomic>
#include <cstdio>

namespace potluck::network::traffic {

namespace {

constexpr uint8_t NoPacket = 0xFF;

std::atomic<uint32_t> g_control_sent{0};
std::atomic<uint32_t> g_control_received{0};
std::atomic<uint32_t> g_data_sent{0};
std::atomic<uint32_t> g_data_received{0};
std::atomic<uint64_t> g_data_bytes_sent{0};
std::atomic<uint64_t> g_data_bytes_received{0};
std::atomic<uint8_t>  g_last_received{NoPacket};

// LogWindow's own state: only the receive thread calls it.
uint64_t g_window_start_ms = 0;
Totals   g_window_base{};

bool IsData(protocol::PacketId id) { return id == protocol::PacketId::ProxyData; }

// The server pings an idle connection every 10 seconds and the module measures latency every 5,
// so pings are only worth a line at the verbose level.
void LogControl(const char* arrow, protocol::PacketId id, size_t frame_bytes) {
    if (id == protocol::PacketId::Ping) {
        LOG_VERBOSE("%s Ping (%zu B)", arrow, frame_bytes);
    } else {
        LOG_INFO("%s %s (%zu B)", arrow, protocol::packet_id_to_string(id), frame_bytes);
    }
}

// Bytes in KB with one decimal, without floating point.
void FormatKb(uint64_t bytes, char* out, size_t size) {
    const uint64_t tenths = (bytes * 10 + 512) / 1024;
    snprintf(out, size, "%llu.%llu KB", static_cast<unsigned long long>(tenths / 10),
             static_cast<unsigned long long>(tenths % 10));
}

} // namespace

void Reset() {
    g_control_sent = 0;
    g_control_received = 0;
    g_data_sent = 0;
    g_data_received = 0;
    g_data_bytes_sent = 0;
    g_data_bytes_received = 0;
    g_last_received = NoPacket;
    g_window_start_ms = 0;
    g_window_base = Totals{};
}

void RecordSent(protocol::PacketId id, size_t frame_bytes) {
    if (IsData(id)) {
        g_data_sent.fetch_add(1, std::memory_order_relaxed);
        g_data_bytes_sent.fetch_add(frame_bytes, std::memory_order_relaxed);
        return;
    }
    g_control_sent.fetch_add(1, std::memory_order_relaxed);
    LogControl("-> server:", id, frame_bytes);
}

void RecordReceived(protocol::PacketId id, size_t frame_bytes) {
    if (IsData(id)) {
        g_data_received.fetch_add(1, std::memory_order_relaxed);
        g_data_bytes_received.fetch_add(frame_bytes, std::memory_order_relaxed);
        return;
    }
    g_control_received.fetch_add(1, std::memory_order_relaxed);
    g_last_received.store(static_cast<uint8_t>(id), std::memory_order_relaxed);
    LogControl("<- server:", id, frame_bytes);
}

Totals Snapshot() {
    Totals t{};
    t.control_sent = g_control_sent.load(std::memory_order_relaxed);
    t.control_received = g_control_received.load(std::memory_order_relaxed);
    t.data_sent = g_data_sent.load(std::memory_order_relaxed);
    t.data_received = g_data_received.load(std::memory_order_relaxed);
    t.data_bytes_sent = g_data_bytes_sent.load(std::memory_order_relaxed);
    t.data_bytes_received = g_data_bytes_received.load(std::memory_order_relaxed);
    t.last_received = g_last_received.load(std::memory_order_relaxed);
    return t;
}

void LogWindow(uint64_t now_ms) {
    if (g_window_start_ms == 0) {
        g_window_start_ms = now_ms;
        g_window_base = Snapshot();
        return;
    }
    if (now_ms - g_window_start_ms < WindowMs) return;

    const Totals now = Snapshot();
    const uint32_t sent = now.data_sent - g_window_base.data_sent;
    const uint32_t received = now.data_received - g_window_base.data_received;
    if (sent != 0 || received != 0) {
        char sent_kb[24], received_kb[24];
        FormatKb(now.data_bytes_sent - g_window_base.data_bytes_sent, sent_kb, sizeof(sent_kb));
        FormatKb(now.data_bytes_received - g_window_base.data_bytes_received, received_kb, sizeof(received_kb));
        LOG_INFO("Game data, last %llu s: sent %u packets (%s), received %u packets (%s)",
                 static_cast<unsigned long long>((now_ms - g_window_start_ms) / 1000), sent, sent_kb,
                 received, received_kb);
    }
    g_window_start_ms = now_ms;
    g_window_base = now;
}

} // namespace potluck::network::traffic
