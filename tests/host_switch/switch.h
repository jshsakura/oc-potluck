#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using Result = uint32_t;
struct Service { int handle; };
struct Thread { void (*entry)(void*); void* argument; };
using SmServiceName = uint64_t;
enum NcmStorageId { NcmStorageId_None = 0 };
struct NcmProgramLocation { u64 program_id; NcmStorageId storageID; };
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
namespace host_module {
struct State {
    bool running = true, registered = true, initialized = true, enabled = true, app = false;
    bool legacy = false, late_app = false, save_fail = false, ghost_after_stop = false;
    bool unsafe_kill = false;
    bool other_mitm = false;
    Result mitm_error = 0;
    Result app_error = 0, pid_error = 0, pm_error = 0, sm_error = 0, terminate_error = 0;
    Result create_error = 0, start_error = 0;
    u32 phase = 1, prepare_status = 0;
    int kills = 0, launches = 0, sleeps = 0, ready_after = 0, clear_after = 0, closes = 0;
};
inline State state;
}
inline Result pmdmntGetApplicationProcessId(u64* pid) {
    auto& s = host_module::state;
    if (s.app_error) return s.app_error;
    *pid = s.app ? 42 : 0;
    return s.app ? 0 : 0x20F;
}
inline Result pmdmntGetProcessId(u64* pid, u64) {
    auto& s = host_module::state;
    *pid = s.running ? 43 : 0;
    return s.pid_error ? s.pid_error : s.running ? 0 : 0x20F;
}
inline Result pmshellInitialize() { return host_module::state.pm_error; }
inline void pmshellExit() {}
inline Result pmshellLaunchProgram(u32, const NcmProgramLocation*, u64* pid) {
    auto& s = host_module::state;
    ++s.launches; s.running = true; s.registered = true; *pid = 43;
    return 0;
}
inline Result pmshellTerminateProgram(u64) {
    auto& s = host_module::state;
    if (s.phase != 3 || s.initialized || s.app) s.unsafe_kill = true;
    if (s.terminate_error) return s.terminate_error;
    ++s.kills; s.running = false; s.registered = s.ghost_after_stop;
    return 0;
}
inline SmServiceName smEncodeName(const char*) { return 0; }
inline Service* smGetServiceSessionTipc() { return nullptr; }
inline Result hostHasService(u32 command, u8* available) {
    if (command == 65004) {
        *available = host_module::state.other_mitm;
        return host_module::state.mitm_error;
    }
    *available = host_module::state.registered;
    return host_module::state.sm_error;
}
#define tipcDispatchInOut(s, command, input, output) (static_cast<void>(input), hostHasService(command, &(output)))
inline void svcSleepThread(int64_t) {
    auto& s = host_module::state;
    ++s.sleeps;
    if (s.ready_after && s.sleeps >= s.ready_after) s.phase = 1;
    if (s.clear_after && s.sleeps >= s.clear_after) { s.registered = false; s.clear_after = 0; }
}
inline Result threadCreate(Thread* thread, void (*entry)(void*), void* arg, void*, size_t, int, int) {
    thread->entry = entry; thread->argument = arg;
    return host_module::state.create_error;
}
inline Result threadStart(Thread* thread) {
    if (host_module::state.start_error) return host_module::state.start_error;
    thread->entry(thread->argument);
    return 0;
}
inline void threadClose(Thread*) { ++host_module::state.closes; }
inline void threadWaitForExit(Thread*) {}
