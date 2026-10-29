// Pure rules of the installer: which SD paths a release may write, the install ledger,
// carrying older settings over, and the safety checks before the module may reach the internet.
// No console or file access here, so tests/ builds it on the host.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace inst {

constexpr const char* Tid = "4200000000504C4B";          // PotLuck's own title id
constexpr const char* LegacyTid = "4200000000000010";    // ldn_mitm's id, and where older builds installed
constexpr const char* ContentsDir = "atmosphere/contents/4200000000504C4B/";
constexpr const char* LegacyContentsDir = "atmosphere/contents/4200000000000010/";
constexpr const char* ConfigFile = "atmosphere/contents/4200000000504C4B/config.ini";
constexpr const char* OverlayPath = "switch/.overlays/potluck.ovl";
constexpr const char* LegacyOverlayPath = "switch/.overlays/ryu_ldn_nx_overlay.ovl";
constexpr const char* LegacyConfigDir = "config/ryu_ldn_nx/";
constexpr const char* Repo = "jshsakura/oc-potluck";
constexpr const char* BootFlag = "flags/boot2.flag";       // below a module's folder: Atmosphere starts it at boot

// A release zip is at most 20 MiB; what it unpacks to is bounded too, per file and in all.
constexpr long MaxZipBytes = 20L << 20;
constexpr unsigned long MaxEntryBytes = 16UL << 20;
constexpr unsigned long MaxUnpackedBytes = 64UL << 20;
// At most this many zip entries (the commit journal lists each one and must stay readable), and folders
// at most this deep below the module folder ("data/sub/file").
constexpr unsigned long MaxEntries = 256;
constexpr int MaxModuleSubfolders = 2;

inline bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
inline bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
/** ASCII lower case: the SD card is FAT, which treats "Flags" and "flags" as the same folder. */
inline std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

/** A zip entry name that stays inside the SD root: relative, no "..", no empty parts. */
inline bool safePath(const std::string& p) {
    if (p.empty() || p.size() > 200 || p[0] == '/') return false;
    if (p.find_first_of("\\:") != std::string::npos) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        const std::string part = p.substr(start, end - start);
        const bool last = end == p.size();
        if (part == "." || part == ".." || (part.empty() && !last)) return false;
        start = end + 1;
    }
    return true;
}

/** The only places a release may write: the module's own folder and its overlay. */
inline bool allowedTarget(const std::string& p) {
    if (!safePath(p) || p.back() == '/') return false;
    return startsWith(p, ContentsDir) || p == OverlayPath;
}

/** What a release may unpack: never a boot flag (the installer sets that itself once every file is in place),
 *  never a name of the installer's own swaps (.bak, .part), and nothing deeper than MaxModuleSubfolders. */
inline bool extractable(const std::string& p) {
    if (!allowedTarget(p)) return false;
    const std::string low = lower(p);
    if (startsWith(low, lower(ContentsDir) + "flags/") || endsWith(low, ".bak") || endsWith(low, ".part")) return false;
    if (!startsWith(p, ContentsDir)) return true;
    int folders = 0;
    for (size_t i = std::string(ContentsDir).size(); i < p.size(); ++i) folders += p[i] == '/';
    return folders <= MaxModuleSubfolders;
}

/** The user's settings: an update never replaces them. */
inline bool keepIfPresent(const std::string& p) { return lower(p) == lower(ConfigFile); }

/** toolbox.json of PotLuck, and of the older ryu_ldn_nx builds (same module, old name and id). */
inline bool isOwnToolbox(const std::string& json) { return json.find("\"potluck\"") != std::string::npos; }
inline bool isLegacyToolbox(const std::string& json) { return json.find("\"ryu_ldn_nx\"") != std::string::npos; }

inline std::string bareVersion(const std::string& tag) {
    return !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
}
inline bool sameVersion(const std::string& a, const std::string& b) { return bareVersion(a) == bareVersion(b); }

/** The release asset to download: potluck-v<version>.zip. */
inline bool isReleaseZip(const std::string& name) {
    return startsWith(name, "potluck-v") && endsWith(name, ".zip") && name.find('/') == std::string::npos;
}
inline bool releaseUrlOk(const std::string& url) {
    return startsWith(url, std::string("https://github.com/") + Repo + "/releases/download/");
}

/** The ledger line saying this app turned ldn_mitm's boot flag off; older ledgers do not have it. */
constexpr const char* MitmFlagMarker = "ldn_mitm_boot2_removed";

/** installed.txt: the version and the ldn_mitm marker. Older ledgers also listed files; those lines are ignored. */
struct Ledger {
    std::string tag;
    bool mitmFlagRemoved = false;  // removal puts ldn_mitm's flag back only when this is set
};
inline std::string ledgerText(const Ledger& l) {
    std::string out = "potluck_installer 1\n" + l.tag + "\n";
    if (l.mitmFlagRemoved) out += std::string(MitmFlagMarker) + "\n";
    return out;
}
inline bool parseLedger(const std::string& text, Ledger& l) {
    l = {};
    size_t pos = 0;
    int line = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(pos, end - pos);
        pos = end + 1;
        if (line == 0) { if (row != "potluck_installer 1") return false; }
        else if (line == 1) l.tag = row;
        else if (row == MitmFlagMarker) l.mitmFlagRemoved = true;
        ++line;
    }
    return line >= 2 && !l.tag.empty();
}

// ---- Safety checks before the internet mode ----

/** The domains the module's own check uses too (same list, same verdict). */
constexpr const char* BlockDomains[] = {"conntest.nintendowifi.net", "ctest.cdn.nintendo.net", "accounts.nintendo.com",
                                        "api.accounts.nintendo.com", "atum.hac.lp1.d4c.nintendo.net"};
constexpr int BlockDomainCount = 5;
/** 90DNS and Atmosphere's default hosts do not block these two, they send them to the connection-test
 *  server so the console still sees an internet link. They are shown but never count as "not blocked". */
inline bool redirectedByDesign(int index) { return index == 0 || index == 1; }

struct Addr {
    std::array<unsigned char, 16> b{};
    bool v6 = false;
};
enum class Reach { Blocked, Open, Unknown };
enum class Lookup { Ok, NxDomain, Failed };

inline bool v4Blocked(const unsigned char* a) {
    if (a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0) return true;      // 0.0.0.0
    if (a[0] == 127) return true;                                            // loopback
    if (a[0] == 10) return true;                                             // private
    if (a[0] == 172 && (a[1] & 0xF0) == 16) return true;
    if (a[0] == 192 && a[1] == 168) return true;
    return a[0] == 169 && a[1] == 254;                                       // link-local
}
inline bool addrBlocked(const Addr& a) {
    if (!a.v6) return v4Blocked(a.b.data());
    bool zero = true;
    for (int i = 0; i < 16; ++i) zero = zero && a.b[i] == 0;
    if (zero) return true;                                                   // ::
    bool loop = true;
    for (int i = 0; i < 15; ++i) loop = loop && a.b[i] == 0;
    if (loop && a.b[15] == 1) return true;                                   // ::1
    if ((a.b[0] & 0xFE) == 0xFC) return true;                                // fc00::/7
    if (a.b[0] == 0xFE && (a.b[1] & 0xC0) == 0x80) return true;              // fe80::/10
    bool mapped = true;                                                      // ::ffff:a.b.c.d
    for (int i = 0; i < 10; ++i) mapped = mapped && a.b[i] == 0;
    if (mapped && a.b[10] == 0xFF && a.b[11] == 0xFF) return v4Blocked(a.b.data() + 12);
    return false;
}
/** One domain: NXDOMAIN or only local answers is blocked, any public address is open. */
inline Reach domainReach(Lookup status, const std::vector<Addr>& addrs) {
    if (status == Lookup::NxDomain) return Reach::Blocked;
    if (status == Lookup::Failed || addrs.empty()) return Reach::Unknown;
    for (const auto& a : addrs) if (!addrBlocked(a)) return Reach::Open;
    return Reach::Blocked;
}
/** The whole list: any open domain makes it open; an unknown one without an open one is unknown. */
inline Reach overallReach(const std::array<Reach, BlockDomainCount>& per) {
    bool unknown = false;
    for (int i = 0; i < BlockDomainCount; ++i) {
        if (redirectedByDesign(i)) continue;
        if (per[i] == Reach::Open) return Reach::Open;
        unknown = unknown || per[i] == Reach::Unknown;
    }
    return unknown ? Reach::Unknown : Reach::Blocked;
}

enum class Serial { Blank, Present, Unknown };
/** A serial with any digit 1-9 is a real-looking one; empty or all zeros is blanked. */
inline Serial serialKind(const std::string& text, bool readOk) {
    if (!readOk) return Serial::Unknown;
    for (char c : text) if (c >= '1' && c <= '9') return Serial::Present;
    return Serial::Blank;
}

struct Exosphere {
    int blankSys = -1, blankEmu = -1;  // -1: the key is not in exosphere.ini
};
inline std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
/** blank_prodinfo_sysmmc / blank_prodinfo_emummc under [exosphere]. */
inline Exosphere parseExosphere(const std::string& text) {
    Exosphere e;
    bool inSection = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { inSection = line == "[exosphere]"; continue; }
        const size_t eq = line.find('=');
        if (!inSection || eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "blank_prodinfo_sysmmc") e.blankSys = value == "1";
        if (key == "blank_prodinfo_emummc") e.blankEmu = value == "1";
    }
    return e;
}
/** emummc.ini: enabled=1 under [emummc]; -1 when it says nothing. */
inline int parseEmummcEnabled(const std::string& text) {
    bool inSection = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { inSection = line == "[emummc]"; continue; }
        const size_t eq = line.find('=');
        if (inSection && eq != std::string::npos && trim(line.substr(0, eq)) == "enabled") return trim(line.substr(eq + 1)) == "1";
    }
    return -1;
}

enum class Mmc { Sys, Emu, Unknown };
enum class Risk { DnsOpen, DnsUnknown, SerialPresent, SerialUnknown, ProdinfoNotBlank, ProdinfoUnknown, OnSysMmc, MmcUnknown };
struct Facts {
    Mmc mmc = Mmc::Unknown;
    Serial serial = Serial::Unknown;
    int blankRuntime = -1;  // exosphere's own answer for the running system: 1 blanked, 0 not, -1 unknown
    Exosphere exo;          // what exosphere.ini asks for: shown, never taken as the running system's state
    int emummcIni = -2;     // emummc.ini enabled: 1, 0, -1 says nothing, -2 no file; a setting, not the boot target
    Reach dns = Reach::Unknown;
    std::vector<std::string> dnsDetail;  // one line per domain: what the console resolved, for the screen
    std::string blankSerial;             // the serial as the system reports it, kept only when it is blanked
};
/** Nintendo cannot tell this console apart: the running system reports a blanked serial and blank_prodinfo on. */
inline bool identityHidden(const Facts& f) { return f.serial == Serial::Blank && f.blankRuntime == 1; }

/** What is unsafe about connecting; empty is safe. Unknown counts as unsafe. */
inline std::vector<Risk> risks(const Facts& f) {
    std::vector<Risk> out;
    if (f.dns == Reach::Open) out.push_back(Risk::DnsOpen);
    else if (f.dns == Reach::Unknown) out.push_back(Risk::DnsUnknown);
    if (f.serial == Serial::Present) out.push_back(Risk::SerialPresent);
    else if (f.serial == Serial::Unknown) out.push_back(Risk::SerialUnknown);
    // Only the running system's answers count; the ini files say what was asked for, not what booted.
    if (f.blankRuntime == 0) out.push_back(Risk::ProdinfoNotBlank);
    else if (f.blankRuntime < 0) out.push_back(Risk::ProdinfoUnknown);
    if (f.mmc == Mmc::Sys) out.push_back(Risk::OnSysMmc);
    else if (f.mmc == Mmc::Unknown) out.push_back(Risk::MmcUnknown);
    return out;
}

/** Safe to connect: a hidden identity outweighs open DNS and sysMMC, otherwise nothing may be found. */
inline bool isSafe(const Facts& f) { return identityHidden(f) || risks(f).empty(); }


// Server address in config.ini ([server] host / port). Only that key is rewritten; every other line stays as it is.
constexpr const char* ServerSection = "server";
constexpr const char* HostKey = "host";
constexpr const char* PortKey = "port";
/// The port the module uses when config.ini has none (DEFAULT_PORT in the module)
constexpr int DefaultPort = 30456;

inline std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}
/** A port is 1 to 65535, digits only. */
inline bool validPort(const std::string& s) {
    if (s.empty() || s.size() > 5) return false;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    return v >= 1 && v <= 65535;
}
/** A host is an IPv4 address or a hostname: letters, digits and hyphens in labels of 1 to 63, joined by dots, no spaces. */
inline bool validHost(const std::string& s) {
    if (s.empty() || s.size() > 63) return false;  // the module's limit for the whole name
    std::vector<std::string> labels;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            labels.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    bool digitsOnly = true;
    for (const auto& l : labels) {
        if (l.empty() || l.size() > 63 || l.front() == '-' || l.back() == '-') return false;
        for (char c : l) {
            const bool digit = c >= '0' && c <= '9';
            if (!digit) digitsOnly = false;
            if (!digit && c != '-' && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
        }
    }
    if (!digitsOnly) return true;
    // Digits and dots only: it must be a real IPv4 address, four parts of 0 to 255.
    if (labels.size() != 4) return false;
    for (const auto& l : labels) if (l.size() > 3 || std::stoi(l) > 255) return false;
    return true;
}
/** The value of key under [section], or "" when absent. */
inline std::string iniGet(const std::string& text, const std::string& section, const std::string& key) {
    std::string current;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trimmed(text.substr(pos, end - pos));
        if (!line.empty() && line[0] == '[' && line.back() == ']') current = trimmed(line.substr(1, line.size() - 2));
        else if (current == section && !line.empty() && line[0] != ';' && line[0] != '#') {
            const size_t eq = line.find('=');
            if (eq != std::string::npos && trimmed(line.substr(0, eq)) == key) return trimmed(line.substr(eq + 1));
        }
        pos = end + 1;
    }
    return "";
}
/** The text with `key = value` set under [section]: replaces that line, else adds it to the section, else adds the section. */
inline std::string iniSet(const std::string& text, const std::string& section, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = end + 1;
    }
    std::string current;
    int sectionEnd = -1;  // index after the last line of the wanted section
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string line = trimmed(lines[i]);
        if (!line.empty() && line[0] == '[' && line.back() == ']') {
            current = trimmed(line.substr(1, line.size() - 2));
            if (current == section) sectionEnd = int(i) + 1;
            continue;
        }
        if (current != section) continue;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        sectionEnd = int(i) + 1;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && trimmed(line.substr(0, eq)) == key) {
            lines[i] = key + " = " + value;
            std::string out;
            for (const auto& l : lines) out += l + "\n";
            return out;
        }
    }
    const std::string entry = key + " = " + value;
    if (sectionEnd >= 0) {
        lines.insert(lines.begin() + sectionEnd, entry);
    } else {
        if (!lines.empty() && !trimmed(lines.back()).empty()) lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(entry);
    }
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

/** The text without the `key` line under [section]; everything else stays. */
inline std::string iniRemove(const std::string& text, const std::string& section, const std::string& key) {
    std::string out, current;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        const bool last = end == std::string::npos;
        if (last) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trimmed(line);
        bool drop = false;
        if (!t.empty() && t[0] == '[' && t.back() == ']') current = trimmed(t.substr(1, t.size() - 2));
        else if (current == section && !t.empty() && t[0] != ';' && t[0] != '#') {
            const size_t eq = t.find('=');
            drop = eq != std::string::npos && trimmed(t.substr(0, eq)) == key;
        }
        if (!drop) out += line + "\n";
        pos = end + 1;
    }
    return out;
}

/** Whether a `key` line is under [section], an empty value included. */
inline bool iniHas(const std::string& text, const std::string& section, const std::string& key) {
    std::string current;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trimmed(text.substr(pos, end - pos));
        pos = end + 1;
        if (!line.empty() && line[0] == '[' && line.back() == ']') { current = trimmed(line.substr(1, line.size() - 2)); continue; }
        if (current != section || line.empty() || line[0] == ';' || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && trimmed(line.substr(0, eq)) == key) return true;
    }
    return false;
}

// ---- Settings of the older version (config/ryu_ldn_nx/config.ini) ----

/** Keys the module reads, so carrying them over is what keeps the old settings. `[ldn] enabled` is gone:
 *  `[ldn] mode` (remote, local or off) is chosen in the overlay. */
constexpr const char* CarriedKeys[][2] = {
    {"server", "host"},           {"server", "port"},       {"network", "connect_timeout"},
    {"network", "reconnect_delay"}, {"network", "max_reconnect_attempts"},
    {"ldn", "passphrase"},        {"ldn", "disable_p2p"},   {"debug", "enabled"},
    {"debug", "level"},           {"debug", "log_to_file"},
};
/** The old settings as the start of a new config.ini: everything but the retired `[ldn] enabled`. */
inline std::string carriedConfig(const std::string& legacy) { return iniRemove(legacy, "ldn", "enabled"); }
/** True when every key the module reads from the old settings is in the new config.ini too, with the same value. */
inline bool carriedOver(const std::string& legacy, const std::string& current) {
    for (const auto& k : CarriedKeys) {
        if (!iniHas(legacy, k[0], k[1])) continue;
        if (!iniHas(current, k[0], k[1]) || iniGet(current, k[0], k[1]) != iniGet(legacy, k[0], k[1])) return false;
    }
    return true;
}

} // namespace inst
