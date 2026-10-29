#include "icons.hpp"

namespace gui {

const char* iconPath(Icon icon) {
    switch (icon) {
    case Icon::None: return nullptr;
    case Icon::Download: return R"(<path d="M12 3v12M7 10l5 5 5-5M4 20h16"/>)";
    case Icon::Server: return R"(<rect x="2" y="2" width="20" height="8" rx="2"/><rect x="2" y="14" width="20" height="8" rx="2"/><path d="M6 6h.01M6 18h.01"/>)";
    case Icon::Sliders: return R"(<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/>)";
    case Icon::Gamepad: return R"(<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 12h4M8 10v4M15 13h.01M18 11h.01"/>)";
    case Icon::Shield: return R"(<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/><path d="M9 12l2 2 4-4"/>)";
    case Icon::Trash: return R"(<path d="M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6M10 11v6M14 11v6"/>)";
    case Icon::Info: return R"(<circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/>)";
    case Icon::Power: return R"(<path d="M12 2v10"/><path d="M18.4 6.6a9 9 0 1 1-12.77.04"/>)";
    case Icon::Archive: return R"(<rect x="2" y="3" width="20" height="5" rx="1"/><path d="M4 8v11a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8M10 12h4"/>)";
    case Icon::Globe: return R"(<circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>)";
    case Icon::Back: return R"(<path d="M15 18l-6-6 6-6"/>)";
    case Icon::Refresh: return R"(<path d="M21 12a9 9 0 1 1-2.6-6.4"/><path d="M21 4v5h-5"/>)";
    case Icon::Wifi: return R"(<path d="M12 20h.01"/><path d="M2 8.82a15 15 0 0 1 20 0"/><path d="M5 12.86a10 10 0 0 1 14 0"/><path d="M8.5 16.43a5 5 0 0 1 7 0"/>)";
    case Icon::Hash: return R"(<path d="M4 9h16M4 15h16M10 3L8 21M16 3l-2 18"/>)";
    case Icon::File: return R"(<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/>)";
    case Icon::Layers: return R"(<path d="M12 2L2 7l10 5 10-5-10-5z"/><path d="M2 17l10 5 10-5"/><path d="M2 12l10 5 10-5"/>)";
    case Icon::Share: return R"(<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><path d="M8.59 13.51l6.83 3.98M15.41 6.51l-6.82 3.98"/>)";
    }
    return nullptr;
}

} // namespace gui
