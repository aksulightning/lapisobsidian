#!/bin/sh
# Experimental Debian/Ubuntu server and browser-bundle preset.
set -eu

case "${1:-}" in
  --help|-h)
    cat <<'HELP'
Usage: ./build-experiments-debian.sh [build-debian.sh options]

Enable HTTP/WebSocket hosting and default both experimental features to 1.
Set LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=0 or LAPIS_ENABLE_EXPERIMENTAL_AUTH=0
to disable either feature. Authentication needs libssl-dev,
libcurl4-openssl-dev and ca-certificates; LapisCube cannot log in with it.
See docs/build-presets.md for prerequisites and browser-compatible builds.

Underlying Debian options:
HELP
    exec "$(dirname "$0")/build-debian.sh" --help ;;
esac
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=${LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM:-1}
LAPIS_ENABLE_EXPERIMENTAL_AUTH=${LAPIS_ENABLE_EXPERIMENTAL_AUTH:-1}
export LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM LAPIS_ENABLE_EXPERIMENTAL_AUTH
if [ "$LAPIS_ENABLE_EXPERIMENTAL_AUTH" = 1 ]; then
  printf '%s\n' 'Experimental authentication is enabled: LapisCube cannot join; set LAPIS_ENABLE_EXPERIMENTAL_AUTH=0 for browser play.' >&2
fi
exec "$(dirname "$0")/build-debian.sh" "$@"
