#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
mkdir -p build
if [ ! -f "build/Makefile" ]; then
    cmake -B build -S .
fi
if command -v lowprio-build >/dev/null 2>&1; then
    exec lowprio-build cmake --build build -j2 "$@"
else
    exec systemd-run --scope --quiet -p MemoryMax=1200M -p CPUQuota=300% nice -n 15 ionice -c3 cmake --build build -j2 "$@"
fi
