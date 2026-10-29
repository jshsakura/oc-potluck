#pragma once

/*
 * Newest firmware this build supports: the newest one its Atmosphere libraries
 * (sysmodule/Atmosphere-libs) know. A console on a newer firmware may run the
 * module wrongly, so the installer and the overlay warn instead of letting it
 * fail without a word. Raise this together with Atmosphere-libs;
 * scripts/ci/check-firmware.sh fails the build when the two disagree.
 */

#define POTLUCK_MAX_FIRMWARE_MAJOR 23
#define POTLUCK_MAX_FIRMWARE_MINOR 0
#define POTLUCK_MAX_FIRMWARE_MICRO 1
#define POTLUCK_MAX_FIRMWARE_TEXT  "23.0.1"

/* True when firmware `hos` (as hosversionGet() returns it) is newer than supported. */
static inline int potluckFirmwareIsNewer(unsigned int hos) {
    const unsigned int max = ((POTLUCK_MAX_FIRMWARE_MAJOR & 0xFF) << 16) |
                             ((POTLUCK_MAX_FIRMWARE_MINOR & 0xFF) << 8) |
                             (POTLUCK_MAX_FIRMWARE_MICRO & 0xFF);
    return (hos & 0x00FFFFFF) > max;
}
