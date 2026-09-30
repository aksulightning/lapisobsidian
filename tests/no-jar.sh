#!/usr/bin/env bash
# Build a source-only copy with a PATH containing only the C toolchain and shell tools.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "$PWD/.tests/clean.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/source" "$tmp/bin"
cp -R src include build.sh "$tmp/source/"
for tool in bash dirname uname gcc as ld; do
  ln -s "$(command -v "$tool")" "$tmp/bin/$tool"
done
(cd "$tmp/source" && PATH="$tmp/bin" ./build.sh)
test -x "$tmp/source/lapis-obsidian"
echo 'source-only build without Java, JARs, extraction or registry generation: passed'
