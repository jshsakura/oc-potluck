# =========================================
# NETWORK:PACKET
# =========================================

echo [NETWORK] Loading packet breakpoints...\n
# Namespace: potluck::network
dprintf potluck::network::RelayClient::send_initialize, "[NETWORK:PACKET] send_initialize (handshake)\n"
dprintf potluck::network::RelayClient::generate_mac_address, "[NETWORK:PACKET] Generating MAC address\n"
dprintf potluck::network::client_op_result_to_string, "[NETWORK:PACKET] client_op_result_to_string\n"

echo [NETWORK] packet: 3 dprintf points\n
