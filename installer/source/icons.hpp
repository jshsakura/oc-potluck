#pragma once
// Line icons for menu rows, the CookingFoil client's set: SVG path data on a 24 unit grid,
// stroked 2.2 wide with round ends. Most are redrawn from Lucide (ISC, https://lucide.dev/license).

namespace gui {

enum class Icon { None, Download, Server, Sliders, Gamepad, Shield, Trash, Info, Power, Archive, Globe, Back, Refresh, Wifi, Hash, File, Layers, Share };

/** The icon's SVG elements, or nullptr for Icon::None. */
const char* iconPath(Icon icon);

} // namespace gui
