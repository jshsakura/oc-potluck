#pragma once
// The app's state and its screens: views.cpp says what each screen shows, actions.cpp what its keys do.
#include <string>
#include <vector>
#include "games.hpp"
#include "install.hpp"
#include "net.hpp"
#include "safety.hpp"
#include "ui.hpp"

namespace gui {

enum class Screen { Main, Server, Options, Games, Safety, ConfirmDelete, ConfirmLegacy, Done, Help };
constexpr int GamePage = 8;  // games per page
enum class MainItem { Install, Server, Options, Games, Language, Safety, Remove, Help, Legacy, Restart };

struct App {
    Screen screen = Screen::Main;
    int selected = 0;
    net::Latest latest;
    inst::State state;
    bool otherMitm = false;
    bool newerFirmware = false;  // the console runs a firmware newer than this build supports
    bool online = false;
    std::vector<std::string> result;
    std::vector<inst::Risk> risks;
    bool safe = false;                    // inst::isSafe: a hidden identity, or nothing found
    std::vector<std::string> confirmed;   // checks the running system passed, shown as such
    inst::Server server;  // host and port now in config.ini
    inst::Options options;
    std::vector<inst::Game> games;  // installed games on the module's list, for viewing
    int gamePage = 0;
    std::vector<std::string> dnsDetail;  // what the console resolved, shown so a wrong verdict can be told from a right one
    std::vector<std::string> sdSettings;  // exosphere.ini and emummc.ini: what was asked for, shown apart from the checks
    bool recoveryFailed = false;  // an interrupted install could not be rolled back
};

// views.cpp
View viewOf(const App& app);
std::string codeText(inst::Code c);
std::string settingsError(inst::Code c);
std::vector<std::string> sdSettingLines(const inst::Facts& f);
std::vector<std::string> confirmedLines(const inst::Facts& f);
std::vector<MainItem> mainItems(const App& app);
int indexOf(const App& app, MainItem item);

// actions.cpp
/** One line on an otherwise empty screen while something slow runs. */
void busy(const char* en, const char* ko);
/** Reread the card's state; check() also asks for the newest release and tidies up first. */
void refresh(App& app);
void check(App& app);
void handle(App& app, u64 down, bool& quit);

} // namespace gui
