// config.ini: read for the screens, and edited one key at a time. A file that cannot be read in full is
// never rewritten; the new text goes through config.ini.part and a ".bak" swap.
#include "install.hpp"

namespace inst {

namespace {
std::string legacyConfig() { return std::string(LegacyConfigDir) + "config.ini"; }

// The text to edit: config.ini, or the older version's settings when there is no config.ini yet, so the first
// save does not leave them behind (the module only carries them over while config.ini is missing).
Code loadForEdit(std::string& text, bool& fromLegacy) {
    const Text current = readText(ConfigFile);
    if (current.status == ReadStatus::Ok) { text = current.data; return Code::Ok; }
    if (current.status == ReadStatus::Error) return Code::Read;
    const Text old = readText(legacyConfig());
    if (old.status == ReadStatus::Error) return Code::Read;
    fromLegacy = old.status == ReadStatus::Ok;
    text = fromLegacy ? carriedConfig(old.data) : "";
    return Code::Ok;
}
} // namespace

Server readServer() {
    const std::string text = readText(ConfigFile).data;  // shown only: an unreadable file shows the defaults
    return {iniGet(text, ServerSection, HostKey), iniGet(text, ServerSection, PortKey)};
}

Code setConfigKey(const std::string& section, const std::string& key, const std::string& value) {
    std::string text;
    bool fromLegacy = false;
    const Code loaded = loadForEdit(text, fromLegacy);
    if (loaded != Code::Ok) return loaded;
    if (!writeSafe(ConfigFile, iniSet(text, section, key, value))) return Code::Write;
    if (fromLegacy) removeLegacySettings();  // the new file was written from every old key
    return Code::Ok;
}

Code setServerKey(const std::string& key, const std::string& value) { return setConfigKey(ServerSection, key, value); }

Options readOptions() {
    const std::string text = readText(ConfigFile).data;
    Options o;
    const auto flag = [&](const char* section, const char* key, bool fallback) {
        const std::string v = iniGet(text, section, key);
        return v == "1" ? true : v == "0" ? false : fallback;
    };
    o.p2p = !flag("ldn", "disable_p2p", true);
    o.debug = flag("debug", "enabled", false);
    const std::string mode = iniGet(text, "ldn", "mode");
    if (mode == "local" || mode == "off") o.mode = mode;
    const std::string level = iniGet(text, "debug", "level");
    if (level.size() == 1 && level[0] >= '0' && level[0] <= '3') o.level = level[0] - '0';
    return o;
}

std::string readLanguage() {
    std::string code = readText(LanguageFile).data;
    while (!code.empty() && (code.back() == '\n' || code.back() == '\r' || code.back() == ' ')) code.pop_back();
    return code == "en" || code == "ko" ? code : "auto";
}

Code writeLanguage(const std::string& code) {
    return writeSafe(LanguageFile, code + "\n") ? Code::Ok : Code::Write;
}

Code clearServer() {
    const Text current = readText(ConfigFile);
    if (current.status == ReadStatus::Missing) return Code::Ok;
    if (current.status == ReadStatus::Error) return Code::Read;
    const std::string next = iniRemove(iniRemove(current.data, ServerSection, HostKey), ServerSection, PortKey);
    return writeSafe(ConfigFile, next) ? Code::Ok : Code::Write;
}

} // namespace inst
