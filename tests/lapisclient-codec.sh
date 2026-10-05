#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -DLAPISCLIENT -Iinclude tests/lapisclient_codec.c src/lapisclient_codec.c -lm -o .tests/lapisclient-codec
.tests/lapisclient-codec
