# LapisCube

Native C client development for Lapis Obsidian, using ClassiCube's engine.

**Current delivery: milestone 1 connection foundation. This is not yet a playable
survival client.** A real connection to the unmodified Lapis testing server has
passed status, offline login, configuration, Play login, spawn synchronization,
keep-alive and reconnection. The integrated engine currently stays on a loading
screen. It receives chunk frames but does not decode or render their blocks.

## Sources

- ClassiCube submodule: `d41c3f7eef2038f59702b58bdb373483fb0d28f9`.
- Lapis testing reference: `56300de744b9b859993f64d5c00d1637e42584c6`.
- Development branch: `testing-cube`; server code is unchanged.

See the [packet compatibility matrix](docs/protocol-matrix.md),
[architecture and milestone plan](docs/architecture.md),
[actual validation results](docs/validation.md) and [license notes](NOTICE.md).

## Build and test the foundation

Requirements: Git, C compiler, Make and Python 3.8+. No Java, game JAR, Microsoft
account or proprietary game assets are used by these tests.

```sh
git clone --branch testing-cube --recurse-submodules https://github.com/aksulightning/lapisobsidian.git
cd lapisobsidian
./build.sh
cd clients/lapiscube
make test
make sanitize
make integration
```

`integration` starts the actual server in a temporary directory on an ephemeral
port with seed 42. It tests status, login and reconnect using the same offline
UUID, stops the server, removes the temporary saves and keeps logs in `build/`.
The integration probe is a test tool, not the game client.

On restricted runners without `/proc`, use
`make sanitize SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS`. This disables leak
detection only; address and undefined-behavior checks stay enabled. Normal
`make sanitize` retains leak detection. The parser/session have no heap allocations.

To inspect a running server:

```sh
make probe
./build/lapiscube-probe status 127.0.0.1 25565 LapisCubeTest
./build/lapiscube-probe login 127.0.0.1 25565 LapisCubeTest
```

The probe exits successfully only after status/pong or Play login, two spawn
teleports, chunk delivery, loaded notification and a keep-alive response queued
and flushed. It prints skipped-packet counts so receipt is not confused with
implemented gameplay. It does not attempt admin authentication or server commands.

## Native engine build

The build stages the pinned engine into `build/engine`, applies
`patches/engine.patch`, and adds the separate Lapis modules. The submodule itself
remains unchanged. Build-mode changes clear engine objects to prevent mixing
window/renderer backends.

Linux windowed build requires the normal ClassiCube X11, Xi and OpenGL development
libraries (for example `libx11-dev libxi-dev libgl-dev` on Debian-family systems):

```sh
make native
./build/engine/LapisCube --lapis MyName 127.0.0.1 25565
```

The existing native terminal/software-renderer backend can test integration in
an environment without a window server:

```sh
make terminal
make native-smoke
```

`native-smoke` runs the actual engine through a PTY against Lapis, then checks
the original Classic login against a synthetic Classic peer. It is an automated
connection test, not visual gameplay validation.

Windows build path (not tested in this environment): use Python, Git, patch,
Make and GCC in MSYS2/MinGW, run `python tools/prepare_engine.py --mode native`,
then `make -C build/engine windows TARGET=LapisCube`. The product adapter uses
ClassiCube's portable socket API. The POSIX test probe and PTY smoke harness are
Linux test tools, not Windows application dependencies.

Classic direct connection remains `LapisCube USER MPPASS HOST PORT`; Classic/CPE
packet implementations are untouched. Lapis is selected only by `--lapis`.
No-argument startup displays usage instead of opening the upstream asset
downloader. No Mojang assets are bundled or fetched by the Lapis path.

## Limits and next work

Offline usernames are limited to 15 ASCII letters/digits/underscores because the
reference server truncates its 16-byte name field. UUIDv3 offline identity is
deterministic; it is not an authentication mechanism. Server checks are unchanged.

The backend deliberately supports the pinned Lapis offline, uncompressed profile.
It rejects encryption, compression, registry NBT payloads and unrecognized
configuration packets with a diagnostic. It is not a general vanilla 772 client.
Registry IDs/names are cached; tag payloads are validated but not retained as
gameplay rules. No built-in vanilla assets or complete vanilla registry definitions
are supplied by acknowledging the Lapis-specific core-pack profile.

Milestone 2 must add streamed chunk storage, paletted decoding, block mappings,
the first independent texture atlas and movement/collision integration. Inventory,
crafting, combat, entities, survival HUD, audio and packaging follow. No items or
world changes are fabricated locally. See the matrix for each unimplemented packet.
