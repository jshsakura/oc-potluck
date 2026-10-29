// The look follows the CookingFoil client: 1280x720 units, Ground/Surface/Ink palette,
// rounded cards, a butter ring on the cursor and key badges in a 60 high footer.
#include "ui.hpp"
#include "touch.hpp"
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

namespace gui {

bool korean = false;
const char* T(const char* en, const char* ko) { return korean ? ko : en; }

namespace {
bool consoleKorean = false;  // the console's own language, for Language::Auto
Language chosen = Language::Auto;
}
void setLanguage(Language language) {
    chosen = language;
    korean = language == Language::Auto ? consoleKorean : language == Language::Korean;
}
Language language() { return chosen; }
const char* languageCode(Language language) {
    return language == Language::English ? "en" : language == Language::Korean ? "ko" : "auto";
}
Language languageFromCode(const std::string& code) {
    return code == "en" ? Language::English : code == "ko" ? Language::Korean : Language::Auto;
}

namespace {

constexpr int Width = 1280, Height = 720, Footer = 60, Left = 48, Right = 1232;

// CookingFoil's light and dark palettes (Theme.hpp), the console's own theme picks one.
constexpr Palette Light{{242, 241, 238, 255}, {255, 255, 255, 255}, {22, 22, 26, 255}, {85, 85, 94, 255},
                        {226, 224, 218, 255}, {30, 142, 90, 255}, {196, 124, 0, 255}, {204, 62, 52, 255},
                        {255, 241, 204, 255}, {253, 226, 226, 255}, {176, 174, 167, 255}, {0, 0, 0, 36},
                        {220, 242, 230, 255}};
constexpr Palette Dark{{18, 18, 22, 255}, {34, 34, 40, 255}, {236, 236, 240, 255}, {160, 160, 170, 255},
                       {56, 56, 64, 255}, {63, 191, 131, 255}, {232, 160, 48, 255}, {240, 104, 92, 255},
                       {72, 60, 30, 255}, {80, 38, 38, 255}, {104, 104, 112, 255}, {255, 255, 255, 30},
                       {24, 64, 46, 255}};

} // namespace

Palette pal = Dark;

namespace {

constexpr SDL_Color Butter{255, 194, 61, 255}, Night{22, 22, 26, 255};

struct Ui {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    PadState pad;
    float scale = 1.0f;
    PlFontData shared{};  // the console's Korean font, when the bundled one will not open
    bool haveShared = false;
    std::map<int, TTF_Font*> faces;  // by size, bold and brand
    TTF_Font* brand = nullptr;
    // Scrolling of the body between the header and the footer
    int scroll = 0;
    int pendingScroll = 0;      // from the right stick and touch drags since the last frame
    touch::Finger finger;
    // Where the last frame put its menu rows and footer keys, for taps
    std::vector<std::pair<touch::Box, int>> rowHits;   // screen rect, menu index
    std::vector<std::pair<touch::Box, u64>> keyHits;   // screen rect, button
    int tappedRow = -1;
    int lastSelected = -1;
    std::string screenKey;      // a new screen starts at the top
} ui;

// Measuring pass: the drawing helpers lay out but put nothing on screen
bool dry = false;

/// The visible body; a row less than half inside it cannot be tapped
constexpr int BodyTop = 86, BodyBottom = 720 - 60 - 8;

/// The button a footer key stands for
u64 buttonOf(char key) {
    switch (key) {
    case 'A': return HidNpadButton_A;
    case 'B': return HidNpadButton_B;
    case 'X': return HidNpadButton_X;
    case 'Y': return HidNpadButton_Y;
    case '+': return HidNpadButton_Plus;
    case '-': return HidNpadButton_Minus;
    default: return 0;
    }
}

// Sizes and layout follow the CookingFoil client (Settings.cpp, Application::footer, Painter): a list
// is one white card of 46 high rows with rules between, the cursor on a row is a butter band with the
// page's ground inside, the footer is 60 high with 32 high key pills, text is 17 to 21 and titles 30.
constexpr int Content = 992, RowHeight = 46, CardRadius = 18, RowRadius = 10;
constexpr int ContentLeft = (Width - Content) / 2, ContentRight = ContentLeft + Content, Inner = ContentLeft + 22;

TTF_Font* face(int size, bool bold = false) {
    const int key = size * 2 + (bold ? 1 : 0);
    const auto found = ui.faces.find(key);
    if (found != ui.faces.end()) return found->second;
    const int px = int(size * ui.scale + 0.5f);
    TTF_Font* f = TTF_OpenFont(bold ? "romfs:/fonts/CookingFoilSans-Bold.otf" : "romfs:/fonts/CookingFoilSans-Regular.otf", px);
    if (!f && ui.haveShared) f = TTF_OpenFontRW(SDL_RWFromMem(ui.shared.address, ui.shared.size), 0, px);
    ui.faces[key] = f;
    return f;
}

void color(SDL_Color c) { SDL_SetRenderDrawColor(ui.renderer, c.r, c.g, c.b, c.a); }

// Shapes are drawn once at 4x on a software surface and scaled down by the renderer, so the corners
// are smooth like the client's; the textures are cached by shape.
constexpr int Super = 4;
SDL_Texture* shapeTexture(int w, int h, int r, SDL_Color c) {
    static std::map<std::array<int, 7>, SDL_Texture*> cache;
    const std::array<int, 7> key{w, h, r, c.r, c.g, c.b, c.a};
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    SDL_Texture* texture = nullptr;
    if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w * Super, h * Super, 32, SDL_PIXELFORMAT_RGBA32)) {
        if (SDL_Renderer* sr = SDL_CreateSoftwareRenderer(s)) {
            SDL_SetRenderDrawBlendMode(sr, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(sr, c.r, c.g, c.b, 0);
            SDL_RenderClear(sr);
            roundedBoxRGBA(sr, 0, 0, w * Super - 1, h * Super - 1, std::min(r, std::min(w, h) / 2) * Super, c.r, c.g, c.b, c.a);
            SDL_DestroyRenderer(sr);
            texture = SDL_CreateTextureFromSurface(ui.renderer, s);
            if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(s);
    }
    cache[key] = texture;
    return texture;
}
void rect(int x, int y, int w, int h, SDL_Color c, int r = 0) {
    if (dry || w <= 0 || h <= 0 || c.a == 0) return;
    if (r <= 0) {
        color(c);
        SDL_Rect d{x, y, w, h};
        SDL_RenderFillRect(ui.renderer, &d);
        return;
    }
    if (SDL_Texture* t = shapeTexture(w, h, r, c)) {
        SDL_Rect d{x, y, w, h};
        SDL_RenderCopy(ui.renderer, t, nullptr, &d);
    }
}
// The cursor on a row inside a card: a 5px butter band around the page's ground (Painter::focusRow).
void focusRow(int x, int y, int w, int h, int r) {
    rect(x, y, w, h, Butter, r);
    rect(x + 5, y + 5, w - 10, h - 10, pal.ground, std::max(0, r - 5));
}

int measure(const std::string& s, int size, bool bold = false) {
    int w = 0, h = 0;
    TTF_SizeUTF8(face(size, bold), s.c_str(), &w, &h);
    return int(w / ui.scale + 0.5f);
}
// A line whose ink is centred on cy, the way the client's Fonts::middle sets it.
int inkTop(const std::string& s, TTF_Font* f, int cy) {
    int top = 0, bottom = 0;
    bool any = false;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        const size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
        Uint32 cp = c < 128 ? c : c < 224 ? c & 31 : c < 240 ? c & 15 : c & 7;
        for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
        i += n;
        int minx, maxx, miny, maxy, adv;
        if (TTF_GlyphMetrics32(f, cp, &minx, &maxx, &miny, &maxy, &adv) != 0) continue;
        top = any ? std::max(top, maxy) : maxy;
        bottom = any ? std::min(bottom, miny) : miny;
        any = true;
    }
    const float ink = any ? (top + bottom) / 2.0f : TTF_FontAscent(f) * 0.35f;
    return int(cy + (ink - TTF_FontAscent(f)) / ui.scale + 0.5f);
}
void drawText(const std::string& s, int x, int y, SDL_Color c, TTF_Font* f, int wrap = 0) {
    if (dry || s.empty() || !f) return;
    SDL_Surface* surface = wrap ? TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), c, Uint32(wrap * ui.scale)) : TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surface) return;
    if (SDL_Texture* t = SDL_CreateTextureFromSurface(ui.renderer, surface)) {
        SDL_Rect r{x, y, int(surface->w / ui.scale + 0.5f), int(surface->h / ui.scale + 0.5f)};
        SDL_RenderCopy(ui.renderer, t, nullptr, &r);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(surface);
}
// Text with its ink centred on cy (labels, values, badges); returns the right end.
int middle(const std::string& s, int x, int cy, int size, SDL_Color c, bool bold = false) {
    TTF_Font* f = face(size, bold);
    drawText(s, x, inkTop(s, f, cy), c, f);
    return x + measure(s, size, bold);
}
// Wrapped text from the top; returns its height (drawn only when asked).
int paragraph(const std::string& s, int x, int y, int w, int size, SDL_Color c, bool draw = true) {
    TTF_Font* f = face(size);
    if (!f || s.empty()) return 0;
    SDL_Surface* sf = TTF_RenderUTF8_Blended_Wrapped(f, s.c_str(), c, Uint32(w * ui.scale));
    if (!sf) return 0;
    const int h = int(sf->h / ui.scale + 0.5f);
    SDL_FreeSurface(sf);
    if (draw) drawText(s, x, y, c, f, w);
    return h;
}

// A line icon centred on cy: rasterized once per kind and size at the screen's resolution, white,
// and tinted when drawn (the client's Painter::icon).
constexpr int IconSize = 24, IconGap = 14;
void icon(Icon kind, int x, int cy, SDL_Color c, int size = IconSize) {
    const char* path = iconPath(kind);
    if (dry || !path) return;
    static std::map<std::pair<int, int>, SDL_Texture*> cache;
    const auto key = std::make_pair(int(kind), size);
    auto found = cache.find(key);
    if (found == cache.end()) {
        const std::string px = std::to_string(int(size * ui.scale + 0.5f));
        const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + px + "\" height=\"" + px +
                                "\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" +
                                path + "</svg>";
        SDL_Texture* texture = nullptr;
        if (SDL_RWops* rw = SDL_RWFromConstMem(svg.data(), int(svg.size()))) {
            if (SDL_Surface* s = IMG_LoadSVG_RW(rw)) {
                texture = SDL_CreateTextureFromSurface(ui.renderer, s);
                SDL_FreeSurface(s);
            }
            SDL_RWclose(rw);
        }
        if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        found = cache.emplace(key, texture).first;
    }
    if (!found->second) return;
    SDL_SetTextureColorMod(found->second, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(found->second, c.a);
    const SDL_Rect d{x, cy - size / 2, size, size};
    SDL_RenderCopy(ui.renderer, found->second, nullptr, &d);
}

// A controller key: an ink pill 32 high, the key centred in it (Painter::key).
int keyWidth(const std::string& name) { return std::max(32, measure(name, 18, true) + 20); }
int key(const std::string& name, int x, int cy, bool enabled = true) {
    const int w = keyWidth(name);
    rect(x, cy - 16, w, 32, enabled ? pal.ink : pal.muted, 16);
    middle(name, x + (w - measure(name, 18, true)) / 2, cy, 18, pal.surface, true);
    return w;
}

// CookingFoil's wordmark: Baloo 2 ExtraBold, lowercase, the full stop in butter.
int wordmark(int x, int cy, int size) {
    TTF_Font* f = ui.brand;
    if (!f) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, "potluck", &w, &h);
    const int base = int(w / ui.scale + 0.5f);
    const int top = inkTop("potluck", f, cy);
    drawText("potluck", x, top, pal.ink, f);
    drawText(".", x + base - 1, top, Butter, f);
    int dw = 0;
    TTF_SizeUTF8(f, ".", &dw, &h);
    (void)size;
    return base + int(dw / ui.scale + 0.5f);
}

// Everything between the header and the footer, from y; returns the bottom.
// selTop/selBottom receive the selected menu row, when there is one.
int bodyAt(const View& v, int y, int* selTop, int* selBottom) {

    if (!v.rows.empty()) {
        const int pitch = 40, h = 16 + int(v.rows.size()) * pitch;
        rect(ContentLeft, y, Content, h, pal.surface, CardRadius);
        int ry = y + 8;
        for (size_t i = 0; i < v.rows.size(); ++i) {
            middle(v.rows[i].label, Inner, ry + pitch / 2, 20, pal.secondary, true);
            const int w = measure(v.rows[i].value, 20, true);
            middle(v.rows[i].value, ContentRight - 22 - w, ry + pitch / 2, 20, v.rows[i].color, true);
            if (i + 1 < v.rows.size()) rect(ContentLeft, ry + pitch - 1, Content, 1, pal.line);
            ry += pitch;
        }
        y += h + 12;
    }
    if (!v.lines.empty()) {
        const int size = v.compact ? 19 : 21, wrap = Content - 44;
        int h = 32;
        for (const auto& l : v.lines) h += paragraph(l, 0, 0, wrap, size, pal.ink, false) + 6;
        rect(ContentLeft, y, Content, h, pal.surface, CardRadius);
        int ly = y + 16;
        for (const auto& l : v.lines) ly += paragraph(l, Inner, ly, wrap, size, pal.ink) + 6;
        y += h + 12;
    }
    for (const auto& sec : v.sections) {
        const int size = 18, wrap = Content - 44, gap = 4, headingHeight = 30;
        int h = 14 + headingHeight;
        for (const auto& l : sec.lines) h += paragraph(l, 0, 0, wrap, size, pal.ink, false) + gap;
        rect(ContentLeft, y, Content, h + 8, pal.surface, CardRadius);
        middle(sec.heading, Inner, y + 14 + headingHeight / 2 - 2, 19, pal.secondary, true);
        int ly = y + 14 + headingHeight;
        for (const auto& l : sec.lines) ly += paragraph(l, Inner, ly, wrap, size, pal.ink) + gap;
        y += h + 8 + 10;
    }
    for (const auto& n : v.notes) {
        const int wrap = Content - 44, h = paragraph(n.text, 0, 0, wrap, 17, pal.ink, false) + 24;
        rect(ContentLeft, y, Content, h, n.kind == Note::Warn ? pal.cream : n.kind == Note::Good ? pal.greenTint : pal.redTint, 12);
        paragraph(n.text, Inner, y + 12, wrap, 17, pal.ink);
        y += h + 12;
    }
    if (v.progress >= 0) {
        rect(ContentLeft, y + 4, Content, 10, pal.line, 5);
        rect(ContentLeft, y + 4, std::max(10, int(Content * v.progress)), 10, Butter, 5);
        y += 30;
    }
    // The menu is one card of rows, like the client's settings; rows shrink a little to stay above the footer.
    if (!v.menu.empty()) {
        const int count = int(v.menu.size());
        const int rowH = RowHeight;  // the body scrolls instead of squeezing rows
        const int h = count * rowH + 16;
        rect(ContentLeft, y, Content, h, pal.surface, CardRadius);
        int ry = y + 8;
        for (int i = 0; i < count; ++i) {
            if (i == v.selected) {
                focusRow(ContentLeft + 14, ry, Content - 28, rowH - 2, RowRadius);
                if (selTop) *selTop = ry;
                if (selBottom) *selBottom = ry + rowH;
            }
            if (touch::Box hit; !dry && touch::visiblePart({ContentLeft, ry, Content, rowH}, BodyTop, BodyBottom, hit)) ui.rowHits.push_back({hit, i});
            const SDL_Color ink = v.menu[i].enabled ? pal.ink : pal.muted;
            const int cy = ry + (rowH - 2) / 2;
            int labelX = Inner;
            if (v.menu[i].icon != Icon::None) {
                icon(v.menu[i].icon, Inner, cy, ink);
                labelX += IconSize + IconGap;
            }
            middle(v.menu[i].label, labelX, cy, 21, ink, true);
            if (!v.menu[i].value.empty())
                middle(v.menu[i].value, ContentRight - 22 - measure(v.menu[i].value, 19), ry + (rowH - 2) / 2, 19, pal.secondary);
            if (i + 1 < count) rect(ContentLeft, ry + rowH - 1, Content, 1, pal.line);
            ry += rowH;
        }
    }

    return y;
}

} // namespace

std::string title() { return T("PotLuck Installer", "PotLuck 설치 도우미"); }

void draw(const View& v) {
    color(pal.ground);
    SDL_RenderClear(ui.renderer);
    // Header: the wordmark always at the left; a sub screen names itself at the right.
    wordmark(ContentLeft, 48, 40);
    if (v.title != title()) {
        const int w = measure(v.title, 24, true);
        middle(v.title, ContentRight - w, 48, 24, pal.ink, true);
    }
    // The body scrolls inside the space between the header and the footer
    constexpr int Top = BodyTop, Bottom = BodyBottom;
    ui.rowHits.clear();
    ui.keyHits.clear();
    const std::string screen = v.title + "|" + std::to_string(v.menu.size()) + "|" + std::to_string(v.lines.size()) + "|" +
                            std::to_string(v.sections.size()) + "|" + (v.lines.empty() ? std::string() : v.lines[0]);
    if (screen != ui.screenKey) {
        ui.screenKey = screen;
        ui.scroll = 0;
        ui.lastSelected = -1;
    }
    int selTop = -1, selBottom = -1;
    dry = true;
    const int height = bodyAt(v, 0, &selTop, &selBottom);
    dry = false;
    const int view = Bottom - Top, most = std::max(0, height - view);
    ui.scroll += ui.pendingScroll;
    ui.pendingScroll = 0;
    if (v.selected != ui.lastSelected && selTop >= 0) {
        // Follow the cursor, but leave the page where the user scrolled it otherwise
        if (selTop < ui.scroll) ui.scroll = selTop;
        if (selBottom > ui.scroll + view) ui.scroll = selBottom - view;
        ui.lastSelected = v.selected;
    }
    ui.scroll = std::clamp(ui.scroll, 0, most);
    SDL_Rect clip{0, Top, Width, view};
    SDL_RenderSetClipRect(ui.renderer, &clip);
    bodyAt(v, Top - ui.scroll, nullptr, nullptr);
    SDL_RenderSetClipRect(ui.renderer, nullptr);
    if (most > 0) {
        // A thin bar at the right shows where the page is
        const int barH = std::max(40, view * view / height);
        const int barY = Top + (view - barH) * ui.scroll / most;
        rect(ContentRight + 14, Top, 6, view, pal.line, 3);
        rect(ContentRight + 14, barY, 6, barH, pal.secondary, 3);
    }

    // Footer: 60 high on the surface, a rule on top, key pills ending at x 1248, centres at y 691 (Application::footer).
    rect(0, Height - Footer, Width, Footer, pal.surface);
    rect(0, Height - Footer, Width, 1, pal.line);
    int x = 1248;
    for (auto it = v.hints.rbegin(); it != v.hints.rend(); ++it) {
        const bool pair = it->key == Shoulders;
        const std::string name(1, it->key);
        const int keys = pair ? keyWidth("L") + 4 + keyWidth("R") : keyWidth(name);
        const int label = measure(it->label, 19);
        x -= label;
        middle(it->label, x, 691, 19, pal.ink);
        x -= keys + 10;
        if (pair) {
            // L and R are two targets: L and the left half go back a page, R and the label go forward
            const int w = key("L", x, 691);
            key("R", x + w + 4, 691);
            const int split = x + w + 2;
            ui.keyHits.push_back({{x - 6, Height - Footer, split - (x - 6), Footer}, HidNpadButton_L});
            ui.keyHits.push_back({{split, Height - Footer, x + keys + 10 + label + 6 - split, Footer}, HidNpadButton_R});
        } else {
            key(name, x, 691);
            // The key and its label together are one tap target
            if (const u64 button = buttonOf(it->key)) ui.keyHits.push_back({{x - 6, Height - Footer, keys + 10 + label + 12, Footer}, button});
        }
        x -= 30;
    }
    // The quiet guidance line sits at the footer's left, up to the key hints.
    if (!v.hint.empty()) {
        const int room = x - 24 - ContentLeft;
        if (room > 120) {
            const int h = paragraph(v.hint, 0, 0, room, 17, pal.secondary, false);
            const int hh = std::min(h, Footer - 8);
            paragraph(v.hint, ContentLeft, Height - Footer + (Footer - hh) / 2, room, 17, pal.secondary);
        }
    }
    SDL_RenderPresent(ui.renderer);
}

u64 pressed() {
    padUpdate(&ui.pad);
    constexpr int StickStep = 18;
    const u64 held = padGetButtons(&ui.pad);
    if (held & HidNpadButton_StickRDown) ui.pendingScroll += StickStep;
    if (held & HidNpadButton_StickRUp) ui.pendingScroll -= StickStep;
    u64 down = padGetButtonsDown(&ui.pad);
    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
        ui.pendingScroll += ui.finger.down(int(touch.touches[0].x), int(touch.touches[0].y)).scroll;
    } else if (const touch::Finger::Step up = ui.finger.up(); up.tapped) {
        // A tap presses the row or footer key under it
        for (const auto& hit : ui.rowHits)
            if (touch::inside(hit.first, up.x, up.y)) ui.tappedRow = hit.second;
        for (const auto& hit : ui.keyHits)
            if (touch::inside(hit.first, up.x, up.y)) down |= hit.second;
    }
    return down;
}

int tappedRow() {
    const int row = ui.tappedRow;
    ui.tappedRow = -1;
    return row;
}

bool setup() {
    u64 language = 0;
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&language))) consoleKorean = language == 0x6f6b;
        setLanguage(chosen);
        setExit();
    }
    if (R_SUCCEEDED(setsysInitialize())) {
        ColorSetId id = ColorSetId_Light;
        if (R_SUCCEEDED(setsysGetColorSetId(&id))) pal = id == ColorSetId_Dark ? Dark : Light;
        setsysExit();
    }
    romfsInit();
    plInitialize(PlServiceType_User);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&ui.pad);
    hidInitializeTouchScreen();
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) return false;
    ui.window = SDL_CreateWindow("potluck_installer", 0, 0, Width, Height, SDL_WINDOW_FULLSCREEN);
    if (!ui.window) return false;
    ui.renderer = SDL_CreateRenderer(ui.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.renderer) return false;
    SDL_SetRenderDrawBlendMode(ui.renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(ui.renderer, Width, Height);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    int outW = Width, outH = Height;
    SDL_GetRendererOutputSize(ui.renderer, &outW, &outH);
    ui.scale = std::max(1.0f, outW / float(Width));
    // CookingFoil Sans (Pretendard subset, OFL) like the client; the console's Korean font if it will not open.
    if (R_SUCCEEDED(plGetSharedFontByType(&ui.shared, PlSharedFontType_KO))) ui.haveShared = true;
    ui.brand = TTF_OpenFont("romfs:/fonts/Baloo2-ExtraBold.ttf", int(44 * ui.scale + 0.5f));
    if (!ui.brand) ui.brand = TTF_OpenFont("romfs:/fonts/CookingFoilSans-Bold.otf", int(44 * ui.scale + 0.5f));
    return face(21, true) && face(19) && ui.brand;
}

void teardown() {
    for (auto& f : ui.faces) if (f.second) TTF_CloseFont(f.second);
    if (ui.brand) TTF_CloseFont(ui.brand);
    if (ui.renderer) SDL_DestroyRenderer(ui.renderer);
    if (ui.window) SDL_DestroyWindow(ui.window);
    TTF_Quit();
    SDL_Quit();
    plExit();
    romfsExit();
}

} // namespace gui
