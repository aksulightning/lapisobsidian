# LapisCube

An experimental native C survival client for Lapis Obsidian, using ClassiCube's
renderer, input, windowing, audio, collision and UI infrastructure.

**Milestones 2–6 have started; they are not complete.** The native client now
renders a server world, moves and jumps, displays health/hunger and authoritative
item stacks, and has mining/placement and inventory controls. A real-server test
passes mining, pickup, cursor transfers, dropping, placement and chat. This is
an early playable development slice, not a finished survival release.

The client uses an independent CC0 atlas, geometric models/UI and synthesized
sounds. It requires no Java, game JAR, Microsoft account or proprietary assets.
Original Classic/CPE packet code remains intact; `--lapis` selects the separate
Java protocol-772 backend. Compatibility is with the pinned Lapis profile, not
arbitrary vanilla servers.

## Sources and status

- ClassiCube: `d41c3f7eef2038f59702b58bdb373483fb0d28f9` (unmodified submodule).
- Lapis testing reference: `56300de744b9b859993f64d5c00d1637e42584c6`.
- Development branch: `testing-cube`; server source and checks are unchanged.

See [milestone status](docs/milestones.md), [packet matrix](docs/protocol-matrix.md),
[architecture](docs/architecture.md), [validation](docs/validation.md),
[assets](docs/assets.md) and [notices](NOTICE.md).

## Build, install and connect

Linux needs Git, GCC/Clang, Make, Python 3.8+, `patch`, and X11/Xi/OpenGL development
libraries (`libx11-dev libxi-dev libgl-dev` on Debian/Ubuntu). Pillow is needed
only to regenerate assets, not to build or run from the committed bundle.

```sh
git clone --branch testing-cube --recurse-submodules https://github.com/aksulightning/lapisobsidian.git
cd lapisobsidian
./build.sh
cd clients/lapiscube
make native package
cd build/LapisCube-linux
./connect.sh MyName 127.0.0.1 25565
```

Start the separate Lapis server before connecting. Use 1–15 ASCII letters, digits
or underscores for the offline username. Extract the entire package; launch from
its directory so textures, sounds and settings are found. The ZIP is in `build/`.
No-argument startup prints usage rather than opening an asset downloader.

For a staged developer build, run from `build/engine`:
`./LapisCube --lapis MyName HOST PORT`.
Classic direct connection remains `./LapisCube USER MPPASS HOST PORT`.

Windows cross-build on Linux requires `gcc-mingw-w64-x86-64`:

```sh
make windows
python3 tools/package.py --platform windows
```

Extract the Windows ZIP and run `connect.bat MyName HOST PORT`. A CI workflow
builds both packages. Windows runtime acceptance is outstanding; see validation
for actual build results. Other ClassiCube ports have not been validated with
these desktop-oriented modules.

## Controls

| Action | Default |
| --- | --- |
| Move / look / jump | WASD / mouse (arrow keys also available in package) / Space |
| Mine / attack | Hold left mouse / target entity and click |
| Place / open container / use held item | Right mouse |
| Select hotbar | 1–9 or wheel |
| Inventory / crafting | B; Escape or B closes |
| Inventory operation | Left: stack, right: one, Shift: transfer |
| Drop one selected item | G |
| Sneak / sprint | Left Ctrl / Left Shift |
| Chat / command | T; type `/command` in chat |
| Respawn | Enter when health is zero and no other screen owns input |
| Settings | Escape, existing ClassiCube options |

Mining currently uses a fixed 650 ms request interval; the server decides whether
the action is allowed and supplies block/item results. Tool-dependent progress
and mining animation are unfinished. Crafting and container grids submit clicks
without predicted item changes. Food/use/release and attack/respawn requests are
wired, but their full gameplay loops have not passed acceptance testing.

## Validation commands

```sh
make test
make sanitize
make integration
make native-smoke
```

`integration` starts the unchanged server in a temporary save with seed 42; its
headless probe shares the product decoders. `native-smoke` builds the existing
terminal/software renderer and checks actual engine world loading plus the
original Classic login. Rebuild `make native` before packaging a windowed client.
On restricted runners without `/proc`, use
`make sanitize SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS`; this disables only leak
detection, retaining address/undefined-behavior checks.

## Current limits

The 7×7 chunk cache presents a 112×256×112 moving window. Server terrain and
block changes are authoritative. Cache recentering refreshes the dense map and
may hitch; efficient incremental remeshing remains work. Lighting is an engine
approximation of the server's placeholder light arrays. Special shapes, water
levels, mob distinctions/metadata and item/projectile rendering need refinement.
Signs have no text editor yet; particles and several entity effects are omitted.
Creative/adventure/spectator handling is partial. Full crafting, furnace,
combat, food, death and Far Lands sessions remain unverified. Unicode chat still
needs proper modified-UTF-8-to-engine-font conversion.

The offline, uncompressed server profile rejects encryption, compression,
unexpected registry NBT and unsupported configuration semantics explicitly.
No server security checks have been relaxed. Offline UUIDs are deterministic
identities, not authentication. See the matrix for exact coverage.
