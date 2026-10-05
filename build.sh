#!/usr/bin/env bash

set -euo pipefail
cd "$(dirname "$0")"

# Check for registries before attempting to compile, prevents confusion
if [ ! -f "include/registries.h" ]; then
  echo "Error: 'include/registries.h' is missing."
  echo "Restore the checked-in registry snapshot from this repository."
  exit 1
fi

# Figure out executable suffix (for MSYS compilation)
case "$OSTYPE" in
  msys*|cygwin*|win32*) exe=".exe" ;;
  *) exe="" ;;
esac

# mingw64-specific linker options
windows_linker=""
unameOut="$(uname -s)"
case "$unameOut" in
  MINGW64_NT*)
    windows_linker="-static -lws2_32 -pthread"
    ;;
esac

# Default compiler
compiler="${CC:-gcc}"

# Handle arguments for windows 9x build
for arg in "$@"; do
  case $arg in
    --9x)
      if [[ "$unameOut" == MINGW64_NT* ]]; then
        compiler="/opt/bin/i686-w64-mingw32-gcc"
        windows_linker="$windows_linker -Wl,--subsystem,console:4"
      else
        echo "Error: Compiling for Windows 9x is only supported when running under the MinGW64 shell."
        exit 1
      fi
      ;;
  esac
done

ws_flags=()
# Native WebSocket support on POSIX desktop; legacy embedded/Windows builds remain available.
if [[ "${LAPISCLIENT:-1}" == 1 && "$unameOut" != MINGW* ]]; then
  ws_flags=(-DLAPISCLIENT -lcrypto)
fi
flags=(-O2 -Wall -Wextra)
if [[ "${DEBUG:-0}" == 1 ]]; then
  flags=(-O1 -g -Wall -Wextra -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
# Disable contraction so density vectors do not depend on FMA availability.
"$compiler" src/*.c "${flags[@]}" -ffp-contract=off -Iinclude -o "lapis-obsidian$exe" $windows_linker -lm "${ws_flags[@]}"
