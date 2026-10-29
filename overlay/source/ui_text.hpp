#pragma once

#include <cstdint>

namespace overlay {

inline bool use_korean = false;

/** @brief Select Korean for the system's ko language code; otherwise use English. */
inline void SelectLanguage(uint64_t language_code) {
    use_korean = language_code == 0x6f6b;
}

/** @brief Select static text without a translation table or heap allocation. */
inline const char* Text(const char* english, const char* korean) {
    return use_korean ? korean : english;
}

} // namespace overlay
