#!/usr/bin/env bash
set -euo pipefail

slice=potluck-ci-tests.slice
limit=4294967296

if [[ ${1:-} == --setup ]]; then
    sudo tee "/run/systemd/system/$slice" >/dev/null <<EOF
[Unit]
Description=Aggregate memory budget for PotLuck tests
[Slice]
MemoryAccounting=yes
MemoryMax=$limit
MemorySwapMax=0
EOF
    sudo systemctl daemon-reload
    sudo systemctl start "$slice"
    test "$(sudo systemctl show "$slice" --property=MemoryMax --value)" = "$limit"
    exit
fi

sudo systemd-run --quiet --wait --pipe --collect \
    --uid="$(id -u)" --gid="$(id -g)" \
    --setenv="PATH=$PATH" \
    --property="Slice=$slice" --property="WorkingDirectory=$PWD" \
    /bin/bash -c '
        set -euo pipefail
        cg_path=$(cut -d: -f3 /proc/self/cgroup)
        parent=/sys/fs/cgroup/potluck.slice/potluck-ci.slice/potluck-ci-tests.slice
        case "/sys/fs/cgroup$cg_path/" in "$parent/"*) ;; *) exit 98 ;; esac
        test "$(cat "$parent/memory.max")" = 4294967296
        test "$(cat "$parent/memory.swap.max")" = 0
        printf "Verified shared test cgroup memory.max=4294967296\n"
        exec "$@"
    ' bash "$@"
