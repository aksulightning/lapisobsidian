#!/bin/sh
# Opt-in Alpine server with its local LapisCube HTTP/WebSocket browser bundle.
set -eu
LC_ALL=C
export LC_ALL

usage() {
  cat <<'HELP'
Usage: ./build_clientsrv.sh [OPTIONS]

Build and package the Alpine server with optional web-client hosting enabled.
By default, build_client.sh builds the browser client first (Alpine 3.23).
The normal build-alpine.sh server remains TCP-only without --webclient.

  --install-deps   Install Alpine dependencies, then build (root)
  --deps-only      Install dependencies and exit (root)
  --prebuilt DIR  Use a validated webclient/ directory; no Emscripten needed
  --static        Link the server statically with musl
  --test          Test the host and default-off behavior in temporary directories
                  Also run client regressions when building the browser bundle
  --jobs N, -j N  Browser compiler jobs (default: ENGINE_JOBS or 2)
  --arch ARCH     Require native x86_64 (x64/amd64), aarch64 (arm64), or riscv64
  --no-fetch      Require cached browser toolchain/engine; prebuilt mode is offline
  --help, -h      Show this help

CC selects one native compiler executable (default: cc); READELF selects readelf.
Relative --prebuilt paths are relative to your invocation directory.
Outputs: clients/lapiscube/build/LapisCube-web-server-alpine-<arch>.tar.gz,
         .buildinfo.txt and .sha256. Extract the archive outside the build tree.
The package includes the server, start.sh, webclient/, licenses and server source.
start.sh uses its bundled assets and keeps worlds/settings in your current directory.
Default web URL: http://localhost:8080/; TCP game port: 25565.
LAPIS_WEB_PORT, LAPIS_WEB_ROOT and LAPIS_WEB_ORIGIN configure hosting at runtime.
No server is started by the build unless --test is explicitly requested.
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
prebuilt=
static=0
run_tests=0
fetch=1
arch=
jobs=${ENGINE_JOBS:-2}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --install-deps) install_deps=1; shift ;;
    --deps-only) install_deps=1; deps_only=1; shift ;;
    --static) static=1; shift ;;
    --test) run_tests=1; shift ;;
    --no-fetch) fetch=0; shift ;;
    --arch|--prebuilt|--jobs|-j)
      if [ "$#" -lt 2 ] || [ -z "$2" ]; then fail "$1 requires a value"; fi
      case "$1" in
        --arch) arch=$(normalize "$2") || fail "Unsupported architecture: $2" ;;
        --prebuilt) prebuilt=$2 ;;
        *) jobs=$2 ;;
      esac
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) fail "Unknown argument: $1 (see --help)" ;;
  esac
done
case "$jobs" in ''|*[!0-9]*|0*) fail "--jobs/ENGINE_JOBS must be a positive integer without leading zeros" ;; esac
if [ ! -f /etc/alpine-release ] || ! command -v apk >/dev/null 2>&1; then
  fail "Run this script on Alpine Linux; see clients/lapiscube/docs/web.md for other systems"
fi
host=$(normalize "$(apk --print-arch)") || fail "Unsupported Alpine architecture"
[ -n "$arch" ] || arch=$host
[ "$arch" = "$host" ] || fail "Requested $arch, but this Alpine system is $host; use a matching native host"
case "$prebuilt" in ''|/*) ;; *) prebuilt=$PWD/$prebuilt ;; esac

# Resolve a relative compiler/wrapper before changing directories.
compiler=${CC:-cc}
inspector=${READELF:-readelf}
case "$compiler" in */*) case "$compiler" in /*) ;; *) compiler=$PWD/$compiler ;; esac ;; esac
case "$inspector" in */*) case "$inspector" in /*) ;; *) inspector=$PWD/$inspector ;; esac ;; esac
cd "$(dirname "$0")"
project=$(pwd -P)
client=$project/clients/lapiscube

if [ "$install_deps" = 1 ]; then
  [ "$(id -u)" = 0 ] || fail "Package installation needs root; run doas ./build_clientsrv.sh --deps-only first"
  apk add --no-cache build-base binutils python3 git ca-certificates
  if [ -z "$prebuilt" ]; then
    set -- --deps-only --arch "$arch"
    [ "$run_tests" = 0 ] || set -- "$@" --test
    "$project/build_client.sh" "$@"
  fi
fi
[ "$deps_only" = 0 ] || exit 0
for tool in git python3 tar sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || fail "Missing $tool; see --deps-only"
done
compiler=$(command -v "$compiler") || fail "C compiler missing; install build-base"
inspector=$(command -v "$inspector") || fail "readelf missing; install binutils"
case "$compiler$inspector" in *[[:space:]]*) fail "Compiler/readelf executable paths must not contain whitespace" ;; esac
CC=$compiler
READELF=$inspector
export CC READELF
git rev-parse --show-toplevel >/dev/null 2>&1 || fail "Use a Git clone to record source provenance"

if [ -z "$prebuilt" ]; then
  set -- --arch "$arch" --jobs "$jobs"
  [ "$run_tests" = 0 ] || set -- "$@" --test
  [ "$fetch" = 1 ] || set -- "$@" --no-fetch
  "$project/build_client.sh" "$@"
  prebuilt=$client/build/webclient
fi
# Reject incomplete/tampered bundles before compiling or replacing any archive.
python3 "$client/tools/install_web.py" "$prebuilt" "$prebuilt"
mkdir -p "$client/build"
work=$(mktemp -d "$client/build/.alpine-clientsrv.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
name=LapisCube-web-server-alpine-$arch
package=$work/$name
mkdir -p "$package"
python3 "$client/tools/install_web.py" "$prebuilt" "$package/webclient"

build_server() {
  destination=$1
  enabled=$2
  set -- --arch "$arch" --output "$destination"
  [ "$static" = 0 ] || set -- "$@" --static
  [ "$enabled" = 0 ] || set -- "$@" --webclient
  "$project/build-alpine.sh" "$@"
}
build_server "$package/lapis-obsidian" 1
"$inspector" -l "$package/lapis-obsidian" > "$work/program-headers"
if [ "$static" = 1 ]; then
  if grep -q INTERP "$work/program-headers"; then fail "Expected a static server without an ELF interpreter"; fi
else
  grep -F "[Requesting program interpreter: /lib/ld-musl-$arch.so.1]" "$work/program-headers" >/dev/null ||
    fail "Packaged server does not use the native Alpine musl interpreter"
fi
cat > "$package/start.sh" <<'LAUNCH'
#!/bin/sh
set -eu
base=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
LAPIS_WEB_ROOT=${LAPIS_WEB_ROOT:-$base/webclient}
export LAPIS_WEB_ROOT
exec "$base/lapis-obsidian" "$@"
LAUNCH
chmod 755 "$package/start.sh"
if [ "$run_tests" = 1 ]; then
  build_server "$work/default-server" 0
  set -- -std=c11 -Wall -Wextra -Werror -Iinclude
  [ "$static" = 0 ] || set -- "$@" -static
  "$compiler" "$@" "$client/tests/web_host.c" -o "$work/web-host-test"
  "$work/web-host-test"
  python3 "$client/tests/client_server.py" "$package" "$work/default-server"
fi

cp LICENSE "$package/LICENSE-server"
cp NOTICE.md "$package/NOTICE-server.md"
cp -R "$client/docs" "$package/docs"
# Capture the actual server inputs, including local edits, with the binary.
tar -czf "$package/server-source.tar.gz" src include LICENSE NOTICE.md build-alpine.sh
revision=$(git rev-parse HEAD)
printf 'Server repository revision: %s\nServer repository: https://github.com/aksulightning/lapisobsidian/tree/%s\nActual server source: server-source.tar.gz\nClient source revision and modified-source status: webclient/release.json\n' \
  "$revision" "$revision" > "$package/SOURCE.txt"
cat > "$package/README.txt" <<'README'
Lapis Obsidian with optional LapisCube web hosting

Run start.sh by its absolute path from your server data directory. Worlds and
server.txt stay in that working directory; browser assets come from this package.
Open http://localhost:8080/ after starting. The native TCP game port is 25565.
LAPIS_WEB_PORT overrides the web port. LAPIS_WEB_ROOT overrides the browser bundle.
For HTTPS/WSS and LAPIS_WEB_ORIGIN, follow docs/web.md#hosting.

server-source.tar.gz contains this server's source and build-alpine.sh.
Rebuild on Alpine with ./build-alpine.sh --webclient (add --static if desired).
The normal server build omits HTTP/WebSocket hosting. Client licenses and source
provenance are in webclient/; server license/notices are alongside this README.
README
{
  printf 'Project: Lapis Obsidian with LapisCube web hosting\nArchitecture: %s\nStatic musl: %s\n' "$arch" "$static"
  printf 'Alpine release: '; cat /etc/alpine-release
  printf 'Server revision: %s\n' "$revision"
  printf 'Compiler: '; "$compiler" --version | sed -n '1p'
  printf 'Compiler target: '; "$compiler" -dumpmachine
  printf 'Host tests requested: %s\nWeb bundle manifest SHA-256: ' "$run_tests"
  sha256sum "$package/webclient/release.json" | cut -d ' ' -f 1
  printf '\nWorking tree changes (empty means clean):\n'; git status --porcelain
  printf '\nInstalled Alpine packages:\n'; apk info -v | sort
} > "$package/BUILDINFO.txt"
tar -czf "$work/$name.tar.gz" -C "$work" "$name"
cp "$package/BUILDINFO.txt" "$work/$name.buildinfo.txt"
(cd "$work" && sha256sum "$name.tar.gz" "$name.buildinfo.txt" > "$name.sha256")
for suffix in tar.gz buildinfo.txt sha256; do
  mv -f "$work/$name.$suffix" "$client/build/$name.$suffix"
done
printf '\nPackage: %s/build/%s.tar.gz\nChecksums: %s/build/%s.sha256\n' "$client" "$name" "$client" "$name"
printf 'Extract outside the build tree, then run the packaged start.sh from your server data directory.\n'
