# Alpine Linux client

From the repository root, `build_client.sh` builds the native, windowed LapisCube
client, bundles the committed textures/sounds and license notices, and verifies
the package. It uses Alpine's `/bin/sh`; Bash, Java, Node, Pillow and FFmpeg are
not needed for a normal build. Node is needed only for `--test`.

## Quick start

Enable `main` and `community` for the **same Alpine release** in
`/etc/apk/repositories`. As root, install Git for the initial clone:

```sh
apk add --no-cache git ca-certificates
```

As your normal user:

```sh
git clone --branch testing-cube --recurse-submodules https://github.com/aksulightning/lapisobsidian.git
cd lapisobsidian
doas ./build_client.sh --deps-only
./build_client.sh
./clients/lapiscube/build/LapisCube-linux/connect.sh MyName 127.0.0.1 25565
```

Use `sudo` instead of `doas` if that is your configured privilege tool, or run
only the dependency command from a root shell. The script does not invoke either
tool itself. In a disposable root-owned build container, use
`./build_client.sh --install-deps` for the complete installation/build flow.
The server must already be running; build it separately with `./build-alpine.sh`.
The client script does not launch a server or game automatically.

The dependencies installed are:

```sh
apk add --no-cache build-base binutils git ca-certificates python3 patch \
  libx11-dev libxi-dev mesa-dev mesa-dri-gallium openal-soft-libs alsa-lib libpulse
```

X11/Xi/OpenGL development libraries are needed for compilation. OpenAL and its
ALSA/PulseAudio backends are loaded at runtime; installing only compiler packages
can otherwise produce a client with no sound. The engine includes its font and
network code; no separate asset downloads or font-generation packages are needed.

## Options and output

```sh
./build_client.sh --jobs 4 --arch x64
doas ./build_client.sh --deps-only --test
./build_client.sh --test --no-fetch
./build_client.sh --help
```

`--arch` verifies the native target: `x86_64` (`x64`, `amd64`), `aarch64` (`arm64`)
or `riscv64`. This script does not cross-compile. `CC` selects one compiler or
wrapper executable, and `ENGINE_JOBS` sets the default parallelism (otherwise 2).
Lower `--jobs` on memory-constrained systems. Every invocation fully recompiles
the client, so previous glibc or other-architecture objects cannot be reused.
Both the compiler probe and final executable must have the matching Alpine musl
interpreter; static and glibc builds are rejected.

A missing ClassiCube submodule is initialized at the repository's pinned commit.
An existing checkout with a different revision or local changes is rejected by
the staging tool, never reset. `--no-fetch` requires the submodule to be present
and permits an offline build once dependencies are installed. Without an install
flag the script makes no package-manager changes. It works when invoked by an
absolute path from outside the repository.

Outputs in `clients/lapiscube/build/`:

| Output | Contents |
| --- | --- |
| `LapisCube-alpine-<arch>.zip` | Complete client, launcher, assets, licenses and per-file hashes |
| `LapisCube-alpine-<arch>.buildinfo.txt` | Source revisions, dirty state, Alpine/compiler/package versions |
| `LapisCube-alpine-<arch>.sha256` | Checksums of the archive and build information |
| `LapisCube-linux/` | Runnable package directory; recreated by packaging |

The Alpine ZIP uses the existing `LapisCube-linux/` directory inside it, and the
normal `LapisCube-linux.zip` is also produced. Use the **Alpine-named** ZIP when
sharing it: this executable needs musl and is not the glibc Linux CI build.
Extract the entire archive with `unzip`, then launch `connect.sh` from an X11
desktop (or a Wayland desktop with Xwayland and `DISPLAY` configured). The launcher
locates its assets even when invoked from another directory. Do not store worlds
or custom settings in the generated build directory; extract a copy to play.

On another Alpine machine of the same architecture, runtime packages are:

```sh
apk add --no-cache libx11 libxi mesa-gl mesa-dri-gallium openal-soft-libs alsa-lib libpulse
unzip LapisCube-alpine-x86_64.zip
./LapisCube-linux/connect.sh MyName HOST 25565
```

The machine also needs a working desktop, GPU driver and audio session. This does
not configure a desktop or start audio services. Mesa provides a software fallback;
hardware acceleration depends on the machine. Configure Xwayland through your
desktop if needed. The ZIP intentionally uses system graphics/audio libraries.

## Validation

`--test` runs the existing protocol, gameplay, audio, asset/license/hash, packaging
and JavaScript audio tests before building. It does not need a display or server.
Every build verifies the musl interpreter and packaged file hashes/permissions.
The native-client workflow builds on Alpine 3.23.6 for x86-64, ARM64 and emulated
RISC-V, runs `--test`, and uploads each Alpine archive, build information and
checksums. Its x86-64 job also exercises the extracted package against a local
server under Xvfb/software Mesa. This does not establish physical GPU, audio-device
or listening acceptance on every architecture.

Package references: Alpine's [mesa-dev](https://pkgs.alpinelinux.org/package/v3.23/main/x86_64/mesa-dev),
[libxi-dev](https://pkgs.alpinelinux.org/package/v3.23/main/x86_64/libxi-dev) and
[openal-soft-libs](https://pkgs.alpinelinux.org/package/v3.23/community/x86_64/openal-soft-libs).
