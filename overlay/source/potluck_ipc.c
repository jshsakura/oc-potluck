/**
 * @file potluck_ipc.c
 * @brief IPC client implementation for potluck sysmodule
 *
 * Connects to the standalone potluck IPC service provided by the sysmodule.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "potluck_ipc.h"
#include <string.h>

/**
 * IPC Command IDs for potluck service
 *
 * These are the command IDs for the standalone configuration service.
 */
enum {
    // Configuration commands (0-22)
    PotluckCmd_GetVersion          = 0,
    PotluckCmd_GetConnectionStatus = 1,
    PotluckCmd_GetPassphrase       = 2,
    PotluckCmd_SetPassphrase       = 3,
    PotluckCmd_GetServerAddress    = 4,
    PotluckCmd_SetServerAddress    = 5,
    PotluckCmd_GetLdnMode          = 6,
    PotluckCmd_SetLdnMode          = 7,
    PotluckCmd_GetUseTls           = 8,
    PotluckCmd_SetUseTls           = 9,
    PotluckCmd_GetDebugEnabled     = 10,
    PotluckCmd_SetDebugEnabled     = 11,
    PotluckCmd_GetDebugLevel       = 12,
    PotluckCmd_SetDebugLevel       = 13,
    PotluckCmd_GetLogToFile        = 14,
    PotluckCmd_SetLogToFile        = 15,
    PotluckCmd_SaveConfig          = 16,
    PotluckCmd_ReloadConfig        = 17,
    PotluckCmd_GetConnectTimeout   = 18,
    PotluckCmd_SetConnectTimeout   = 19,
    PotluckCmd_GetPingInterval     = 20,
    PotluckCmd_SetPingInterval     = 21,
    PotluckCmd_IsServiceActive     = 22,

    // Runtime LDN state commands (23-28)
    PotluckCmd_IsGameActive        = 23,
    PotluckCmd_GetLdnState         = 24,
    PotluckCmd_GetSessionInfo      = 25,
    PotluckCmd_GetLastRtt          = 26,
    PotluckCmd_ForceReconnect      = 27,
    PotluckCmd_GetActiveProcessId  = 28,

    // P2P Proxy control & Server probe (29-31)
    PotluckCmd_GetDisableP2p       = 29,
    PotluckCmd_SetDisableP2p       = 30,
    PotluckCmd_TestServer          = 31,
    PotluckCmd_PrepareStop         = 32,
    PotluckCmd_GetRuntimePhase     = 33,
    PotluckCmd_GetRelayTraffic     = 34,
};

/// Global service handle
static PotluckService g_potluckService;
static bool g_potluckInitialized = false;

Result potluckInitialize(void) {
    if (g_potluckInitialized) {
        return 0;  // Already initialized
    }

    u8 available = 0;
    const SmServiceName name = smEncodeName("potluck");
    Result availability = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, available);
    if (R_SUCCEEDED(availability) && !(available & 1)) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    Result rc = smGetService(&g_potluckService.s, "potluck");
    if (R_SUCCEEDED(rc)) {
        g_potluckInitialized = true;
    }
    return rc;
}

void potluckExit(void) {
    if (g_potluckInitialized) {
        serviceClose(&g_potluckService.s);
        g_potluckInitialized = false;
    }
}

PotluckService* potluckGetService(void) {
    return g_potluckInitialized ? &g_potluckService : NULL;
}

Result potluckIsServiceActive(PotluckService* s, u32* active) {
    return serviceDispatchOut(&s->s, PotluckCmd_IsServiceActive, *active);
}

Result potluckGetVersion(PotluckService* s, char* version) {
    char version_buf[32];
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_GetVersion, version_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(version, version_buf, 32);
        version[31] = '\0';  // Ensure null termination
    }
    return rc;
}

Result potluckGetConnectionStatus(PotluckService* s, PotluckConnectionStatus* status) {
    u32 out_status;
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_GetConnectionStatus, out_status);
    if (R_SUCCEEDED(rc)) {
        *status = (PotluckConnectionStatus)out_status;
    }
    return rc;
}

Result potluckGetServerAddress(PotluckService* s, char* host, u16* port) {
    struct {
        char host[64];
        u16 port;
        u16 padding;
    } out;

    Result rc = serviceDispatchOut(&s->s, PotluckCmd_GetServerAddress, out);
    if (R_SUCCEEDED(rc)) {
        memcpy(host, out.host, 64);
        host[63] = '\0';
        *port = out.port;
    }
    return rc;
}

Result potluckSetServerAddress(PotluckService* s, const char* host, u16 port) {
    struct {
        char host[64];
        u16 port;
        u16 padding;
    } in;

    memset(&in, 0, sizeof(in));
    strncpy(in.host, host, 63);
    in.port = port;

    return serviceDispatchIn(&s->s, PotluckCmd_SetServerAddress, in);
}

Result potluckGetDebugEnabled(PotluckService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetDebugEnabled, *enabled);
}

Result potluckSetDebugEnabled(PotluckService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetDebugEnabled, enabled);
}

//=============================================================================
// Configuration Commands
//=============================================================================

Result potluckGetPassphrase(PotluckService* s, char* passphrase) {
    char passphrase_buf[64];
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_GetPassphrase, passphrase_buf);
    if (R_SUCCEEDED(rc)) {
        memcpy(passphrase, passphrase_buf, 64);
        passphrase[63] = '\0';
    }
    return rc;
}

Result potluckSetPassphrase(PotluckService* s, const char* passphrase) {
    char passphrase_buf[64];
    memset(passphrase_buf, 0, sizeof(passphrase_buf));
    if (passphrase != NULL) {
        strncpy(passphrase_buf, passphrase, 63);
    }
    return serviceDispatchIn(&s->s, PotluckCmd_SetPassphrase, passphrase_buf);
}

Result potluckGetLdnMode(PotluckService* s, u32* mode) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetLdnMode, *mode);
}

Result potluckSetLdnMode(PotluckService* s, u32 mode) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetLdnMode, mode);
}

Result potluckGetUseTls(PotluckService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetUseTls, *enabled);
}

Result potluckSetUseTls(PotluckService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetUseTls, enabled);
}

Result potluckGetConnectTimeout(PotluckService* s, u32* timeout_ms) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetConnectTimeout, *timeout_ms);
}

Result potluckSetConnectTimeout(PotluckService* s, u32 timeout_ms) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetConnectTimeout, timeout_ms);
}

Result potluckGetPingInterval(PotluckService* s, u32* interval_ms) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetPingInterval, *interval_ms);
}

Result potluckSetPingInterval(PotluckService* s, u32 interval_ms) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetPingInterval, interval_ms);
}

Result potluckGetDebugLevel(PotluckService* s, u32* level) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetDebugLevel, *level);
}

Result potluckSetDebugLevel(PotluckService* s, u32 level) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetDebugLevel, level);
}

Result potluckGetLogToFile(PotluckService* s, u32* enabled) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetLogToFile, *enabled);
}

Result potluckSetLogToFile(PotluckService* s, u32 enabled) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetLogToFile, enabled);
}

Result potluckSaveConfig(PotluckService* s, PotluckResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_SaveConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (PotluckResult)out_result;
    }
    return rc;
}

Result potluckReloadConfig(PotluckService* s, PotluckResult* result) {
    u32 out_result;
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_ReloadConfig, out_result);
    if (R_SUCCEEDED(rc)) {
        *result = (PotluckResult)out_result;
    }
    return rc;
}

//=============================================================================
// Runtime LDN State Commands (23-28)
//=============================================================================

Result potluckIsGameActive(PotluckService* s, u32* active) {
    return serviceDispatchOut(&s->s, PotluckCmd_IsGameActive, *active);
}

Result potluckGetLdnState(PotluckService* s, PotluckLdnState* state) {
    u32 out_state;
    Result rc = serviceDispatchOut(&s->s, PotluckCmd_GetLdnState, out_state);
    if (R_SUCCEEDED(rc)) {
        *state = (PotluckLdnState)out_state;
    }
    return rc;
}

Result potluckGetSessionInfo(PotluckService* s, PotluckSessionInfo* info) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetSessionInfo, *info);
}

Result potluckGetLastRtt(PotluckService* s, u32* rtt_ms) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetLastRtt, *rtt_ms);
}

Result potluckForceReconnect(PotluckService* s) {
    return serviceDispatch(&s->s, PotluckCmd_ForceReconnect);
}

Result potluckGetActiveProcessId(PotluckService* s, u64* pid) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetActiveProcessId, *pid);
}

Result potluckTestServer(PotluckService* s, PotluckServerTestResult* result) {
    return serviceDispatchOut(&s->s, PotluckCmd_TestServer, *result);
}

Result potluckPrepareStop(PotluckService* s, u32* status) {
    return serviceDispatchOut(&s->s, PotluckCmd_PrepareStop, *status);
}

Result potluckGetRuntimePhase(PotluckService* s, u32* phase) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetRuntimePhase, *phase);
}

Result potluckGetRelayTraffic(PotluckService* s, PotluckRelayTraffic* traffic) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetRelayTraffic, *traffic);
}

const char* potluckStateToString(PotluckLdnState state) {
    switch (state) {
        case PotluckLdnState_None:               return "없음";
        case PotluckLdnState_Initialized:        return "초기화됨";
        case PotluckLdnState_AccessPoint:        return "방장 대기 (AP)";
        case PotluckLdnState_AccessPointCreated: return "방 생성됨";
        case PotluckLdnState_Station:            return "참가자 대기";
        case PotluckLdnState_StationConnected:   return "접속됨";
        case PotluckLdnState_Error:              return "오류";
        default:                             return "알 수 없음";
    }
}

Result potluckGetDisableP2p(PotluckService* s, u32* disabled) {
    return serviceDispatchOut(&s->s, PotluckCmd_GetDisableP2p, *disabled);
}

Result potluckSetDisableP2p(PotluckService* s, u32 disabled) {
    return serviceDispatchIn(&s->s, PotluckCmd_SetDisableP2p, disabled);
}
