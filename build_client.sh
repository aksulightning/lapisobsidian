#!/bin/sh
# Build and package the native LapisCube client on Alpine Linux.
set -eu
LC_ALL=C
export LC_ALL

usage() {
  cat <<'HELP'
Usage: ./build_client.sh [OPTIONS]

Build the native X11/OpenGL client and its complete asset/license package.
Run on Alpine Linux with the matching main and community repositories enabled.

  --install-deps   Install build/runtime packages with apk, then build (root)
  --deps-only      Install packages and exit (root; build as your normal user)
  --test           Run the client C, Python and JavaScript regression tests
  --jobs N, -j N   Compiler jobs (default: ENGINE_JOBS or 2)
  --arch ARCH     Require x86_64 (x64/amd64), aarch64 (arm64), or riscv64
  --no-fetch      Do not initialize a missing engine submodule; build offline
  --help, -h      Show this help

Without an install option, no system packages are changed. A missing engine
submodule is downloaded at the repository's pinned revision unless --no-fetch
is supplied. An existing checkout is never reset or updated by this script.
CC selects one compiler executable or wrapper (default: cc).
This is a native, dynamically linked musl build, not a cross/static build.
Outputs: clients/lapiscube/build/LapisCube-alpine-<arch>.zip, .buildinfo.txt,
         .sha256 and the runnable LapisCube-linux/ directory.
HELP
}

fail() { printf '%s\n' "Error: $*" >&2; exit 1; }
normalize() {
  case "$1" in
    x86_64|x64|amd64) printf '%s\n' x86_64 ;;
    aarch64|arm64) printf '%s\n' aarch64 ;;
    riscv64) printf '%s\n' riscv64 ;;
    *) return 1 ;;
  esac
}

install_deps=0
deps_only=0
run_tests=0
fetch=1
arch=
jobs=${ENGINE_JOBS:-2}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --install-deps) install_deps=1; shift ;;
    --deps-only) install_deps=1; deps_only=1; shift ;;
    --test) run_tests=1; shift ;;
    --no-fetch) fetch=0; shift ;;
    --arch|--jobs|-j)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 requires a value"
      case "$1" in
        --arch) arch=$(normalize "$2") || fail "Unsupported architecture: $2" ;;
        *) jobs=$2 ;;
      esac
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1 (see --help)" ;;
  esac
done
case "$jobs" in ''|*[!0-9]*|0*) fail "--jobs/ENGINE_JOBS must be a positive integer without leading zeros" ;; esac
[ -f /etc/alpine-release ] && command -v apk >/dev/null 2>&1 ||
  fail "Run this script on Alpine Linux; other Linux systems can use make -C clients/lapiscube native package"
host=$(normalize "$(apk --print-arch)") || fail "Unsupported Alpine architecture"
[ -n "$arch" ] || arch=$host
[ "$arch" = "$host" ] || fail "Requested $arch, but this Alpine system is $host; cross compilation is not supported"

if [ "$install_deps" = 1 ]; then
  [ "$(id -u)" = 0 ] || fail "Package installation needs root. Run doas ./build_client.sh --deps-only, then build as your normal user"
  # OpenAL loads audio backends at runtime; they are not linker dependencies.
  set -- build-base binutils git ca-certificates python3 patch libx11-dev libxi-dev \
    mesa-dev mesa-dri-gallium openal-soft-libs alsa-lib libpulse
  [ "$run_tests" = 0 ] || set -- "$@" nodejs
  apk add --no-cache "$@" || fail "apk failed; enable main and community for your Alpine release in /etc/apk/repositories"
fi
[ "$deps_only" = 0 ] || exit 0

for tool in git make python3 patch readelf sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; see --install-deps/--deps-only"
done
if [ "$run_tests" = 1 ]; then
  command -v node >/dev/null 2>&1 || fail "--test needs nodejs; install with --deps-only --test"
fi
compiler=$(command -v "${CC:-cc}") || fail "C compiler missing; install build-base"
case "$compiler" in /*) ;; *) compiler=$PWD/$compiler ;; esac
# GNU Make and the upstream recipes cannot quote a compiler path containing spaces.
case "$compiler" in *[[:space:]]*) fail "CC must name a compiler/wrapper path without whitespace" ;; esac
cd "$(dirname "$0")"
project=$(pwd -P)
client=$project/clients/lapiscube
[ -f "$client/Makefile" ] || fail "Cannot find the LapisCube source next to this script"
git rev-parse --show-toplevel >/dev/null 2>&1 || fail "Use a Git clone, including the pinned engine submodule"
python3 -c 'import sys; assert sys.version_info >= (3, 8), "Python 3.8+ is required"'
triple=$("$compiler" -dumpmachine)
target=$(normalize "${triple%%-*}") || fail "Unsupported compiler target: $triple"
[ "$target" = "$arch" ] || fail "CC targets $target, but the native Alpine architecture is $arch"

mkdir -p "$client/build"
work=$(mktemp -d "$client/build/.alpine-client.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$work/probe.c" <<'C'
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <GL/gl.h>
_Static_assert(sizeof(void *) == 8, "A 64-bit compiler ABI is required");
int main(void) { return 0; }
C
"$compiler" "$work/probe.c" -o "$work/probe" -lX11 -lXi -lGL -lpthread -ldl -lm ||
  fail "Compiler/X11/OpenGL probe failed; install the development packages with --deps-only"
check_musl() {
  readelf -l "$1" > "$work/elf.txt"
  grep -F "[Requesting program interpreter: /lib/ld-musl-$arch.so.1]" "$work/elf.txt" >/dev/null ||
    fail "Expected the native Alpine /lib/ld-musl-$arch.so.1 interpreter: $1"
}
check_musl "$work/probe"
python3 -c 'import ctypes; [ctypes.CDLL(n) for n in ("libX11.so.6", "libXi.so.6", "libGL.so.1", "libopenal.so.1")]' ||
  fail "Runtime libraries missing; install the packages with --deps-only"

if [ ! -e "$client/engine/.git" ]; then
  [ "$fetch" = 1 ] || fail "Engine submodule missing and --no-fetch was requested; run git submodule update --init --recursive"
  git submodule update --init --recursive -- clients/lapiscube/engine
fi
# prepare_engine.py verifies both the exact pin and a pristine engine checkout.
# Force rebuilding: objects left by glibc, a different compiler or architecture
# must never leak into an Alpine package. The source checkout stays untouched.
printf 'Building LapisCube for Alpine %s (%s), %s compiler jobs\n' "$arch" "$triple" "$jobs"
if [ "$run_tests" = 1 ]; then
  make -B -C "$client" test CC="$compiler"
fi
make -B -C "$client" native CC="$compiler" ENGINE_JOBS="$jobs" \
  NATIVE_ARGS="BUILD_DIR=build/lapiscube-alpine-$arch"
check_musl "$client/build/engine/LapisCube"
make -C "$client" package

name=LapisCube-alpine-$arch
cp "$client/build/LapisCube-linux.zip" "$work/$name.zip"
{
  printf 'Project: LapisCube\nArchitecture: %s\nLinkage: dynamic musl, X11/OpenGL\n' "$arch"
  printf 'Alpine release: '; cat /etc/alpine-release
  printf 'Client revision: '; git rev-parse HEAD
  printf 'Engine revision: '; git -C "$client/engine" rev-parse HEAD
  printf 'Compiler: '; "$compiler" --version | sed -n '1p'
  printf 'Compiler target: %s\nClient regression tests requested: %s\n' "$triple" "$run_tests"
  printf '\nWorking tree changes (empty means clean):\n'; git status --porcelain
  printf '\nInstalled Alpine packages:\n'; apk info -v | sort
} > "$work/$name.buildinfo.txt"
(cd "$work" && sha256sum "$name.zip" "$name.buildinfo.txt" > "$name.sha256")
for suffix in zip buildinfo.txt sha256; do
  mv -f "$work/$name.$suffix" "$client/build/$name.$suffix"
done
printf '\nPackage: %s/build/%s.zip\nChecksums: %s/build/%s.sha256\n' "$client" "$name" "$client" "$name"
printf 'Connect from your desktop session:\n  "%s/build/LapisCube-linux/connect.sh" MyName HOST 25565\n' "$client"
