#pragma once

/*
 * Private room passphrase as the relay server expects it: a fixed prefix
 * followed by 8 lowercase hex digits. The prefix is a wire-protocol constant
 * the server matches byte for byte, so it is spelled here once and never shown
 * in the UI; users only see and type the 8 digits.
 */

#define POTLUCK_ROOM_PREFIX      "Ryujinx-"
#define POTLUCK_ROOM_PREFIX_LEN  8
#define POTLUCK_ROOM_HEX_LEN     8
#define POTLUCK_ROOM_CODE_LEN    (POTLUCK_ROOM_PREFIX_LEN + POTLUCK_ROOM_HEX_LEN)
