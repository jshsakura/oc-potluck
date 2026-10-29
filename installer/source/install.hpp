#pragma once
#include <functional>
#include <string>
#include "logic.hpp"
#include "sdfs.hpp"

namespace inst {

using Progress = std::function<void(int done, int total)>;

constexpr const char* WorkDir = "config/potluck_installer/";
constexpr const char* LedgerFile = "config/potluck_installer/installed.txt";
constexpr const char* LanguageFile = "config/potluck_installer/language.txt";  // "auto", "en" or "ko"
constexpr const char* LogDir = "config/potluck/";  // the module's and the overlay's logs

struct State {
    bool present = false;         // the PotLuck folder holds this module
    std::string tag;              // empty when it was not installed by this app
    bool enabled = false;         // PotLuck's flags/boot2.flag is set
    bool legacyOurs = false;      // the old id folder holds the module of the older builds (ryu_ldn_nx)
    bool mitmPresent = false;     // the old id folder holds another module: ldn_mitm
    bool mitmFlag = false;        // and its flags/boot2.flag is set (both on ldn:u crash at boot)
    bool mitmDisabledByUs = false;  // this app turned that flag off, and removal turns it back on
    bool legacyLeft = false;      // the old overlay or the old settings folder is still on the card
    bool leftover = false;        // a removal stopped after toolbox.json: only empty folders or the ledger are left
};

enum class Code { Ok, ZipOpen, NotRelease, Corrupt, TooLarge, Read, Write, Remove, NotOurs, Migrate, LegacyConflict };
struct Outcome {
    Code code = Code::Ok;
    int written = 0, kept = 0, skipped = 0, removed = 0;
    bool legacyPending = false;   // old files wait until their settings are in the new config.ini
    bool mitmDisabled = false;    // install: ldn_mitm's boot flag was turned off
    bool mitmRestored = false;    // removal: ldn_mitm's boot flag was put back
};

/** Unpacked-size limits of one install; the defaults are the real ones, tests pass smaller. */
struct Limits {
    unsigned long entry = MaxEntryBytes;
    unsigned long total = MaxUnpackedBytes;
};

State readState();
/** Finish what an interrupted install left: roll its files back and bring back any ".bak" copy. */
bool recoverInterrupted();

/** Move the older build's files (toolbox name ryu_ldn_nx) from ldn_mitm's id to PotLuck's, after the new
 *  files are in place. Only the old build's own files and copies identical to the new ones are deleted; other
 *  files move over when the new folder lacks them, and anything else is a LegacyConflict that changes nothing.
 *  `shipped` names the release's files (they replace the old build's). */
Code migrateLegacy(const std::vector<std::string>& shipped = {});
/** Remove the old overlay once PotLuck is installed, and the old settings folder once every key the module
 *  reads from it is in the new config.ini. True when nothing is left. */
bool cleanupLegacy();
/** Delete config/ryu_ldn_nx/ alone: for a new config.ini that was just written from it. */
bool removeLegacySettings();
/** Delete the older version's overlay and settings now, without waiting for them to be carried over. */
bool removeLegacy();

/** The server address now in config.ini: host and port, empty when the key is not there. */
struct Server { std::string host, port; };
Server readServer();
/** Write one [server] key (host or port) into config.ini, leaving every other line as it is.
 *  Returns Code::Read when the file cannot be read and Code::Write when it cannot be saved; either way
 *  config.ini stays as it was. Without a config.ini the older version's settings are its start, if any.
 *  The value must already be validated. */
Code setServerKey(const std::string& key, const std::string& value);
/** Write any one key of config.ini the same way (changed line only, a missing key at the end of its section, a missing section at the end of the file). */
Code setConfigKey(const std::string& section, const std::string& key, const std::string& value);

/** P2P and diagnostics settings, with the module's defaults when a key is absent. */
struct Options {
    bool p2p = false;     // [ldn] disable_p2p = 0; the module's default is 1 (relay only)
    bool debug = false;   // [debug] enabled: the module writes config/potluck/potluck.log
    int level = 1;        // [debug] level 0..3
    std::string mode = "remote";  // [ldn] mode: remote (default), local or off. The overlay changes it, this app only shows it.
};
Options readOptions();
/** The language picked in this app ("auto", "en" or "ko"); "auto" when none was saved. */
std::string readLanguage();
Code writeLanguage(const std::string& code);
/** Clear the server address: delete the host and port lines. Without a host the module plays in local mode. */
Code clearServer();
/** Unpack a release zip onto the SD card, only below the paths extractable() names. Every file is staged and
 *  checked first; then they replace the old ones together, and PotLuck's boot flag is set last. */
Outcome installZip(const std::string& zipRel, const std::string& tag, const Progress& progress, const Limits& limits = {});
/** Remove what this app put there. config.ini stays unless `withSettings`. */
Outcome uninstall(bool withSettings);

} // namespace inst
