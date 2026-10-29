// PotLuck Installer: shows the newest release next to the installed one, downloads the
// release zip, unpacks it onto the SD card, edits config.ini, and removes it again.
// The screens live in ui.cpp (drawing), views.cpp (what they show) and actions.cpp (what keys do).
#include "app.hpp"

int main() {
    using namespace gui;
    if (!setup()) { teardown(); return 1; }
    setLanguage(languageFromCode(inst::readLanguage()));
    App app;
    app.online = net::init();
    check(app);
    bool quit = false;
    while (!quit && appletMainLoop()) {
        draw(viewOf(app));
        u64 down = pressed();
        // A tapped row is chosen and pressed, like moving there and pressing A
        if (const int row = tappedRow(); row >= 0) {
            app.selected = row;
            down |= HidNpadButton_A;
        }
        handle(app, down, quit);
    }
    if (app.online) net::exit();
    teardown();
    return 0;
}
