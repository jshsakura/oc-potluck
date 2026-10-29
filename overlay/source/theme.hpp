#pragma once

/*
 * PotLuck palette: CookingFoil butter (#ffc23d) on warm neutrals, light or
 * dark following the console theme. Applied once at startup in place of the
 * user's Ultrahand theme, so the overlay always looks the same.
 */

#include <tesla.hpp>

namespace overlay::theme {

struct Palette {
    const char* background;
    const char* card;
    const char* text;
    const char* muted;
    const char* accent;      ///< bars, borders and the selection frame
    const char* accentText;  ///< accent used for text, readable on the background
    const char* separator;
    const char* good;
    const char* bad;
};

inline constexpr Palette Dark{"171511", "2A2620", "F5EFE3", "A89F8E", "FFC23D", "FFC23D",
                              "3A342A", "8FD18B", "FF7B6B"};
inline constexpr Palette Light{"FBF7EE", "EFE7D6", "2B2620", "7A7062", "FFC23D", "A86F00",
                               "D9CFBC", "2E7D32", "C62828"};

inline const Palette* current = &Dark;

inline tsl::Color Rgb(const char* hex, size_t alpha = 15) { return tsl::RGB888(hex, alpha); }

/** @brief Pick the palette from the console theme and install it into libtesla. */
inline void Apply() {
    ColorSetId color_set = ColorSetId_Dark;
    if (R_SUCCEEDED(setsysInitialize())) {
        if (R_FAILED(setsysGetColorSetId(&color_set))) color_set = ColorSetId_Dark;
        setsysExit();
    }
    current = color_set == ColorSetId_Light ? &Light : &Dark;
    const Palette& p = *current;

    // Claim the theme so libtesla does not load the Ultrahand one over ours
    ult::themeIsInitialized.store(true, std::memory_order_release);

    tsl::defaultBackgroundColor = Rgb(p.background, 14);
    tsl::defaultTextColor = Rgb(p.text);
    tsl::headerTextColor = Rgb(p.text);
    tsl::headerSeparatorColor = Rgb(p.accent);
    tsl::topSeparatorColor = Rgb(p.separator);
    tsl::bottomSeparatorColor = Rgb(p.separator);
    tsl::bottomTextColor = Rgb(p.muted);
    tsl::buttonColor = Rgb(p.muted);
    tsl::clockColor = Rgb(p.muted);
    tsl::bannerVersionTextColor = Rgb(p.muted);
    tsl::sectionTextColor = Rgb(p.accentText);
    tsl::onTextColor = Rgb(p.accentText);
    tsl::offTextColor = Rgb(p.muted);
    tsl::selectionBGColor = Rgb(p.card, 13);
    tsl::highlightColor1 = Rgb(p.accent);
    tsl::highlightColor2 = Rgb(p.accent);
    tsl::highlightColor3 = Rgb(p.accent);
    tsl::highlightColor4 = Rgb(p.accent);
    tsl::clickColor = Rgb(p.accent, 6);
    tsl::separatorColor = Rgb(p.separator);
    tsl::edgeSeparatorColor = Rgb(p.separator);
    tsl::textSeparatorColor = Rgb(p.separator);
    tsl::selectedTextColor = Rgb(p.accentText);
    tsl::selectedValueTextColor = Rgb(p.accentText);
    tsl::invalidTextColor = Rgb(p.bad);
    tsl::scrollBarColor = Rgb(p.separator);
    tsl::scrollBarWallColor = Rgb(p.muted);
}

} // namespace overlay::theme
