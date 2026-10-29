# =========================================
# BSD:ALIGN
# =========================================

echo [BSD] Loading align breakpoints...\n
# Namespace: ams::mitm::bsd
dprintf ams::mitm::bsd::ProxySocket::ProxySocket, "[BSD:ALIGN] ProxySocket constructed: type=%d protocol=%d\n", $x1, $x2
dprintf ams::mitm::bsd::ProxySocket::~ProxySocket, "[BSD:ALIGN] ProxySocket destroyed\n"
dprintf ams::mitm::bsd::ProxySocket::Bind, "[BSD:ALIGN] ProxySocket::Bind\n"
dprintf ams::mitm::bsd::ProxySocket::Connect, "[BSD:ALIGN] ProxySocket::Connect\n"
dprintf ams::mitm::bsd::ProxySocket::SendTo, "[BSD:ALIGN] ProxySocket::SendTo\n"
# Namespace: potluck::bsd
dprintf potluck::bsd::IsLdnAddress, "[BSD:ALIGN] IsLdnAddress(free): ip=0x%x\n", $x0
dprintf potluck::bsd::SockAddrIn::IsLdnAddress, "[BSD:ALIGN] SockAddrIn::IsLdnAddress: this=%p\n", $x0
dprintf potluck::bsd::SockAddrIn::GetPort, "[BSD:ALIGN] SockAddrIn::GetPort\n"
dprintf potluck::bsd::SockAddrIn::GetAddr, "[BSD:ALIGN] SockAddrIn::GetAddr\n"

echo [BSD] align: 9 dprintf points\n
