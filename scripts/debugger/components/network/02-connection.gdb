# =========================================
# NETWORK:CONNECTION
# =========================================

echo [NETWORK] Loading connection breakpoints...\n
# Namespace: potluck::network
dprintf potluck::network::RelayClient::get_config, "[NETWORK:CONNECTION] get_config\n"
dprintf potluck::network::RelayClient::connect, "[NETWORK:CONNECTION] Connect initiated\n"
dprintf potluck::network::RelayClient::get_state, "[NETWORK:CONNECTION] get_state\n"
dprintf potluck::network::RelayClient::is_connected, "[NETWORK:CONNECTION] is_connected queried\n"
dprintf potluck::network::RelayClient::is_ready, "[NETWORK:CONNECTION] is_ready queried\n"
dprintf potluck::network::RelayClient::is_transitioning, "[NETWORK:CONNECTION] is_transitioning queried\n"
dprintf potluck::network::RelayClient::get_retry_count, "[NETWORK:CONNECTION] get_retry_count\n"
dprintf potluck::network::RelayClient::get_last_error_code, "[NETWORK:CONNECTION] get_last_error_code\n"
dprintf potluck::network::RelayClient::get_last_rtt_ms, "[NETWORK:CONNECTION] get_last_rtt_ms\n"
dprintf potluck::network::ReconnectManager::ReconnectManager, "[NETWORK:CONNECTION] ReconnectManager created\n"
dprintf potluck::network::ReconnectManager::get_next_delay_ms, "[NETWORK:CONNECTION] Getting next backoff delay\n"
dprintf potluck::network::ReconnectManager::should_retry, "[NETWORK:CONNECTION] Should we retry?\n"
dprintf potluck::network::ReconnectManager::record_failure, "[NETWORK:CONNECTION] Recording connection failure\n"
dprintf potluck::network::ReconnectManager::reset, "[NETWORK:CONNECTION] Resetting reconnect state\n"

echo [NETWORK] connection: 14 dprintf points\n
