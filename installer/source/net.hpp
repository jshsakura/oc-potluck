#pragma once
#include <functional>
#include <string>

namespace net {

struct Latest {
    bool ok = false;
    std::string tag, url, error;
    long size = 0;
};

bool init();
void exit();
/** The newest release of the repository: its tag and the potluck-v*.zip asset. */
Latest fetchLatest();
/** Save `url` to `path`; `progress(now, total)` returns false to cancel. */
bool download(const std::string& url, const std::string& path, long expected,
              const std::function<bool(long, long)>& progress, std::string& error);

/** Open a TCP connection to host:port and close it. Returns the time in ms, -1 when it cannot connect, -2 when the name does not resolve. */
/// probe() failures; zero or more is the connect time in milliseconds
constexpr int ProbeFailed = -1;    // no answer in time, or another socket error
constexpr int ProbeNotFound = -2;  // the name does not exist
constexpr int ProbeIpv6Only = -3;  // the name has only IPv6 addresses
constexpr int ProbeDnsDown = -4;   // the DNS server did not answer
constexpr int ProbeRefused = -5;   // the host answered but nothing listens on the port
int probe(const std::string& host, int port, int timeoutMs);

} // namespace net
