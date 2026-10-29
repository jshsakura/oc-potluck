#!/usr/bin/env bash
# Fail when the firmware PotLuck says it supports (common/potluck_firmware.h)
# differs from the newest one its Atmosphere libraries know.
set -euo pipefail

cd "$(dirname "$0")/../.."
target=sysmodule/Atmosphere-libs/libvapours/include/vapours/ams/ams_target_firmware.h
current=$(sed -n 's/^#define ATMOSPHERE_TARGET_FIRMWARE_CURRENT ATMOSPHERE_TARGET_FIRMWARE_\([0-9_]*\)$/\1/p' "$target")
[[ -n $current ]] || { echo "Cannot read ATMOSPHERE_TARGET_FIRMWARE_CURRENT from $target" >&2; exit 1; }
libs=${current//_/.}

claimed=$(sed -n 's/^#define POTLUCK_MAX_FIRMWARE_TEXT  "\(.*\)"$/\1/p' common/potluck_firmware.h)
if [[ $claimed != "$libs" ]]; then
    echo "common/potluck_firmware.h claims firmware $claimed, Atmosphere-libs knows $libs." >&2
    echo "Update POTLUCK_MAX_FIRMWARE_* to match." >&2
    exit 1
fi
echo "Supported firmware: up to $libs"
