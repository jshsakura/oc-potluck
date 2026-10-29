#pragma once
/**
 * @file poll_split.hpp
 * @brief Splitting one poll() between the real BSD service and proxy sockets.
 *
 * The real service gets a copy with every proxy entry turned off (fd -1); the
 * proxy entries are remembered by index so their readiness is filled in after.
 * Indexes are kept apart from the copy, so a long array (most of it real or
 * ignored entries) never hides the proxy sockets in it.
 */

#include <cstddef>
#include <cstdint>
#include "bsd_types.hpp"

namespace potluck::bsd {

struct PollSplit {
    size_t proxies = 0;     ///< Proxy entries remembered in proxy_at
    bool any_real = false;  ///< An entry that is not a proxy socket (fd -1 included)
    bool dropped = false;   ///< More proxy entries than proxy_at holds; the rest read as idle
};

/**
 * @param in        The game's array
 * @param n         Its length
 * @param real      Receives the copy for the real service (n entries)
 * @param proxy_at  Receives the indexes of proxy entries
 * @param max_proxies Room in proxy_at
 * @param is_proxy  bool(int fd): whether fd is a proxy socket
 */
template <typename IsProxy>
PollSplit SplitPoll(const PollFd* in, size_t n, PollFd* real, uint16_t* proxy_at, size_t max_proxies, IsProxy is_proxy) {
    PollSplit split;
    for (size_t i = 0; i < n; i++) {
        real[i] = in[i];
        if (in[i].fd < 0 || !is_proxy(in[i].fd)) {
            split.any_real = true;
            continue;
        }
        real[i].fd = -1;
        if (split.proxies < max_proxies) {
            proxy_at[split.proxies++] = static_cast<uint16_t>(i);
        } else {
            split.dropped = true;
        }
    }
    return split;
}

} // namespace potluck::bsd
