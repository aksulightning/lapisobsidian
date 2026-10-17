# Debian and experimental build presets

All three scripts compile in HTTP/WebSocket hosting. The experimental presets
default both persistent drops and Java authentication to `1`; setting either
experimental environment variable to `0` overrides that default.

| Script | Platform | Experimental defaults (drops/auth) | Browser assets |
| --- | --- | --- | --- |
| `build-debian.sh` | Debian/Ubuntu and derivatives | `0` / `0` | Builds or installs a validated prebuilt bundle |
| `build-experiments-debian.sh` | Debian/Ubuntu and derivatives | `1` / `1` | Builds or installs a validated prebuilt bundle |
| `build-experiments-alpine.sh` | Alpine/musl, including existing cross toolchains | `1` / `1` | Server only; install the bundle separately |

These are build-time settings, not runtime switches. None of these scripts
installs system packages or starts a server. The existing `build.sh` and
`build-alpine.sh` defaults remain TCP-only with both experiments off.

**Authentication currently prevents LapisCube login**, over both TCP and
WebSocket. An authenticated build can serve the browser files, but playing in
LapisCube requires `LAPIS_ENABLE_EXPERIMENTAL_AUTH=0` at build time. The presets
do not bypass authentication. See [experimental systems](experimental-systems.md)
for persistence, dependencies, supported Java clients and validation limits.

## Debian/Ubuntu

Install the native prerequisites:

```sh
sudo apt-get update
sudo apt-get install build-essential bash git python3 make patch ca-certificates
# Also required when authentication is enabled:
sudo apt-get install libssl-dev libcurl4-openssl-dev
```

For a fresh browser build, initialize the pinned engine and activate an
Emscripten SDK environment (`emcc` on PATH; the existing browser workflow uses
emsdk 4.0.23). See the [browser build guide](../clients/lapiscube/docs/web.md).

```sh
git submodule update --init --recursive
./build-debian.sh
# Or enable both experimental systems:
./build-experiments-debian.sh
# Persistent drops with playable LapisCube browser multiplayer:
LAPIS_ENABLE_EXPERIMENTAL_AUTH=0 ./build-experiments-debian.sh
```

To reuse an extracted, validated browser bundle without Emscripten:

```sh
./build-debian.sh --prebuilt /path/to/webclient
./build-experiments-debian.sh --prebuilt /path/to/webclient
```

`LAPIS_WEBCLIENT_PREBUILT=/path/to/webclient` is equivalent to `--prebuilt`;
the option takes precedence. Paths are resolved from the invocation directory.
`CC` selects one native compiler executable, including a relative wrapper path.
`--debug` or `DEBUG=1` enables the existing ASan/UBSan build. Experimental flags
are inherited by the normal Debian preset and validated as `0` or `1`.

Outputs are `lapis-obsidian` and `webclient/` at the repository root. Launch
`./lapis-obsidian` there and open `http://localhost:8080/`. If launching from a
separate data directory, set `LAPIS_WEB_ROOT` to the absolute bundle directory.
The TCP game port is 25565; [web hosting settings](../clients/lapiscube/docs/web.md#hosting)
describe the web port, origin and reverse-proxy options.

## Alpine

The experimental wrapper retains `build-alpine.sh`'s compiler/ABI checks,
`--arch`, `--output`, `--debug` and `--static` options. Relative compiler and
output paths retain their meaning even when invoked outside the repository.

```sh
apk add --no-cache build-base binutils openssl-dev curl-dev ca-certificates
./build-experiments-alpine.sh
```

Authentication currently requires dynamic linking. For static persistent drops
and browser-compatible login:

```sh
LAPIS_ENABLE_EXPERIMENTAL_AUTH=0 ./build-experiments-alpine.sh --static
```

As with `build-alpine.sh --webclient`, the wrapper compiles only the server and
does not fetch or build browser assets. On Alpine 3.23, build those separately:

```sh
./build_client.sh --install-deps
python3 clients/lapiscube/tools/install_web.py clients/lapiscube/build/webclient webclient
LAPIS_ENABLE_EXPERIMENTAL_AUTH=0 ./build-experiments-alpine.sh
./lapis-obsidian
```

Only `build_client.sh --install-deps` in this example requires root. Use its
`--deps-only` option as root first to build as a normal user. Alternatively,
install a prebuilt bundle with `install_web.py /path/to/webclient webclient`.
Keep the bundle available under `LAPIS_WEB_ROOT` when using `--output` to place
the server elsewhere. See the [Alpine toolchain guide](build-alpine.md) for
cross compilation and the [browser build guide](../clients/lapiscube/docs/alpine.md)
for assets and offline rebuilds.
