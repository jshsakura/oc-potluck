# =========================================
# CONFIG:MGR
# =========================================

echo [CONFIG] Loading mgr breakpoints...\n
# Namespace: potluck::config
dprintf potluck::config::IsValidPassphrase, "[CONFIG:MGR] Validating passphrase\n"
dprintf potluck::config::GenerateRandomPassphrase, "[CONFIG:MGR] Generating random passphrase\n"
dprintf potluck::config::ConfigManager::Instance, "[CONFIG:MGR] Getting manager instance\n"
dprintf potluck::config::ConfigManager::Initialize, "[CONFIG:MGR] Initializing configuration manager\n"
dprintf potluck::config::ConfigManager::Save, "[CONFIG:MGR] Saving configuration to disk\n"
dprintf potluck::config::ConfigManager::Reload, "[CONFIG:MGR] Reloading configuration from disk\n"
dprintf potluck::config::ConfigManager::SetServerHost, "[CONFIG:MGR] Setting server host\n"
dprintf potluck::config::ConfigManager::SetServerPort, "[CONFIG:MGR] Setting server port\n"
dprintf potluck::config::ConfigManager::SetUseTls, "[CONFIG:MGR] Setting TLS enabled state\n"
dprintf potluck::config::ConfigManager::SetConnectTimeout, "[CONFIG:MGR] Setting connect timeout\n"
dprintf potluck::config::ConfigManager::SetPingInterval, "[CONFIG:MGR] Setting ping interval\n"
dprintf potluck::config::ConfigManager::SetReconnectDelay, "[CONFIG:MGR] Setting reconnect delay\n"
dprintf potluck::config::ConfigManager::SetMaxReconnectAttempts, "[CONFIG:MGR] Setting max reconnect attempts\n"
dprintf potluck::config::ConfigManager::SetLdnMode, "[CONFIG:MGR] Setting LDN mode\n"
dprintf potluck::config::ConfigManager::SetPassphrase, "[CONFIG:MGR] Setting passphrase\n"
dprintf potluck::config::ConfigManager::SetInterfaceName, "[CONFIG:MGR] Setting interface name\n"
dprintf potluck::config::ConfigManager::SetDebugEnabled, "[CONFIG:MGR] Setting debug enabled\n"
dprintf potluck::config::ConfigManager::SetDebugLevel, "[CONFIG:MGR] Setting debug level\n"
dprintf potluck::config::ConfigManager::SetLogToFile, "[CONFIG:MGR] Setting log to file\n"
dprintf potluck::config::ConfigManager::SetChangeCallback, "[CONFIG:MGR] Setting change callback\n"
dprintf potluck::config::ConfigManager::NotifyChange, "[CONFIG:MGR] Config change notified\n"

echo [CONFIG] mgr: 21 dprintf points\n
