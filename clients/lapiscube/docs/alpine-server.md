# Alpine server with optional browser hosting

`build_clientsrv.sh` explicitly opts into the existing HTTP/WebSocket server
feature and packages the server with a validated LapisCube browser bundle.
Plain `build-alpine.sh` and `build.sh` builds remain TCP-only. Hosting is selected
at compile time; a runtime environment variable cannot enable the default binary.

## Build

On Alpine 3.23 with matching `main` and `community` repositories enabled:

```sh
doas ./build_clientsrv.sh --deps-only
./build_clientsrv.sh --test
```

In a disposable root-owned build container, combine installation and building:

```sh
./build_clientsrv.sh --install-deps --test
```

The script uses `/bin/sh`, builds the web client through `build_client.sh`, then
compiles the server through `build-alpine.sh --webclient`. The browser compiler
and assets are described in the [web-client build guide](alpine.md). Server
compilation needs `build-base` and `binutils`; Python validates the bundle.
Without an installation flag, no system packages are changed.

To reuse a web client already built or extracted from `LapisCube-web.zip`:

```sh
doas ./build_clientsrv.sh --deps-only --prebuilt /path/to/webclient
./build_clientsrv.sh --prebuilt /path/to/webclient --static --test
```

Prebuilt mode needs no Emscripten, Node, npm or initialized engine submodule.
It validates the manifest, exact file set, per-file hashes, required licenses and
WASM magic before compiling the server. It makes no network requests unless
package installation is explicitly requested. Relative paths are resolved from
the directory where you invoke the script.

`--arch` checks the native host: `x86_64` (`x64`, `amd64`), `aarch64` (`arm64`) or
`riscv64`. `--static` affects the server's musl linkage; the browser bundle stays
WebAssembly. `--jobs`/`ENGINE_JOBS` control the client build. `--no-fetch` reuses
client toolchain caches after a successful build; it is also accepted with
`--prebuilt`. `CC` and `READELF` select single native executables, not option lists.

## Package and run

Outputs in `clients/lapiscube/build/`:

| Output | Contents |
| --- | --- |
| `LapisCube-web-server-alpine-<arch>.tar.gz` | Server, launcher, browser bundle, notices, documentation and actual server source inputs |
| `LapisCube-web-server-alpine-<arch>.buildinfo.txt` | Revision, source dirty state, compiler/packages, linkage and client manifest hash |
| `LapisCube-web-server-alpine-<arch>.sha256` | Checksums for the archive and build information |

Only these generated archives/metadata are replaced on rebuild. The script
never overwrites the repository's `lapis-obsidian` executable or an extracted
server/data directory. The package contains `server-source.tar.gz` with `src/`,
`include/`, notices and the Alpine compiler script; this captures local source
edits as well as committed inputs. Client provenance remains in `webclient/`.

Copy the archive and sidecars to a directory outside the checkout, then:

```sh
sha256sum -c LapisCube-web-server-alpine-x86_64.sha256
tar -xzf LapisCube-web-server-alpine-x86_64.tar.gz
mkdir -p server-data
cd server-data
../LapisCube-web-server-alpine-x86_64/start.sh
```

Use the archive for your CPU. Dynamic builds need an Alpine/musl runtime;
`--static` removes the server's shared-library dependency. `start.sh` locates the
bundled web assets independently of the current directory. It keeps worlds,
players and `server.txt` in the current working directory (`server-data` above).
Open **http://localhost:8080/** in a browser, or use the server's LAN address.
The normal TCP game port remains **25565** and can be configured in `server.txt`.

The web-enabled binary accepts `LAPIS_WEB_PORT` (default 8080), `LAPIS_WEB_ROOT`
(defaulted by the launcher to the bundled assets), and `LAPIS_WEB_ORIGIN`.
For public HTTPS/WSS hosting, follow the [reverse-proxy instructions](web.md#hosting).
The build script does not start services, change firewall settings or launch a
persistent server. `--test` alone starts temporary test instances and stops them.

## Checks

Every build validates the browser bundle and final server linkage. `--test` runs
the host's malformed-input/bounds unit tests, then checks the packaged launcher
from a separate data directory: HTTP assets and MIME types, WebSocket origin
checks, ping and game status, and absence of HTTP hosting in a default server
regardless of the runtime flag. Building a new browser bundle with `--test` also
runs the client's protocol/gameplay/audio/asset regressions.

Alpine CI exercises the combined dynamic build and a static prebuilt-bundle
build, verifies archive checksums/executable permissions and revalidates the
extracted browser package. Existing web transport and desktop/touch acceptance
continue to cover the optional host implementation.
