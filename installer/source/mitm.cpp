#include "mitm.hpp"
#include <switch.h>

namespace inst {

namespace {
// Atmosphere's sm extension: is a mitm installed for the service? (the overlay asks the same)
bool hasMitm(const char* service) {
    u8 installed = 0;
    const SmServiceName name = smEncodeName(service);
    return R_SUCCEEDED(tipcDispatchInOut(smGetServiceSessionTipc(), 65004, name, installed)) && (installed & 1);
}
// the module's own service: present only while that module runs.
bool moduleRunning() {
    u8 available = 0;
    const SmServiceName name = smEncodeName("potluck");
    return R_SUCCEEDED(tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, available)) && (available & 1);
}
} // namespace

bool otherMitmActive() {
    return !moduleRunning() && (hasMitm("ldn:u") || hasMitm("bsd:u"));
}

bool reloadModuleConfig() {
    constexpr u32 ReloadConfig = 17;  // potluck::ipc::ConfigCmd::ReloadConfig
    Service service;
    if (R_FAILED(smGetService(&service, "potluck"))) return false;
    u32 result = 1;
    const bool ok = R_SUCCEEDED(serviceDispatchOut(&service, ReloadConfig, result)) && result == 0;
    serviceClose(&service);
    return ok;
}

} // namespace inst
