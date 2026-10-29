#pragma once

/*
 * SD card layout shared by the sysmodule, the overlay and the tests.
 *
 * The module, its settings and game data live in the module's own folder.
 * Logs go to config/potluck/, next to the installer's config/potluck_installer/,
 * and the overlay file sits in switch/.overlays/ where overlay menus read it.
 * Removing PotLuck from the installer deletes all three.
 */

#define POTLUCK_PROGRAM_ID      0x4200000000504C4BULL
#define POTLUCK_TITLE_ID_STR    "4200000000504C4B"

#define POTLUCK_MODULE_DIR      "sdmc:/atmosphere/contents/" POTLUCK_TITLE_ID_STR
#define POTLUCK_CONFIG_PATH     POTLUCK_MODULE_DIR "/config.ini"
#define POTLUCK_LOG_DIR         "sdmc:/config/potluck"
#define POTLUCK_LOG_PATH        POTLUCK_LOG_DIR "/potluck.log"
#define POTLUCK_GAMELIST_PATH   POTLUCK_MODULE_DIR "/gamelist.txt"
#define POTLUCK_GAME_NAMES_PATH POTLUCK_MODULE_DIR "/game_names.tsv"
#define POTLUCK_BOOT_FLAG_DIR   POTLUCK_MODULE_DIR "/flags"
#define POTLUCK_BOOT_FLAG_PATH  POTLUCK_BOOT_FLAG_DIR "/boot2.flag"
#define POTLUCK_OVERLAY_TRACE_PATH POTLUCK_LOG_DIR "/overlay-startup.log"

/* Where settings lived before the rename; the module migrates them once. */
#define POTLUCK_LEGACY_CONFIG_PATH "sdmc:/config/ryu_ldn_nx/config.ini"
