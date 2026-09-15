#!/usr/bin/env bash
# Run a command with the STM32Cube VS Code bundle toolchain on PATH.
# Usage: tools/cube-env.sh cmake --build --preset Debug
set -euo pipefail

CUBE_BUNDLE_PATH="${CUBE_BUNDLE_PATH:-$HOME/.local/share/stm32cube/bundles}"
export CUBE_BUNDLE_PATH

# newest version dir of a bundle
newest() { ls -d "$CUBE_BUNDLE_PATH/$1"/*/ 2>/dev/null | sort -V | tail -1; }

for b in gnu-tools-for-stm32 gnu-gdb-for-stm32 cmake ninja programmer stlink-gdbserver stlink-server; do
  d="$(newest "$b")"; [ -n "$d" ] && [ -d "${d}bin" ] && PATH="${d}bin:$PATH"
done
export PATH

[ $# -eq 0 ] && { echo "PATH set. usage: $0 <cmd> [args]"; exec "${SHELL:-bash}"; }
exec "$@"
