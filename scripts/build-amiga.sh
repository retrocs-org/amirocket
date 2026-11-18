#!/bin/sh
# Force a complete build so changed VERSION/compiler flags cannot reuse old objects.
set -eu
cd "$(dirname "$0")/.."
cc=${AMIGA_CC:-${AMIROCKET_CC:-$HOME/Development/tools/amiga/m68k-amigaos-gcc-16.2/bin/m68k-amigaos-gcc}}
cpu=${AMIGA_CPU:--m68020}
[ "$cpu" = "-m68020" ] || { echo "Release target is fixed at -m68020" >&2; exit 2; }
mkdir -p work
make -B amiga AMIGA_CC="$cc" AMIGA_CPU="$cpu" BUILDS_DIR="$PWD/work/native"
python3 -B scripts/make-aminet-release.py --runtime-readme build/amiga/AmiRocket.readme
python3 -B scripts/build-provenance.py record --compiler "$cc" --flags="$(make -s print-amiga-cflags AMIGA_CC="$cc" AMIGA_CPU="$cpu")"
