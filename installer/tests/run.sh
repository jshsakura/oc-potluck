#!/bin/bash
# Host tests: pure rules, then install/switch/migrate/remove against a throwaway "sdmc:" folder.
# Usage: tests/run.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
g++ -std=c++17 -Wall -Wextra -o "$work/logic" "$here/test_logic.cpp" && "$work/logic"
g++ -std=c++17 -Wall -Wextra -o "$work/touch" "$here/test_touch.cpp" && "$work/touch"
python3 -I - "$work" <<'PY'
import sys, zipfile
work = sys.argv[1]
C = "atmosphere/contents/4200000000504C4B/"
with zipfile.ZipFile(work + "/release.zip", "w") as z:
    z.writestr(C + "exefs.nsp", "new build")
    z.writestr(C + "main.npdm", "npdm")
    z.writestr(C + "toolbox.json", '{"name": "potluck", "tid": "0x4200000000504C4B"}')
    z.writestr(C + "flags/boot2.flag", "")
    z.writestr("switch/.overlays/potluck.ovl", "ovl")
    z.writestr("../evil.txt", "x")
    z.writestr("switch/hbmenu.nro", "x")
    z.writestr("atmosphere/contents/0100000000001000/exefs.nsp", "x")
    z.writestr("atmosphere/contents/4200000000000010/exefs.nsp", "overwrites ldn_mitm")
with zipfile.ZipFile(work + "/bad.zip", "w") as z:
    z.writestr(C + "config.ini", "x")
import os
fx = work + "/fixtures"
os.makedirs(fx)
def release(path, flag_first=False, method=zipfile.ZIP_DEFLATED, big=0):
    with zipfile.ZipFile(path, "w", method) as z:
        if flag_first:
            z.writestr(C + "flags/boot2.flag", "")
        z.writestr(C + "exefs.nsp", "unique-payload")
        z.writestr(C + "main.npdm", "npdm")
        z.writestr(C + "toolbox.json", '{"name": "potluck"}')
        z.writestr("switch/.overlays/potluck.ovl", "ovl")
        if big:
            z.writestr(C + "big.bin", bytes(big))
        if not flag_first:
            z.writestr(C + "flags/boot2.flag", "")
release(fx + "/release.zip")
release(fx + "/flagfirst.zip", flag_first=True)
release(fx + "/big.zip", big=3 << 20)
# Re-review fixtures: a deep path, a name ending in .bak, case tricks on a case-insensitive card, too many entries.
def extra(path, names):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(C + "exefs.nsp", "unique-payload")
        z.writestr(C + "main.npdm", "npdm")
        z.writestr(C + "toolbox.json", '{"name": "potluck"}')
        z.writestr("switch/.overlays/potluck.ovl", "ovl")
        for n in names:
            z.writestr(C + n, "x")
extra(fx + "/deep.zip", ["data/sub/x.bin", "a/b/c/d/e/f.bin"])
extra(fx + "/bakname.zip", ["main.npdm.bak", "exefs.nsp.PART"])
extra(fx + "/case.zip", ["Flags/boot2.flag", "FLAGS/x", "Config.ini", "EXEFS.NSP"])
extra(fx + "/many.zip", ["g/entry%05d.txt" % i for i in range(1500)])
# A stored entry whose payload no longer matches its CRC.
release(fx + "/badcrc.zip", method=zipfile.ZIP_STORED)
data = bytearray(open(fx + "/badcrc.zip", "rb").read())
at = data.index(b"unique-payload")
data[at] = ord("X")
open(fx + "/badcrc.zip", "wb").write(data)
PY
src="$here/../source"
sources=("$src/install.cpp" "$src/legacy.cpp" "$src/settings.cpp" "$src/sdfs.cpp" "$src/games.cpp")
g++ -std=c++17 -Wall -Wextra -o "$work/install" "$here/test_install.cpp" "${sources[@]}" -lminizip -lz
(cd "$work" && mkdir -p "sdmc:" && mv release.zip bad.zip "sdmc:/" && ./install release.zip bad.zip)
# The same code with failures injected into rename, unlink and fopen.
g++ -std=c++17 -Wall -Wextra -o "$work/failures" "$here/test_failures.cpp" "${sources[@]}" \
    -Wl,--wrap=rename,--wrap=unlink,--wrap=fopen -lminizip -lz
mkdir "$work/f" && (cd "$work/f" && ../failures "$work/fixtures")
# Every Hangul syllable the UI can show must be in the bundled font (it is a subset).
python3 - "$here/../romfs/fonts/CookingFoilSans-Regular.otf" "$here"/../source/*.cpp <<'PY'
import re, sys
try:
    from fontTools.ttLib import TTFont
except ImportError:
    print("fontTools missing: font coverage not checked"); sys.exit(0)
cm = TTFont(sys.argv[1]).getBestCmap()
used = set()
for path in sys.argv[2:]:
    used |= set(re.findall(r'[ㄱ-ㆎ가-힣]', open(path, encoding="utf8").read()))
missing = sorted(c for c in used if ord(c) not in cm)
if missing:
    print("FAIL font lacks:", "".join(missing)); sys.exit(1)
print("Font covers all", len(used), "Hangul characters")
PY
