# Alpine Linux web client

From the repository root, `build_client.sh` builds **only the browser client**:
WebAssembly, JavaScript, HTML/CSS, committed textures/sounds and license notices.
It does not build or launch a desktop client or server. Alpine's `/bin/sh` is
sufficient; no Bash, X11, OpenGL, OpenAL, Java, Pillow or FFmpeg is required.

## Quick start

Use Alpine **3.23**, with `main` and `community` for that release enabled in
`/etc/apk/repositories`. Install Git as root for the initial clone:

```sh
apk add --no-cache git ca-certificates
```

As your normal user:

```sh
git clone --branch testing-cube https://github.com/aksulightning/lapisobsidian.git
cd lapisobsidian
doas ./build_client.sh --deps-only
./build_client.sh
```

Use `sudo` instead of `doas` if that is your configured privilege tool, or run
only the dependency command from a root shell. The script never invokes either.
In a disposable root-owned build container, `./build_client.sh --install-deps`
installs dependencies and builds in one command.

The first build downloads the pinned ClassiCube engine, pinned Emscripten sources
and Emscripten's locked npm dependencies. Assets are already committed. Existing
source checkouts with a different revision or local changes are rejected rather
than reset. The script also works when invoked by an absolute path from elsewhere;
the checkout path must not contain whitespace because of upstream Make recipes.

## Alpine-native toolchain

The dependency options install:

```sh
apk add --no-cache git ca-certificates make python3 patch nodejs npm \
  clang21 llvm21 lld21 binaryen
```

`--test` additionally installs `build-base` when used with an installation flag.
Node and npm are needed for normal web builds too.

Alpine 3.23's LLVM 21 and Binaryen 123 are paired with Emscripten **4.0.10**, pinned
to `b7dc6e5747465580df5984e723b9d1f10d8e804b`. Emscripten's Python/JavaScript
sources use Alpine's native musl tools instead of emsdk's glibc Linux binaries.
The script creates its own configuration, source checkout and writable compiler
cache under `clients/lapiscube/build/toolchains/`. It leaves `~/.emscripten` alone.
The small compiler/linker probe runs before the full build.

The separate Ubuntu browser acceptance workflow uses emsdk 4.0.23. This Alpine
script deliberately uses the Emscripten version matching Alpine's packages.
Package versions are recorded in build metadata; this is not a byte-for-byte
reproducibility guarantee across package updates or build hosts.

## Options and output

```sh
./build_client.sh --jobs 4 --arch x64
doas ./build_client.sh --deps-only --test
./build_client.sh --test
./build_client.sh --no-fetch
./build_client.sh --help
```

`--arch` checks the **build host**, accepting `x86_64` (`x64`, `amd64`), `aarch64`
(`arm64`) or `riscv64`. The output is always browser WebAssembly, independent of
the host CPU. `ENGINE_JOBS` sets default parallelism (otherwise 2); `--jobs`
overrides it and also limits Emscripten/Binaryen worker counts. Lower it on
memory-constrained machines. `CC` selects one host compiler/wrapper executable
only for `--test`; the web build always uses the pinned Emscripten toolchain.

Every invocation recompiles the client to avoid reusing incompatible objects.
After one successful build, `--no-fetch` reuses the source/npm/compiler caches
without Git/npm downloads. Keep `clients/lapiscube/build/toolchains/` for this;
`make clean` removes it. `--deps-only` installs OS packages only, so it does not
by itself prepare an offline build. Without an installation flag, the script
makes no package-manager changes.

Outputs in `clients/lapiscube/build/`:

| Output | Contents |
| --- | --- |
| `LapisCube-web.zip` | Complete `webclient/` directory, assets, licenses and file hashes |
| `LapisCube-web.buildinfo.txt` | Source revisions, dirty state, Alpine/toolchain/package versions |
| `LapisCube-web.sha256` | SHA-256 checksums of the archive and build information |
| `webclient/` | Ready-to-host browser package; recreated by packaging |

Verify the downloads with `sha256sum -c LapisCube-web.sha256` from their directory.
Every build checks the deployable directory and extracted ZIP against the web
release manifest, including required files/licenses, hashes and WASM magic.

Host the extracted `webclient/` directory with an existing WebSocket-enabled
Lapis server or reverse proxy, following the [web hosting guide](web.md#hosting).
Opening `index.html` as a local file is not a game server. The client script does
not run an HTTP server or change existing server binaries/configuration.

## Validation

`--test` runs the existing protocol, gameplay, audio, asset/license/hash,
packaging and JavaScript audio regressions. It needs no display or running
server and builds only the test executables in addition to the browser client.

The web workflow's Alpine jobs build on x86-64, ARM64 and emulated RISC-V using
Alpine 3.23.6. They exercise dependency installation, the client tests, a cached
`--no-fetch` rebuild and checksums, and assert that neither the native game client
nor a server binary was produced. Each host uploads a separately named artifact
containing the same browser-targeted package filenames.

References: Alpine's [LLVM 21 compiler](https://pkgs.alpinelinux.org/package/v3.23/main/x86_64/clang21),
[Binaryen 123](https://pkgs.alpinelinux.org/package/v3.23/community/x86_64/binaryen),
and [Emscripten source builds](https://emscripten.org/docs/building_from_source/index.html).
