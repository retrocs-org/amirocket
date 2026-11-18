#!/bin/sh
# Compatibility entry point: release artifacts now use the staged Beam68 workflow.
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -gt 0 ] && [ "$1" != "$(cat VERSION)" ]; then
    echo "The release version must match VERSION" >&2
    exit 2
fi
exec python3 -B scripts/release.py build
