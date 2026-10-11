#!/bin/sh
# Build and package only the LapisCube browser client on Alpine Linux.
set -eu
LC_ALL=C
export LC_ALL

# Alpine 3.23 supplies LLVM 21 and Binaryen 123. Use matching Emscripten
# sources; emsdk's prebuilt Linux toolchain requires glibc, not Alpine musl.
emscripten_version=4.0.10
emscripten_revision=b7dc6e5747465580df5984e723b9d1f10d8e804b

usage() {
  cat <<'HELP'
Usage: ./build_client.sh [OPTIONS]

Build only the WebAssembly browser client, assets and license package on Alpine
Linux 3.23. Enable main and community for the same Alpine release.

  --install-deps   Install Alpine toolchain packages, then build (root)
  --deps-only      Install Alpine packages and exit (root)
  --test           Run client C, Python and JavaScript regression tests
  --jobs N, -j N   Compiler jobs (default: ENGINE_JOBS or 2)
  --arch ARCH     Check the build host: x86_64 (x64/amd64), aarch64 (arm64), riscv64
  --no-fetch      Require cached engine, Emscripten sources and npm dependencies
  --help, -h      Show this help

Without an install option, no system packages are changed. The first build
downloads the pinned engine and Emscripten sources plus locked npm dependencies.
Toolchain sources, configuration and cache live in clients/lapiscube/build/.
Existing source checkouts are verified, never reset. --no-fetch prevents Git/npm
downloads; combine it with a normal build after an initial successful build.
CC selects the host compiler for --test only (default: cc).

Outputs: clients/lapiscube/build/LapisCube-web.zip, .buildinfo.txt, .sha256,
         and the ready-to-host webclient/ directory.
The output targets browsers on any host architecture. No desktop client or
server is built or started. See clients/lapiscube/docs/web.md for hosting.
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
      if [ "$#" -lt 2 ] || [ -z "$2" ]; then
        fail "$1 requires a value"
      fi
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
if [ ! -f /etc/alpine-release ] || ! command -v apk >/dev/null 2>&1; then
  fail "Run this script on Alpine Linux; elsewhere use make -C clients/lapiscube web with Emscripten on PATH"
fi
host=$(normalize "$(apk --print-arch)") || fail "Unsupported Alpine build host architecture"
[ -n "$arch" ] || arch=$host
[ "$arch" = "$host" ] || fail "Requested host $arch, but this Alpine system is $host; the output always targets WebAssembly"

if [ "$install_deps" = 1 ]; then
  [ "$(id -u)" = 0 ] || fail "Package installation needs root. Run doas ./build_client.sh --deps-only, then build as your normal user"
  set -- git ca-certificates make python3 patch nodejs npm clang21 llvm21 lld21 binaryen
  [ "$run_tests" = 0 ] || set -- "$@" build-base
  apk add --no-cache "$@" || fail "apk failed; Alpine 3.23 main/community must provide LLVM 21 and Binaryen 123"
fi
[ "$deps_only" = 0 ] || exit 0

for tool in git make python3 patch node npm wasm-opt sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; see --install-deps/--deps-only"
done
for tool in clang clang++ llvm-ar llvm-nm; do
  [ -x "/usr/lib/llvm21/bin/$tool" ] || fail "Missing LLVM 21 $tool; see --deps-only"
done
[ -x /usr/bin/wasm-ld ] || fail "Missing wasm-ld; install lld21 with --deps-only"
case "$(/usr/bin/wasm-ld --version)" in
  'LLD 21.'*) ;;
  *) fail "The WebAssembly linker must be LLVM 21; install lld21 with --deps-only" ;;
esac
case "$(wasm-opt --version)" in
  'wasm-opt version 123'*) ;;
  *) fail "This pinned Emscripten toolchain requires Alpine's Binaryen 123" ;;
esac
compiler=
if [ "$run_tests" = 1 ]; then
  compiler=$(command -v "${CC:-cc}") || fail "--test needs a host C compiler; install with --deps-only --test"
  case "$compiler" in /*) ;; *) compiler=$PWD/$compiler ;; esac
  case "$compiler" in *[[:space:]]*) fail "CC must name a compiler/wrapper path without whitespace" ;; esac
fi
cd "$(dirname "$0")"
project=$(pwd -P)
# The upstream make recipes do not support whitespace in build paths.
case "$project" in *[[:space:]]*) fail "Clone the repository into a path without whitespace" ;; esac
client=$project/clients/lapiscube
[ -f "$client/Makefile" ] || fail "Cannot find the LapisCube source next to this script"
git rev-parse --show-toplevel >/dev/null 2>&1 || fail "Use a Git clone, including the pinned engine submodule"
python3 -c 'import sys; assert sys.version_info >= (3, 9), "Python 3.9+ is required"'

mkdir -p "$client/build/toolchains"
work=$(mktemp -d "$client/build/.alpine-web.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
emscripten=$client/build/toolchains/emscripten-$emscripten_version
if [ ! -e "$emscripten" ]; then
  [ "$fetch" = 1 ] || fail "Emscripten sources missing and --no-fetch was requested; run once without --no-fetch"
  git init -q "$work/emscripten"
  git -C "$work/emscripten" fetch --depth 1 https://github.com/emscripten-core/emscripten.git "$emscripten_revision"
  git -C "$work/emscripten" checkout -q --detach FETCH_HEAD
  mv "$work/emscripten" "$emscripten"
fi
[ "$(git -C "$emscripten" rev-parse HEAD)" = "$emscripten_revision" ] || fail "Unexpected Emscripten revision: $emscripten"
[ -z "$(git -C "$emscripten" status --porcelain)" ] || fail "Emscripten sources have local changes: $emscripten"
if [ "$fetch" = 1 ]; then
  # Use upstream's bootstrap so its entry points, submodules and dependency
  # stamps stay consistent. Only production npm dependencies are needed.
  if [ ! -d "$emscripten/node_modules/acorn" ]; then
    rm -f "$emscripten/out/npm_packages.stamp"
  fi
  (cd "$emscripten" && npm_config_omit=dev npm_config_ignore_scripts=true \
    npm_config_audit=false npm_config_fund=false python3 bootstrap.py)
else
  [ -d "$emscripten/node_modules/acorn" ] || fail "Emscripten npm dependencies missing; run once without --no-fetch"
  python3 - "$emscripten" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import bootstrap
bootstrap.check()
PY
fi

# Isolate this build from emsdk environments and ~/.emscripten. The system
# compiler/Binaryen executables are Alpine-native; generated code is wasm32.
EM_CONFIG=$client/build/toolchains/emscripten-$emscripten_version.config
EM_CACHE=$client/build/toolchains/cache-$emscripten_version
# Alpine installs lld's drivers in /usr/bin, separately from LLVM's tools.
# Emscripten expects one directory; assemble it without touching /usr.
EM_LLVM_ROOT=$client/build/toolchains/llvm21/bin
mkdir -p "$EM_LLVM_ROOT"
for tool in /usr/lib/llvm21/bin/*; do
  ln -sf "$tool" "$EM_LLVM_ROOT/${tool##*/}"
done
ln -sf /usr/bin/wasm-ld "$EM_LLVM_ROOT/wasm-ld"
EM_BINARYEN_ROOT=/usr
EM_NODE_JS=$(command -v node)
EMCC_CORES=$jobs
BINARYEN_CORES=$jobs
export EM_CONFIG EM_CACHE EM_LLVM_ROOT EM_BINARYEN_ROOT EM_NODE_JS EMCC_CORES BINARYEN_CORES
printf '%s\n' '# Configuration supplied by build_client.sh via EM_* variables.' > "$EM_CONFIG"
PATH=$emscripten:$PATH
export PATH
emcc --check
printf 'int main(void) { return 0; }\n' > "$work/probe.c"
emcc -O2 "$work/probe.c" -sENVIRONMENT=web -o "$work/probe.js" ||
  fail "WebAssembly compiler/linker probe failed; check the Alpine toolchain packages"

if [ ! -e "$client/engine/.git" ]; then
  [ "$fetch" = 1 ] || fail "Engine submodule missing and --no-fetch was requested; run git submodule update --init --recursive"
  git submodule update --init --recursive -- clients/lapiscube/engine
fi
# prepare_engine.py verifies the engine pin and pristine checkout. Force a
# rebuild so objects from another compiler or build mode cannot be reused.
printf 'Building LapisCube web client on Alpine %s, %s compiler jobs\n' "$host" "$jobs"
if [ "$run_tests" = 1 ]; then
  make -B -C "$client" test CC="$compiler"
fi
make -B -C "$client" web ENGINE_JOBS="$jobs"
# Validate the deployable directory with the same checks used by the server's
# prebuilt-bundle installer, without compiling, installing or starting a server.
python3 "$client/tools/install_web.py" "$client/build/webclient" "$client/build/webclient"
python3 - "$client/build/LapisCube-web.zip" "$work/unpacked" "$client/tools" <<'PY'
import pathlib
import sys
import zipfile
sys.path.insert(0, sys.argv[3])
from install_web import install

with zipfile.ZipFile(sys.argv[1]) as archive:
    names = archive.namelist()
    if len(names) != len(set(names)):
        raise SystemExit('Duplicate web archive paths')
    for name in names:
        path = pathlib.PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or path.parts[0] != 'webclient':
            raise SystemExit('Unsafe web archive path: ' + name)
    archive.extractall(sys.argv[2])
bundle = pathlib.Path(sys.argv[2]) / 'webclient'
install(bundle, bundle)
print('Web archive: file set, hashes, WebAssembly and required licenses verified')
PY

name=LapisCube-web
{
  printf 'Project: LapisCube web client\nTarget: wasm32, WebGL 1\nBuild host: %s\n' "$host"
  printf 'Alpine release: '; cat /etc/alpine-release
  printf 'Client revision: '; git rev-parse HEAD
  printf 'Engine revision: '; git -C "$client/engine" rev-parse HEAD
  printf 'Emscripten revision: %s\n' "$emscripten_revision"
  emcc --version | sed -n '1p'
  /usr/lib/llvm21/bin/clang --version | sed -n '1p'
  wasm-opt --version
  printf 'Node: '; node --version
  printf 'Client regression tests requested: %s\n' "$run_tests"
  printf '\nWorking tree changes (empty means clean):\n'; git status --porcelain
  printf '\nInstalled Alpine packages:\n'; apk info -v | sort
} > "$work/$name.buildinfo.txt"
mv -f "$work/$name.buildinfo.txt" "$client/build/$name.buildinfo.txt"
(cd "$client/build" && sha256sum "$name.zip" "$name.buildinfo.txt") > "$work/$name.sha256"
mv -f "$work/$name.sha256" "$client/build/$name.sha256"
printf '\nPackage: %s/build/%s.zip\nChecksums: %s/build/%s.sha256\n' "$client" "$name" "$client" "$name"
printf 'Ready-to-host directory: %s/build/webclient/\n' "$client"
printf 'Hosting instructions: %s/docs/web.md (requires a WebSocket-enabled game server)\n' "$client"
