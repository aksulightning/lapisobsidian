#!/usr/bin/env bash
# Check environment-to-compiler flags, without requiring an Alpine toolchain.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tests
work=$(mktemp -d "$PWD/.tests/build-features.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/source"
cp -R build.sh build-alpine.sh src include "$work/source/"
cat > "$work/compiler" <<'SH'
#!/bin/sh
set -eu
if [ "${1:-}" = -dumpmachine ]; then echo x86_64-linux-musl; exit 0; fi
world=0 tree=0 source=0 output=
while [ "$#" -gt 0 ]; do
  case "$1" in
    -DLAPIS_WORLD_EDIT=1) world=1 ;;
    -DLAPIS_TREE_CHOPPER=1) tree=1 ;;
    src/*.c) source=1 ;;
    -o) shift; output=$1 ;;
  esac
  shift
done
if [ "$source" = 1 ]; then
  [ "$world" = "$EXPECT_WORLD" ] && [ "$tree" = "$EXPECT_TREE" ] || exit 99
  echo checked >> "$BUILD_CHECK_LOG"
fi
: > "$output"
SH
cat > "$work/readelf" <<'SH'
#!/bin/sh
echo '[Requesting program interpreter: /lib/ld-musl-x86_64.so.1]'
SH
chmod +x "$work/compiler" "$work/readelf"
export CC="$work/compiler" READELF="$work/readelf" BUILD_CHECK_LOG="$work/checks"
for script in build.sh build-alpine.sh; do
  for world in unset '' 0 1 true 01; do
    for tree in unset '' 0 1 true 01; do
      (
        unset LAPIS_WORLD_EDIT LAPIS_TREE_CHOPPER
        [[ "$world" == unset ]] || export LAPIS_WORLD_EDIT="$world"
        [[ "$tree" == unset ]] || export LAPIS_TREE_CHOPPER="$tree"
        export EXPECT_WORLD=0 EXPECT_TREE=0
        [[ "$world" != 1 ]] || export EXPECT_WORLD=1
        [[ "$tree" != 1 ]] || export EXPECT_TREE=1
        "$work/source/$script" >/dev/null
      )
    done
  done
done
[[ $(wc -l < "$work/checks") == 72 ]]
echo 'build feature flags: Bash and Alpine scripts enable only exact value 1 (72 cases) passed'
