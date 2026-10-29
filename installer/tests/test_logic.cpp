#include "../source/logic.hpp"
#include <cstdio>
using namespace inst;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static Addr v4(int a, int b, int c, int d) {
    Addr x;
    x.v6 = false;
    x.b[0] = a; x.b[1] = b; x.b[2] = c; x.b[3] = d;
    return x;
}
static Addr v6(std::initializer_list<int> head, int last = 0) {
    Addr x;
    x.v6 = true;
    int i = 0;
    for (int h : head) x.b[i++] = h;
    x.b[15] = last;
    return x;
}

int main() {
    // Paths: the module's own folder and its overlay, nothing else.
    CHECK(safePath("atmosphere/contents/4200000000504C4B/exefs.nsp"));
    CHECK(!safePath("") && !safePath("/etc/x") && !safePath("../a") && !safePath("a/../b") && !safePath("a//b"));
    CHECK(!safePath("a\\b") && !safePath("sdmc:/a") && !safePath(std::string(201, 'a')));
    CHECK(allowedTarget("atmosphere/contents/4200000000504C4B/flags/boot2.flag"));
    CHECK(allowedTarget("atmosphere/contents/4200000000504C4B/config.ini"));
    CHECK(allowedTarget("switch/.overlays/potluck.ovl"));
    CHECK(!allowedTarget("atmosphere/contents/4200000000000010/exefs.nsp"));  // ldn_mitm's folder is never written
    CHECK(!allowedTarget("atmosphere/contents/0100000000001000/exefs.nsp") && !allowedTarget("switch/hbmenu.nro"));
    CHECK(!allowedTarget("config/potluck/x") && !allowedTarget("switch/.overlays/ryu_ldn_nx_overlay.ovl"));
    CHECK(!allowedTarget("atmosphere/contents/4200000000504C4B/") && !allowedTarget("atmosphere/contents/4200000000504C4B/../x"));
    CHECK(keepIfPresent("atmosphere/contents/4200000000504C4B/config.ini") && !keepIfPresent("atmosphere/contents/4200000000504C4B/exefs.nsp"));
    CHECK(isOwnToolbox("{\"name\": \"potluck\"}") && !isOwnToolbox("{\"name\": \"ldn_mitm\"}") && !isOwnToolbox("{\"name\": \"ryu_ldn_nx\"}"));
    CHECK(isLegacyToolbox("{\"name\": \"ryu_ldn_nx\"}") && !isLegacyToolbox("{\"name\": \"potluck\"}"));
    CHECK(sameVersion("v0.1.15", "0.1.15") && !sameVersion("v0.1.14", "v0.1.15"));
    CHECK(isReleaseZip("potluck-v0.2.0.zip") && !isReleaseZip("ryu_ldn_nx-v0.1.16.zip") && !isReleaseZip("potluck.elf"));
    CHECK(releaseUrlOk("https://github.com/jshsakura/oc-potluck/releases/download/v1/a.zip"));
    CHECK(!releaseUrlOk("https://example.com/jshsakura/oc-potluck/releases/download/v1/a.zip"));

    Ledger l; l.tag = "v0.2.0";
    Ledger r;
    CHECK(parseLedger(ledgerText(l), r) && r.tag == l.tag && ledgerText(l) == "potluck_installer 1\nv0.2.0\n");
    CHECK(!parseLedger("garbage\nv1\n", r) && !parseLedger("", r));
    CHECK(parseLedger("potluck_installer 1\nv1\n../evil\nswitch/hbmenu.nro\n", r) && r.tag == "v1" && !r.mitmFlagRemoved);  // older file lists are ignored
    l.mitmFlagRemoved = true;
    CHECK(parseLedger(ledgerText(l), r) && r.mitmFlagRemoved && r.tag == l.tag);

    // A release never brings its own boot flag: the installer sets it after every file is in place.
    CHECK(!extractable("atmosphere/contents/4200000000504C4B/flags/boot2.flag") && !extractable("atmosphere/contents/4200000000504C4B/flags/x"));
    CHECK(extractable("atmosphere/contents/4200000000504C4B/exefs.nsp") && extractable("switch/.overlays/potluck.ovl") && !extractable("switch/hbmenu.nro"));
    CHECK(!extractable("atmosphere/contents/4200000000504C4B/Flags/boot2.flag") && !extractable("atmosphere/contents/4200000000504C4B/x.BAK") && !extractable("atmosphere/contents/4200000000504C4B/x.part"));
    CHECK(extractable("atmosphere/contents/4200000000504C4B/a/b/x") && !extractable("atmosphere/contents/4200000000504C4B/a/b/c/x"));
    CHECK(keepIfPresent("atmosphere/contents/4200000000504C4B/CONFIG.INI") && lower("AbC-1") == "abc-1");

    // Older settings: carried when every key the module reads is in the new file; `[ldn] enabled` is retired.
    const std::string old = "[server]\nhost = a.example\n[ldn]\nenabled = 1\npassphrase =\nuse_tls = 1\n";
    CHECK(iniHas(old, "ldn", "passphrase") && !iniHas(old, "server", "passphrase") && !iniHas("; passphrase = x\n", "", "passphrase"));
    CHECK(carriedConfig(old) == "[server]\nhost = a.example\n[ldn]\npassphrase =\nuse_tls = 1\n");
    CHECK(carriedOver(old, carriedConfig(old)) && carriedOver(old, "[server]\nport = 1\nhost = a.example\n[ldn]\npassphrase =\n"));
    CHECK(!carriedOver(old, "[server]\nhost = a.example\n") && !carriedOver(old, "") && carriedOver("", ""));
    CHECK(!carriedOver(old, "[server]\nhost = other.example\n[ldn]\npassphrase =\n"));  // the value must match too

    // DNS: loopback, 0.0.0.0, private and link-local answers are blocked; a public one is open.
    CHECK(addrBlocked(v4(0, 0, 0, 0)) && addrBlocked(v4(127, 0, 0, 1)) && addrBlocked(v4(127, 8, 8, 8)));
    CHECK(addrBlocked(v4(10, 1, 2, 3)) && addrBlocked(v4(172, 16, 0, 1)) && addrBlocked(v4(172, 31, 255, 1)));
    CHECK(addrBlocked(v4(192, 168, 1, 1)) && addrBlocked(v4(169, 254, 1, 1)));
    CHECK(!addrBlocked(v4(172, 32, 0, 1)) && !addrBlocked(v4(172, 15, 0, 1)) && !addrBlocked(v4(203, 0, 113, 5)));
    CHECK(addrBlocked(v6({})) && addrBlocked(v6({}, 1)) && addrBlocked(v6({0xFC, 0})) && addrBlocked(v6({0xFD, 0x12})));
    CHECK(addrBlocked(v6({0xFE, 0x80})) && !addrBlocked(v6({0x20, 0x01, 0x0D, 0xB8}, 1)));
    CHECK(addrBlocked(v6({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 127, 0, 0, 1})));
    CHECK(!addrBlocked(v6({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 203, 0, 113, 5})));
    CHECK(domainReach(Lookup::NxDomain, {}) == Reach::Blocked && domainReach(Lookup::Failed, {}) == Reach::Unknown);
    CHECK(domainReach(Lookup::Ok, {v4(127, 0, 0, 1)}) == Reach::Blocked);
    CHECK(domainReach(Lookup::Ok, {v4(127, 0, 0, 1), v4(203, 0, 113, 5)}) == Reach::Open);
    CHECK(domainReach(Lookup::Ok, {}) == Reach::Unknown);

    std::array<Reach, BlockDomainCount> per;
    per.fill(Reach::Blocked);
    CHECK(overallReach(per) == Reach::Blocked);
    per[0] = Reach::Open; per[1] = Reach::Open;  // conntest and ctest are redirected on purpose (90DNS)
    CHECK(overallReach(per) == Reach::Blocked);
    per[3] = Reach::Unknown;
    CHECK(overallReach(per) == Reach::Unknown);
    per[2] = Reach::Open;
    CHECK(overallReach(per) == Reach::Open);

    // Serial: any digit 1-9 looks real; empty or zeros is blanked.
    CHECK(serialKind("", true) == Serial::Blank && serialKind("XAW00000000000", true) == Serial::Blank);
    CHECK(serialKind("XAW10012345678", true) == Serial::Present && serialKind("x", false) == Serial::Unknown);

    // exosphere.ini and emummc.ini.
    const Exosphere e = parseExosphere("; c\n[stratosphere]\nblank_prodinfo_sysmmc=1\n[exosphere]\nblank_prodinfo_sysmmc = 1\nblank_prodinfo_emummc=0\n");
    CHECK(e.blankSys == 1 && e.blankEmu == 0);
    CHECK(parseExosphere("").blankSys == -1 && parseExosphere("[exosphere]\n# x\n").blankEmu == -1);
    CHECK(parseEmummcEnabled("[emummc]\nenabled=1\n") == 1 && parseEmummcEnabled("[emummc]\nenabled=0\n") == 0);
    CHECK(parseEmummcEnabled("[other]\nenabled=1\n") == -1 && parseEmummcEnabled("") == -1);

    // Risks: unknown counts as unsafe; a clean emuMMC console has none.
    Facts safe;
    safe.mmc = Mmc::Emu; safe.serial = Serial::Blank; safe.blankRuntime = 1; safe.dns = Reach::Blocked;
    CHECK(risks(safe).empty());
    Facts f = safe; f.dns = Reach::Open;
    CHECK(risks(f) == std::vector<Risk>{Risk::DnsOpen});
    f = safe; f.dns = Reach::Unknown; f.serial = Serial::Present;
    CHECK(risks(f) == (std::vector<Risk>{Risk::DnsUnknown, Risk::SerialPresent}));
    f = safe; f.mmc = Mmc::Sys;
    CHECK(risks(f) == std::vector<Risk>{Risk::OnSysMmc});
    f = safe; f.blankRuntime = 0;
    CHECK(risks(f) == std::vector<Risk>{Risk::ProdinfoNotBlank});
    // No runtime answer stays unknown: exosphere.ini and emummc.ini say what was asked for, not what booted.
    f = safe; f.blankRuntime = -1; f.exo.blankSys = 1; f.exo.blankEmu = 1;
    CHECK(risks(f) == std::vector<Risk>{Risk::ProdinfoUnknown});
    f = safe; f.mmc = Mmc::Unknown; f.emummcIni = 1;
    CHECK(risks(f) == std::vector<Risk>{Risk::MmcUnknown});
    f = safe; f.mmc = Mmc::Unknown; f.serial = Serial::Unknown;
    CHECK(risks(f) == (std::vector<Risk>{Risk::SerialUnknown, Risk::MmcUnknown}));

    // A serial the running system reports blanked, with blank_prodinfo on, is safe even with open DNS.
    f = safe; f.dns = Reach::Open; f.mmc = Mmc::Sys;
    CHECK(identityHidden(f) && isSafe(f));
    f = safe; f.dns = Reach::Open; f.blankRuntime = -1;  // only the ini says it is blanked
    CHECK(!identityHidden(f) && !isSafe(f));
    f = safe; f.dns = Reach::Open; f.serial = Serial::Present;
    CHECK(!isSafe(f));
    CHECK(isSafe(safe));

    // Server address: port 1-65535, host a name or a real IPv4 address.
    CHECK(validPort("1") && validPort("30456") && validPort("65535"));
    CHECK(!validPort("") && !validPort("0") && !validPort("65536") && !validPort("12a") && !validPort("-1") && !validPort("123456") && !validPort(" 80"));
    CHECK(validHost("example.com") && validHost("play.example.org") && validHost("a-b.example.com") && validHost("localhost") && validHost("192.0.2.10"));
    CHECK(!validHost("") && !validHost("a b.com") && !validHost("-a.com") && !validHost("a-.com") && !validHost("a..com") && !validHost(".com") && !validHost("a.com."));
    CHECK(!validHost("256.1.1.1") && !validHost("1.2.3") && !validHost("1.2.3.4.5") && !validHost("http://example.com") && !validHost("example.com:80") && !validHost(std::string(60, 'a') + ".com") && validHost(std::string(59, 'a') + ".com") && !validHost("a_b.com") && !validHost("a:b.com"));

    // config.ini: only the asked key changes; the rest, comments included, stays.
    const std::string ini = "; head\n[server]\n; host\nhost = 192.0.2.1\n; port\nport = 30456\nuse_tls = 0\n\n[network]\nport = 1\n";
    CHECK(iniGet(ini, "server", "host") == "192.0.2.1" && iniGet(ini, "server", "port") == "30456" && iniGet(ini, "network", "port") == "1");
    CHECK(iniGet(ini, "server", "nope").empty() && iniGet("", "server", "host").empty());
    const std::string a = iniSet(ini, "server", "host", "play.example.com");
    CHECK(iniGet(a, "server", "host") == "play.example.com" && iniGet(a, "server", "port") == "30456" && iniGet(a, "network", "port") == "1");
    CHECK(a == "; head\n[server]\n; host\nhost = play.example.com\n; port\nport = 30456\nuse_tls = 0\n\n[network]\nport = 1\n");
    const std::string b = iniSet("[server]\nuse_tls = 0\n\n[network]\nx = 1\n", "server", "port", "40000");
    CHECK(b == "[server]\nuse_tls = 0\nport = 40000\n\n[network]\nx = 1\n");
    CHECK(iniSet("", "server", "host", "example.com") == "[server]\nhost = example.com\n");
    CHECK(iniSet("[network]\nx = 1\n", "server", "host", "example.com") == "[network]\nx = 1\n\n[server]\nhost = example.com\n");
    CHECK(iniSet("[server]\r\nhost = old\r\n", "server", "host", "new.example.com") == "[server]\nhost = new.example.com\n");

    CHECK(iniRemove(ini, "server", "host") == "; head\n[server]\n; host\n; port\nport = 30456\nuse_tls = 0\n\n[network]\nport = 1\n");
    CHECK(iniRemove(ini, "network", "port") == "; head\n[server]\n; host\nhost = 192.0.2.1\n; port\nport = 30456\nuse_tls = 0\n\n[network]\n");
    CHECK(iniRemove(ini, "server", "nope") == ini && iniRemove("", "server", "host").empty());

    if (!failures) std::puts("All tests passed!");
    return failures ? 1 : 0;
}
