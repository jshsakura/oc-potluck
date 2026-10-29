# =========================================
# CONFIG:IPC
# =========================================

echo [CONFIG] Loading ipc breakpoints...\n
# Namespace: potluck::ipc
dprintf potluck::ipc::ConfigService::GetVersion, "[CONFIG:IPC] GetVersion\n"
dprintf potluck::ipc::ConfigService::GetConnectionStatus, "[CONFIG:IPC] GetConnectionStatus\n"
dprintf potluck::ipc::ConfigService::GetPassphrase, "[CONFIG:IPC] GetPassphrase\n"
dprintf potluck::ipc::ConfigService::SetPassphrase, "[CONFIG:IPC] SetPassphrase\n"
dprintf potluck::ipc::ConfigService::GetServerAddress, "[CONFIG:IPC] GetServerAddress\n"
dprintf potluck::ipc::ConfigService::SetServerAddress, "[CONFIG:IPC] SetServerAddress\n"
dprintf potluck::ipc::ConfigService::GetLdnMode, "[CONFIG:IPC] GetLdnMode\n"
dprintf potluck::ipc::ConfigService::SetLdnMode, "[CONFIG:IPC] SetLdnMode\n"
dprintf potluck::ipc::ConfigService::GetUseTls, "[CONFIG:IPC] GetUseTls\n"
dprintf potluck::ipc::ConfigService::SetUseTls, "[CONFIG:IPC] SetUseTls\n"
dprintf potluck::ipc::ConfigService::GetDebugEnabled, "[CONFIG:IPC] GetDebugEnabled\n"
dprintf potluck::ipc::ConfigService::SetDebugEnabled, "[CONFIG:IPC] SetDebugEnabled\n"
dprintf potluck::ipc::ConfigService::GetDebugLevel, "[CONFIG:IPC] GetDebugLevel\n"
dprintf potluck::ipc::ConfigService::SetDebugLevel, "[CONFIG:IPC] SetDebugLevel\n"
dprintf potluck::ipc::ConfigService::GetLogToFile, "[CONFIG:IPC] GetLogToFile\n"
dprintf potluck::ipc::ConfigService::SetLogToFile, "[CONFIG:IPC] SetLogToFile\n"
dprintf potluck::ipc::ConfigService::GetConnectTimeout, "[CONFIG:IPC] GetConnectTimeout\n"
dprintf potluck::ipc::ConfigService::SetConnectTimeout, "[CONFIG:IPC] SetConnectTimeout\n"
dprintf potluck::ipc::ConfigService::GetPingInterval, "[CONFIG:IPC] GetPingInterval\n"
dprintf potluck::ipc::ConfigService::SetPingInterval, "[CONFIG:IPC] SetPingInterval\n"
dprintf potluck::ipc::ConfigService::SaveConfig, "[CONFIG:IPC] SaveConfig\n"
dprintf potluck::ipc::ConfigService::ReloadConfig, "[CONFIG:IPC] ReloadConfig\n"
dprintf potluck::ipc::ConfigService::IsServiceActive, "[CONFIG:IPC] IsServiceActive\n"
dprintf potluck::ipc::ConfigService::IsGameActive, "[CONFIG:IPC] IsGameActive\n"
dprintf potluck::ipc::ConfigService::GetLdnState, "[CONFIG:IPC] GetLdnState\n"
dprintf potluck::ipc::ConfigService::GetSessionInfo, "[CONFIG:IPC] GetSessionInfo\n"
dprintf potluck::ipc::ConfigService::GetLastRtt, "[CONFIG:IPC] GetLastRtt\n"
dprintf potluck::ipc::ConfigService::ForceReconnect, "[CONFIG:IPC] ForceReconnect\n"
dprintf potluck::ipc::ConfigService::GetActiveProcessId, "[CONFIG:IPC] GetActiveProcessId\n"
dprintf potluck::ipc::ConfigService::GetDisableP2p, "[CONFIG:IPC] GetDisableP2p\n"
dprintf potluck::ipc::ConfigService::SetDisableP2p, "[CONFIG:IPC] SetDisableP2p\n"

echo [CONFIG] ipc: 31 dprintf points\n
