#include "safety.hpp"
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <switch.h>

namespace inst {

namespace {

// Atmosphere's exosphere config items (spl): the running system's own answers.
constexpr u32 ConfigBlankProdInfo = 65005;
constexpr u32 ConfigEmummcType = 65007;

std::string readFile(const char* path) {
    std::string out;
    FILE* f = fopen(path, "rb");
    if (!f) return out;
    char buffer[1024];
    size_t n;
    while (out.size() < 16384 && (n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return out;
}

Lookup lookup(const char* domain, std::vector<Addr>& addrs) {
    addrinfo hints{};
    hints.ai_family = AF_INET;  // the redirect (hosts file or DNS) is an IPv4 answer; games connect over IPv4
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const int rc = getaddrinfo(domain, nullptr, &hints, &list);
    if (rc == 0) {
        for (addrinfo* p = list; p; p = p->ai_next) {
            Addr a;
            if (p->ai_family == AF_INET) {
                a.v6 = false;
                std::memcpy(a.b.data(), &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr, 4);
            } else if (p->ai_family == AF_INET6) {
                a.v6 = true;
                std::memcpy(a.b.data(), &reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr, 16);
            } else {
                continue;
            }
            addrs.push_back(a);
        }
        freeaddrinfo(list);
        return Lookup::Ok;
    }
    return rc == EAI_NONAME ? Lookup::NxDomain : Lookup::Failed;
}

std::string addrText(const Addr& a) {
    char buffer[64];
    if (a.v6) return "IPv6";
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d", a.b[0], a.b[1], a.b[2], a.b[3]);
    return buffer;
}

Reach checkDns(bool online, std::vector<std::string>& detail) {
    std::array<Reach, BlockDomainCount> per;
    per.fill(Reach::Unknown);
    if (!online) return overallReach(per);
    for (int i = 0; i < BlockDomainCount; ++i) {
        std::vector<Addr> addrs;
        const Lookup status = lookup(BlockDomains[i], addrs);
        per[i] = domainReach(status, addrs);
        std::string line = std::string(BlockDomains[i]) + ": ";
        if (status == Lookup::NxDomain) line += "NXDOMAIN";
        else if (status == Lookup::Failed) line += "?";
        else for (size_t k = 0; k < addrs.size() && k < 2; ++k) line += (k ? ", " : "") + addrText(addrs[k]);
        detail.push_back(line);
    }
    return overallReach(per);
}

} // namespace

Facts gatherFacts(bool online) {
    Facts f;
    f.dns = checkDns(online, f.dnsDetail);

    bool serialOk = false;
    std::string serial;
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysSerialNumber sn{};
        if (R_SUCCEEDED(setsysGetSerialNumber(&sn))) {
            serialOk = true;
            serial.assign(sn.number, strnlen(sn.number, sizeof(sn.number)));
        }
        setsysExit();
    }
    f.serial = serialKind(serial, serialOk);
    if (f.serial == Serial::Blank) f.blankSerial = serial;  // a real serial is never kept or shown

    f.exo = parseExosphere(readFile("sdmc:/exosphere.ini"));

    if (R_SUCCEEDED(splInitialize())) {
        u64 value = 0;
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(ConfigEmummcType), &value))) {
            f.mmc = value == 0 ? Mmc::Sys : Mmc::Emu;
        }
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(ConfigBlankProdInfo), &value))) f.blankRuntime = value ? 1 : 0;
        splExit();
    }
    // f.mmc stays Unknown when spl does not answer: only the running system can say where it booted from.
    // emummc.ini is what the next boot is asked to do: shown beside the result, never used as it.
    const std::string ini = readFile("sdmc:/emummc/emummc.ini");
    f.emummcIni = ini.empty() ? -2 : parseEmummcEnabled(ini);
    return f;
}

std::vector<uint64_t> installedApplications() {
    std::vector<uint64_t> ids;
    if (R_FAILED(nsInitialize())) return ids;
    constexpr s32 Chunk = 64, Limit = 4096;
    NsApplicationRecord records[Chunk];
    for (s32 offset = 0; offset < Limit;) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records, Chunk, offset, &count)) || count <= 0) break;
        for (s32 i = 0; i < count; ++i) if (records[i].application_id) ids.push_back(records[i].application_id);
        offset += count;
        if (count < Chunk) break;
    }
    nsExit();
    return ids;
}

} // namespace inst
