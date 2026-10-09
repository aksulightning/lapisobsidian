#!/bin/sh
# Native Alpine builds, or cross-builds with an existing Linux/musl toolchain.
set -eu
LC_ALL=C
export LC_ALL

usage() {
  cat <<'HELP'
Usage: ./build-alpine.sh [--arch ARCH] [--static] [--debug] [--output FILE]

ARCH: riscv64, aarch64 (arm64), x86_64 (x64 or amd64)
Default: compiler target architecture; output is <repository>/lapis-obsidian.
Install prerequisites on Alpine: apk add --no-cache build-base

CC selects one compiler executable (default: cc).
READELF selects an ELF inspection executable (default: readelf).
--arch checks the compiler target; it does not install/select a cross compiler.
--static links musl statically; --debug enables ASan/UBSan (not with --static).
Relative --output paths are resolved from your invocation directory.
The script downloads nothing and never executes the target binary.
LAPIS_OBSIDIAN_WEB_CLIENT=1 embeds the optional HTML5 client (requires awk/od/tr).
HELP
}
fail() { printf '%s\n' "Error: $*" >&2; exit 1; }
normalize() {
  case "$1" in
    riscv64) printf '%s\n' riscv64 ;;
    aarch64|arm64) printf '%s\n' aarch64 ;;
    x86_64|x64|amd64) printf '%s\n' x86_64 ;;
    *) return 1 ;;
  esac
}
arch=
static=0
debug=0
output=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --arch|--output)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 requires a value"
      case "$1" in
        --arch) arch=$(normalize "$2") || fail "Unsupported architecture: $2" ;;
        --output) output=$2 ;;
      esac
      shift 2 ;;
    --static) static=1; shift ;;
    --debug) debug=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1 (see --help)" ;;
  esac
done
[ "$static$debug" != 11 ] || fail "--static and --debug cannot be combined"

# Resolve executable paths before changing directories, including relative CC.
compiler=$(command -v "${CC:-cc}") || fail "C compiler missing; install build-base on Alpine"
inspector=$(command -v "${READELF:-readelf}") || fail "readelf missing; install binutils on Alpine"
case "$compiler" in /*) ;; *) compiler=$PWD/$compiler ;; esac
case "$inspector" in /*) ;; *) inspector=$PWD/$inspector ;; esac
if [ -n "$output" ]; then
  case "$output" in /*) ;; *) output=$PWD/$output ;; esac
fi
cd "$(dirname "$0")"
project=$(pwd -P)
[ -n "$output" ] || output=$project/lapis-obsidian
[ ! -d "$output" ] || fail "Output names a directory: $output"
[ -f include/registries.h ] && [ -f src/registries.c ] || fail "Restore the checked-in C registry snapshot"

triple=$("$compiler" -dumpmachine) || fail "Cannot query compiler target"
case "$triple" in *-linux-*) ;; *) fail "A Linux/musl compiler is required (got $triple)" ;; esac
target=$(normalize "${triple%%-*}") || fail "Unsupported compiler target: $triple"
[ -n "$arch" ] || arch=$target
[ "$arch" = "$target" ] || fail "Requested $arch, but CC targets $target; supply a matching musl compiler"

work=$(mktemp -d "$project/.alpine-build.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Check actual libc/ABI rather than trusting names such as musl-gcc or CC wrappers.
cat > "$work/probe.c" <<'C'
#include <stdio.h>
_Static_assert(sizeof(void *) == 8, "A 64-bit compiler ABI is required");
int main(void) { return puts("Lapis Obsidian build probe") < 0; }
C
"$compiler" "$work/probe.c" -o "$work/probe" || fail "Compiler/linker probe failed"
"$inspector" -l "$work/probe" > "$work/program-headers"
grep -F "[Requesting program interpreter: /lib/ld-musl-$arch.so.1]" "$work/program-headers" >/dev/null ||
  fail "CC must link against Alpine's /lib/ld-musl-$arch.so.1; glibc/custom-loader binaries are not Alpine builds"

set -- -O2 -Wall -Wextra -ffp-contract=off -Iinclude
if [ "$debug" = 1 ]; then
  set -- -O1 -g -Wall -Wextra -Wconversion -Wshadow -ffp-contract=off -Iinclude \
    -fsanitize=address,undefined -fno-omit-frame-pointer
fi
[ "$static" = 0 ] || set -- "$@" -static
if [ "${LAPIS_OBSIDIAN_WEB_CLIENT:-0}" = 1 ] || [ "${LAPIS_OBSIDIAN_WEB_CLIENT:-0}" = 2 ]; then
  sh tools/embed-web.sh > "$work/web_assets.h"
  set -- "$@" -DLAPIS_OBSIDIAN_WEB_CLIENT="$LAPIS_OBSIDIAN_WEB_CLIENT" "-I$work"
fi
printf 'Building Lapis Obsidian for Alpine %s using %s\n' "$arch" "$triple"
"$compiler" src/*.c "$@" -o "$work/lapis-obsidian" -lm
mkdir -p "$(dirname "$output")"
mv -f "$work/lapis-obsidian" "$output"
printf 'Built %s\n' "$output"
