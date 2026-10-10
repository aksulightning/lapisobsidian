# LapisCube

An experimental native C survival client for Lapis Obsidian, using ClassiCube's
renderer, input, windowing, audio, collision and UI infrastructure.

**Milestones 2–10 remain in progress. M8–M10 now have working streaming, entity-presentation and usability slices.** The native client now
renders a server world, moves and jumps, displays health/hunger and authoritative
item stacks, and has mining/placement and inventory controls. Real-server tests
pass mining, pickup/drop, placement, crafting, chest/furnace use, food, signs,
combat, death and respawn; advanced survival tests use an isolated seeded save. This is
an early playable development slice, not a finished survival release. A separate
fresh-world test starts empty and reaches a crafted pickaxe and mined cobblestone,
then verifies the earned inventory on reconnect. The next slice adds bounded
column updates, eight distinct original mob shapes, server-driven hit/swing/bow
effects, smaller-window layouts and an extracted-package native acceptance run.

The client uses an independent CC0 atlas, geometric models/UI and recorded CC0
gameplay sounds (with pitched note-block synthesis). It requires no Java, game JAR, Microsoft account or proprietary assets.
Original Classic/CPE packet code remains intact; `--lapis` selects the separate
Java protocol-772 backend. Compatibility is with the pinned Lapis profile, not
arbitrary vanilla servers.

## Sources and status

- ClassiCube: `d41c3f7eef2038f59702b58bdb373483fb0d28f9` (unmodified submodule).
- Lapis testing reference: `56300de744b9b859993f64d5c00d1637e42584c6`.
- Development branch: `testing-cube`; server source and checks are unchanged.

See [milestone status](docs/milestones.md), [packet matrix](docs/protocol-matrix.md),
[architecture](docs/architecture.md), [validation](docs/validation.md),
[assets](docs/assets.md), [first-tools guide](docs/survival-guide.md) and [notices](NOTICE.md).

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
its launcher, which locates textures, sounds and settings even when invoked from
another directory. Paths containing spaces are included in Linux acceptance.
The ZIP is in `build/`; `release.json` records revisions and every bundled file hash.
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
| Place / open container / use held item | Right mouse; hold to eat, release before the next use |
| Select hotbar | 1–9 or wheel |
| Inventory / crafting | B; Escape or B closes |
| Inventory operation | Left: stack, right: one, Shift: transfer |
| Equip from inventory | Hover an item and press 1–9 to swap with a hotbar slot |
| Drop from inventory | Hover and press Drop (G); Shift drops the stack |
| Drop one selected item | G |
| Sneak / sprint | Left Ctrl / Left Shift |
| Chat / command | T; type `/command` in chat |
| Respawn | Enter when health is zero and no other screen owns input |
| Edit sign | Right-click sign; Tab/Up/Down chooses line, Enter saves, Escape cancels |
| Read sign | Aim at it to show front/back text |
| Settings | Escape, existing ClassiCube options |

Mining has a progress bar and material/tool-dependent delays. These are initial
LapisCube pacing choices: the reference server specifies instant-break cases but
no timed hardness table. Releasing, changing target or changing tool cancels the
current request. Only server updates remove blocks or award drops. Crafting and
container clicks never predict or create items locally.

Signs support four lines per side. Text appears in an aimed-at overlay; it is not
yet painted onto the model. Wire text supports UTF-8 and modified UTF-8; the
engine font/input remains CP437. Unedited sign lines retain their original Unicode.

## Validation commands

```sh
make test
make sanitize
make integration
make survival
make progression
make travel
make native-smoke
make native package
python3 tests/windowed.py  # requires Xvfb, ImageMagick and unzip
```

`integration` starts the unchanged server in a temporary save with seed 42; its
headless probe shares the product decoders. `survival` generates a clearly
labelled saved-game fixture with supplies and workstations, then exercises two
clients against the unchanged server. It is not fresh-world progression evidence. `progression` instead begins in a
new seed-42 world with an empty inventory and no administrator token, harvests
natural logs, crafts/places a workbench, crafts/uses a pickaxe, and reconnects to
check the earned inventory. Its movement is a test input driver over received
terrain; it does not prove native collision or long-distance travel. `native-smoke` builds the existing
terminal/software renderer and checks actual engine world loading plus the
original Classic login. Rebuild `make native` before packaging a windowed client.
`travel` uses an isolated saved platform and an ephemeral administrator credential
through the normal `/admin` command. It checks signed chunk boundaries, both Far
Lands, eight mob types, equipment and damage. This is teleport/packet evidence,
not an unassisted survival journey. `windowed.py` extracts the Linux ZIP, launches
it outside its installation directory, resizes inventory, captures native combat
and terrain views, and records CPU/RSS/copy counts under `build/windowed-evidence`.
Packaging rejects the terminal build and wrong executable format; build objects
are separated by native/terminal/Windows mode. CI runs these gates and uploads
review screenshots alongside the development packages.

Set `lapis-entity-effects=false` in the installed `options.txt` before connecting
to disable hit/death tint, creeper flashing and attack-arm effects. Walking
animation and server-owned gameplay continue. Audio remains separately adjustable
through the existing engine settings.

On restricted runners without `/proc`, use
`make sanitize SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS`; this disables only leak
detection, retaining address/undefined-behavior checks.

## Current limits

The 7×7 chunk cache presents a 112×256×112 moving window. Server terrain and
block changes are authoritative. Cache recentering refreshes the dense map and
may hitch. Arriving columns now update at most two per tick and retain unaffected
meshes with Classic lighting; Fancy lighting conservatively refreshes all meshes. Lighting is an engine
approximation of the server's placeholder light arrays. Special shapes, water
levels, mob detail/metadata and item/projectile rendering need refinement.
Particles, remote armour and several entity effects are omitted. Creative/adventure/spectator
handling is partial. Progression beyond the first tools, inventory-full crafting-close
recovery, armour and long-distance survival walking remain unverified. Both Far
Lands have teleport and native render checks; this is not a long-session gate. Sky and
terrain shading follow server time; per-voxel modern lighting remains approximate.
The pinned server can broadcast Play packets to another connection that is still
configuring during simultaneous joins; sequential joins are the tested path.
The client does not accept Play packets in Configuration to hide that server issue.

The offline, uncompressed server profile rejects encryption, compression,
unexpected registry NBT and unsupported configuration semantics explicitly.
No server security checks have been relaxed. Offline UUIDs are deterministic
identities, not authentication. See the matrix for exact coverage.

## WebAssembly browser client

Build the C client for desktop browsers and landscape fullscreen touch play with
`make web` (Emscripten 4.0.23). From the repository root,
`LAPIS_ENABLE_WEBCLIENT=1 ./build.sh` builds the browser package and optional HTTP /
WebSocket host. The default server build remains TCP-only. See
[web build and hosting](docs/web.md) for packaging, HTTPS, controls and tests.
