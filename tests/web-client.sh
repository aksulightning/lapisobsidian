#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests/web-assets
sh tools/embed-web.sh > .tests/web-assets/web_assets.h
flags=(-std=c11 -O2 -Wall -Wextra -Wconversion -Wshadow -Werror -DLAPIS_OBSIDIAN_WEB_CLIENT=1 -Iinclude -I.tests/web-assets)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
fi
"${CC:-gcc}" "${flags[@]}" tests/web_client.c tests/sanitizer.c src/web_client.c src/packet_input.c -o .tests/web-client
.tests/web-client
