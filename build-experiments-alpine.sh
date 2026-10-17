#!/bin/sh
# Experimental Alpine server preset; retain build-alpine.sh's native/cross ABI checks.
set -eu

case "${1:-}" in
  --help|-h)
    cat <<'HELP'
Usage: ./build-experiments-alpine.sh [build-alpine.sh options]

Enable HTTP/WebSocket hosting and default both experimental features to 1.
Set LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=0 or LAPIS_ENABLE_EXPERIMENTAL_AUTH=0
to disable either feature. Authentication needs openssl-dev, curl-dev and
ca-certificates; it cannot use --static and LapisCube cannot log in with it.
This compiles only the server. Build/install browser assets separately as
described in docs/build-presets.md. No packages are installed or downloaded.

Underlying server options:
HELP
    exec "$(dirname "$0")/build-alpine.sh" --help ;;
esac
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=${LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM:-1}
LAPIS_ENABLE_EXPERIMENTAL_AUTH=${LAPIS_ENABLE_EXPERIMENTAL_AUTH:-1}
export LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM LAPIS_ENABLE_EXPERIMENTAL_AUTH
if [ "$LAPIS_ENABLE_EXPERIMENTAL_AUTH" = 1 ]; then
  printf '%s\n' 'Experimental authentication is enabled: LapisCube cannot join; set LAPIS_ENABLE_EXPERIMENTAL_AUTH=0 for browser play.' >&2
fi
exec "$(dirname "$0")/build-alpine.sh" --webclient "$@"
