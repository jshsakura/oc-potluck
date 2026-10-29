#pragma once
// The screen toolkit: CookingFoil's palette, the View a screen is described by, and drawing it.
#include <SDL2/SDL.h>
#include <string>
#include <switch.h>
#include <vector>
#include "icons.hpp"

namespace gui {

extern bool korean;
/** The English or the Korean text, by the language chosen in the app (the console's by default). */
const char* T(const char* en, const char* ko);

/** The app's language: the console's (Auto) or one picked in the menu. */
enum class Language { Auto, English, Korean };
void setLanguage(Language language);
Language language();
/** The saved form of a language ("auto", "en", "ko") and back; anything else is Auto. */
const char* languageCode(Language language);
Language languageFromCode(const std::string& code);

struct Palette {
    SDL_Color ground, surface, ink, secondary, line, good, pending, bad, cream, redTint, muted, press, greenTint;
};
extern Palette pal;

struct Row {
    std::string label, value;
    SDL_Color color;
};
enum class Note { Warn, Bad, Good };
struct Notice {
    Note kind;
    std::string text;
};
constexpr char Shoulders = '/';
struct Hint {
    char key;  // 'A', 'B', '+' ...; Shoulders stands for the L and R pair
    std::string label;
};
struct Item {
    std::string label;
    bool enabled = true;
    std::string value;  // quiet text at the row's right end
    Icon icon = Icon::None;  // line icon before the label
};
/** A titled card of short lines (the help screen). */
struct Section {
    std::string heading;
    std::vector<std::string> lines;
};
struct View {
    std::string title;
    std::vector<Row> rows;      // a label/value card
    std::vector<Notice> notes;  // cream (warning), red (error) and green (safe) cards
    std::vector<std::string> lines;  // plain text card
    std::vector<Section> sections;   // titled cards, after the text card
    bool compact = false;       // lines in the small font (long pages)
    std::vector<Item> menu;
    int selected = 0;
    std::vector<Hint> hints;
    std::string hint;           // one quiet line inside the footer
    double progress = -1;       // 0..1 draws a bar
};

/** The app's own name, the title of the main screen. */
std::string title();
/** Draw one whole frame. */
void draw(const View& v);
/** Buttons pressed since the last frame; a tap on a footer key counts as its button. */
u64 pressed();
/** The menu row tapped since the last call, or -1. Call after pressed(). */
int tappedRow();
/** Language, theme, fonts, window. False when the screen cannot be used. */
bool setup();
void teardown();

} // namespace gui
