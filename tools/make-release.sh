#!/bin/sh
# Builds build/release/AmiRocket-<version>.lha from the Amiga build.
set -e
VERSION=${1:-0.1}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/release"
rm -rf "$OUT/AmiRocket"
mkdir -p "$OUT/AmiRocket"
cp "$ROOT/build/amiga/AmiRocket" "$OUT/AmiRocket/"
cp "$ROOT/docs/AmiRocket.readme" "$OUT/AmiRocket/ReadMe"
LHA=$(command -v lha || true)
[ -z "$LHA" ] && [ -x "$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/lha" ] && LHA="$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/lha"
if [ -z "$LHA" ]; then
    echo "lha not found; release folder is in $OUT/AmiRocket" >&2
    exit 0
fi
cd "$OUT"
rm -f "AmiRocket-$VERSION.lha"
"$LHA" ao5 "AmiRocket-$VERSION.lha" AmiRocket
echo "$OUT/AmiRocket-$VERSION.lha"
