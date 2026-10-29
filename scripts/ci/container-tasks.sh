#!/usr/bin/env bash
# Shared container build/clean/test runner for local Docker Compose and GitHub Actions CI.
set -euo pipefail

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITARM="${DEVKITARM:-${DEVKITPRO}/devkitARM}"
DEVKITA64="${DEVKITA64:-${DEVKITPRO}/devkitA64}"
export DEVKITPRO DEVKITARM DEVKITA64
export PATH="${DEVKITPRO}/tools/bin:${DEVKITA64}/bin:${PATH}"

PREBUILT_LIBSTRATOSPHERE="/opt/potluck/libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a"
ATMOSPHERE_LIBS_DIR="${WORKSPACE_DIR}/sysmodule/Atmosphere-libs"
TARGET_LIB_DIR="${ATMOSPHERE_LIBS_DIR}/libstratosphere/lib/nintendo_nx_arm64_armv8a/release"
MEMORY_LIMIT_BYTES=4294967296

ensure_devkitpro_tool_wrappers() {
    # Wrap devkitPro packaging tools so LD_PRELOAD never interferes with foreign-arch
    # or containerized execution, and stage npdmtool's input JSON to /tmp to avoid
    # bind-mount 64-bit inode / stat issues.
    local tool real_tool
    for tool in npdmtool elf2nso build_pfs0 nacptool elf2nro; do
        real_tool="${DEVKITPRO}/tools/bin/${tool}.real"
        if [ -f "${DEVKITPRO}/tools/bin/${tool}" ] && [ ! -f "${real_tool}" ]; then
            mv "${DEVKITPRO}/tools/bin/${tool}" "${real_tool}"
        fi
    done

    if [ -f "${DEVKITPRO}/tools/bin/npdmtool.real" ]; then
        cat > "${DEVKITPRO}/tools/bin/npdmtool" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
in_json="$1"
out_npdm="$2"
tmp_json="/tmp/potluck_app_$$.json"
cp "$in_json" "$tmp_json"
trap 'rm -f "$tmp_json"' EXIT
env -u LD_PRELOAD /opt/devkitpro/tools/bin/npdmtool.real "$tmp_json" "$out_npdm"
EOF
        chmod +x "${DEVKITPRO}/tools/bin/npdmtool"
    fi

    for tool in elf2nso build_pfs0 nacptool elf2nro; do
        if [ -f "${DEVKITPRO}/tools/bin/${tool}.real" ]; then
            cat > "${DEVKITPRO}/tools/bin/${tool}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec env -u LD_PRELOAD /opt/devkitpro/tools/bin/${tool}.real "\$@"
EOF
            chmod +x "${DEVKITPRO}/tools/bin/${tool}"
        fi
    done
}

verify_real_submodules_and_prebuilt() {
    local strat_mk="${ATMOSPHERE_LIBS_DIR}/config/templates/stratosphere.mk"
    local strat_hpp="${ATMOSPHERE_LIBS_DIR}/libstratosphere/include/stratosphere.hpp"
    local vapours_hpp="${ATMOSPHERE_LIBS_DIR}/libvapours/include/vapours.hpp"
    local tesla_hpp="${WORKSPACE_DIR}/overlay/libultrahand/libtesla/include/tesla.hpp"

    if [ ! -f "${strat_mk}" ] || [ ! -f "${strat_hpp}" ] || [ ! -f "${vapours_hpp}" ]; then
        echo "[build] ERROR: sysmodule/Atmosphere-libs submodule is missing or incomplete." >&2
        echo "[build] Run: git submodule update --init --recursive" >&2
        exit 1
    fi

    # Reject fake/stub stratosphere.hpp headers
    if [ "$(wc -l < "${strat_hpp}")" -lt 15 ]; then
        echo "[build] ERROR: ${strat_hpp} appears to be a stub header rather than real Atmosphere-libs." >&2
        exit 1
    fi

    if [ ! -f "${tesla_hpp}" ]; then
        echo "[build] ERROR: overlay/libultrahand submodule is missing (${tesla_hpp} not found)." >&2
        echo "[build] Run: git submodule update --init --recursive" >&2
        exit 1
    fi

    if [ ! -f "${PREBUILT_LIBSTRATOSPHERE}" ]; then
        echo "[build] ERROR: Pre-built libstratosphere.a not found at ${PREBUILT_LIBSTRATOSPHERE}" >&2
        exit 1
    fi

    mkdir -p "${TARGET_LIB_DIR}"
    ln -sf "${PREBUILT_LIBSTRATOSPHERE}" "${TARGET_LIB_DIR}/libstratosphere.a"
}

verify_cgroup_memory_limit() {
    # Inspect ONLY the current process's own cgroup v2 path and its direct ancestor
    # directories up to /sys/fs/cgroup. Never inspect sibling/unrelated slices.
    local cg_rel cur val found_max="" found_swap="" found_path=""
    cg_rel="$(awk -F: '$1 == "0" {print $3}' /proc/self/cgroup 2>/dev/null || true)"
    if [ -z "${cg_rel}" ]; then
        echo "[test] ERROR: Unable to determine cgroup v2 path from /proc/self/cgroup." >&2
        exit 97
    fi

    cur="$(realpath -m "/sys/fs/cgroup${cg_rel%/}")"
    case "${cur}" in
        /sys/fs/cgroup|/sys/fs/cgroup/*) ;;
        *) echo "[test] ERROR: Invalid cgroup path: ${cg_rel}" >&2; exit 97 ;;
    esac
    if [ ! -d "${cur}" ]; then
        echo "[test] ERROR: Current process cgroup is not visible: ${cur}" >&2
        exit 97
    fi
    while true; do
        if [ -f "${cur}/memory.max" ]; then
            val="$(cat "${cur}/memory.max")"
            if [[ "${val}" =~ ^[0-9]+$ ]]; then
                if [ -z "${found_max}" ] || [ "${val}" -lt "${found_max}" ]; then
                    found_max="${val}"
                    found_path="${cur}"
                fi
            fi
        fi
        if [ -f "${cur}/memory.swap.max" ]; then
            val="$(cat "${cur}/memory.swap.max")"
            if [[ "${val}" =~ ^[0-9]+$ ]] &&
               { [ -z "${found_swap}" ] || [ "${val}" -lt "${found_swap}" ]; }; then
                found_swap="${val}"
            fi
        fi
        if [ "${cur}" = "/sys/fs/cgroup" ] || [ "${cur}" = "/" ] || [ -z "${cur}" ]; then
            break
        fi
        cur="$(dirname "${cur}")"
    done

    if [ -z "${found_max}" ]; then
        echo "[test] ERROR: No numeric cgroup memory.max limit is active on current process or its ancestors (/proc/self/cgroup=${cg_rel})." >&2
        exit 97
    fi

    if [ "${found_max}" -gt "${MEMORY_LIMIT_BYTES}" ]; then
        echo "[test] ERROR: Effective cgroup memory.max (${found_max} at ${found_path}) exceeds 4 GiB (${MEMORY_LIMIT_BYTES})." >&2
        exit 97
    fi

    if [ -n "${found_swap}" ] && [ "${found_swap}" != "0" ]; then
        echo "[test] ERROR: Effective cgroup memory.swap.max (${found_swap} at ${found_path}) must be 0." >&2
        exit 97
    fi

    echo "[test] Verified live process cgroup limit at ${found_path} (self=${cg_rel}): memory.max=${found_max} memory.swap.max=${found_swap:-n/a}"
}

cmd_build() {
    echo "=========================================="
    echo "Building potluck (sysmodule + overlay + dist)"
    echo "Container arch: $(uname -m)"
    echo "=========================================="

    verify_real_submodules_and_prebuilt
    ensure_devkitpro_tool_wrappers
    git config --global --add safe.directory "${WORKSPACE_DIR}"
    git config --global --add safe.directory "${ATMOSPHERE_LIBS_DIR}"
    git config --global --add safe.directory "${WORKSPACE_DIR}/overlay/libultrahand"

    echo "[1/3] Building Overlay..."
    make -C "${WORKSPACE_DIR}/overlay" -j4 all
    test -s "${WORKSPACE_DIR}/overlay/potluck.ovl"

    echo "[2/3] Building sysmodule..."
    make -C "${WORKSPACE_DIR}/sysmodule" -j4 all
    test -s "${WORKSPACE_DIR}/sysmodule/potluck.elf"
    test -s "${WORKSPACE_DIR}/sysmodule/potluck.nso"
    test -s "${WORKSPACE_DIR}/sysmodule/potluck.npdm"
    test -s "${WORKSPACE_DIR}/sysmodule/potluck.nsp"

    echo "[3/3] Packaging SD card distribution and release ZIP..."
    make -C "${WORKSPACE_DIR}/sysmodule" dist
    test -s "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/exefs.nsp"
    test -s "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/main.npdm"
    test -s "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/toolbox.json"
    test -f "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/flags/boot2.flag"
    test -s "${WORKSPACE_DIR}/output/switch/.overlays/potluck.ovl"
    test -s "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/game_names.tsv"
    test -s "${WORKSPACE_DIR}/output/atmosphere/contents/4200000000504C4B/gamelist.txt"
    test -s "${WORKSPACE_DIR}/potluck-release.zip"

    echo "=========================================="
    echo "Build complete: potluck-release.zip & output/ verified"
    echo "=========================================="
}

cmd_clean() {
    echo "[clean] Cleaning sysmodule..."
    make -C "${WORKSPACE_DIR}/sysmodule" clean
    echo "[clean] Cleaning overlay..."
    make -C "${WORKSPACE_DIR}/overlay" clean
    echo "[clean] Cleaning tests..."
    make -C "${WORKSPACE_DIR}/tests" clean
    echo "[clean] Removing output and release archive..."
    rm -rf "${WORKSPACE_DIR}/output"
    rm -f "${WORKSPACE_DIR}/potluck-release.zip"
    if [ -L "${TARGET_LIB_DIR}/libstratosphere.a" ]; then
        rm "${TARGET_LIB_DIR}/libstratosphere.a"
    fi
    echo "[clean] Done"
}

cmd_test() {
    verify_cgroup_memory_limit
    echo "[test] Running host unit tests serially (-j1)..."
    make -C "${WORKSPACE_DIR}/tests" -j1 clean
    if [ "$#" -gt 0 ]; then
        make -C "${WORKSPACE_DIR}/tests" "$@" -j1
    else
        make -C "${WORKSPACE_DIR}/tests" -j1 test
    fi
}

case "${1:-}" in
    build)
        shift
        cmd_build "$@"
        ;;
    clean)
        shift
        cmd_clean "$@"
        ;;
    test)
        shift
        cmd_test "$@"
        ;;
    *)
        echo "Usage: $0 {build|clean|test [make-args...]}" >&2
        exit 2
        ;;
esac
