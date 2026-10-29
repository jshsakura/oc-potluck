# =========================================
# NETWORK:LIFECYCLE
# =========================================

echo [NETWORK] Loading lifecycle breakpoints...\n
# Namespace: potluck::network
dprintf potluck::network::RelayClient::RelayClient, "[NETWORK:LIFECYCLE] Client constructor\n"
dprintf potluck::network::RelayClient::~RelayClient, "[NETWORK:LIFECYCLE] Client destructor\n"
dprintf potluck::network::RelayClient::set_config, "[NETWORK:LIFECYCLE] Configuration set\n"

echo [NETWORK] lifecycle: 3 dprintf points\n
