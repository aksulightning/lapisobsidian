#!/bin/sh
# Debian/Ubuntu server build with the local LapisCube browser bundle enabled.
set -eu

usage() {
  cat <<'HELP'
Usage: ./build-debian.sh [--debug] [--prebuilt DIR]

Build the server and its browser bundle on Debian-based Linux distributions.
HTTP/WebSocket hosting is always compiled in. Output: lapis-obsidian and webclient/
in the repository. The build does not install packages or start the server.

  --debug          Enable ASan/UBSan (also available as DEBUG=1)
  --prebuilt DIR   Validate and copy an existing webclient/ bundle instead of
                   building it; also available as LAPIS_WEBCLIENT_PREBUILT
  --help, -h       Show this help

Prerequisites: build-essential, bash, git, python3, make and patch.
For a fresh browser build, initialize the pinned engine submodule and activate
Emscripten so emcc is on PATH; see docs/build-presets.md. Prebuilt mode needs
no Emscripten. Relative CC and prebuilt paths use your invocation directory.
CC selects one compiler executable (default: gcc); DEBUG defaults to 0.
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM and LAPIS_ENABLE_EXPERIMENTAL_AUTH default
to 0; use build-experiments-debian.sh to default both to 1. Authentication needs
libssl-dev, libcurl4-openssl-dev and ca-certificates and prevents LapisCube login.
HELP
}
fail() { printf '%s\n' "Error: $*" >&2; exit 1; }

prebuilt=${LAPIS_WEBCLIENT_PREBUILT:-}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --debug) DEBUG=1; export DEBUG; shift ;;
    --prebuilt)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "--prebuilt requires a directory"
      prebuilt=$2; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1 (see --help)" ;;
  esac
done
[ "$(uname -s)" = Linux ] && [ -f /etc/debian_version ] ||
  fail "Run this script on Debian-based Linux; see build-alpine.sh for Alpine"
for feature in LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM LAPIS_ENABLE_EXPERIMENTAL_AUTH; do
  case "$feature" in
    LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM) value=${LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM:-0} ;;
    LAPIS_ENABLE_EXPERIMENTAL_AUTH) value=${LAPIS_ENABLE_EXPERIMENTAL_AUTH:-0} ;;
  esac
  case "$value" in 0|1) ;; *) fail "$feature must be 0 or 1" ;; esac
done

# build.sh changes to the repository before compiling/installing. Preserve the
# meaning of relative paths supplied by callers outside the repository.
CC=$(command -v "${CC:-gcc}") || fail "C compiler missing; install build-essential"
case "$CC" in /*) ;; *) CC=$PWD/$CC ;; esac
case "$prebuilt" in ''|/*) ;; *) prebuilt=$PWD/$prebuilt ;; esac
for tool in bash python3; do
  command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; see --help"
done
if [ -z "$prebuilt" ]; then
  for tool in emcc make git patch; do
    command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; activate Emscripten or use --prebuilt (see --help)"
  done
fi
LAPIS_ENABLE_WEBCLIENT=1
LAPIS_WEBCLIENT_PREBUILT=$prebuilt
export CC LAPIS_ENABLE_WEBCLIENT LAPIS_WEBCLIENT_PREBUILT
exec "$(dirname "$0")/build.sh"
