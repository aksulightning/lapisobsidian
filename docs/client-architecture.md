# Lapis Obsidian Client architecture decision

Inspection baseline: `testing`, `e96af88` (Use explicit plate create and remove
subcommands). The requested client is additive; the inspected project is a C
server. No existing client renderer or device-input implementation was found.

## Inspection and reuse map

| Concern | Existing implementation | Client decision |
| --- | --- | --- |
| Build | `build.sh`, `build-alpine.sh`, `tests/run.sh`; C compiler and libm. `src/CMakeLists.txt` is ESP-IDF-specific. Optional Node registry generator. | Preserve all server build paths. Add isolated npm/Cargo workspace. |
| TCP | `src/main.c` nonblocking POSIX/Winsock accept loop; `src/tools.c` socket reads/writes; `src/packet_input.c` 8 KiB bounded serverbound frame buffer and 15-second partial-frame deadline. | Native bridge is independent. Browser uses only WebSocket. Do not link server listeners into the application. |
| Protocol | `include/protocol.h`: 772 / 1.21.8. `src/packets.c`, `src/main.c` dispatch; VarInt framing and endian codecs in `src/varnum.c`, `src/tools.c`. | Generate shared constants now. Evaluate memory-buffer codec extraction and WASM/native adapters in M3; socket-bound server handlers cannot be imported directly into TypeScript. |
| Session/auth | `cs_handshake`, `cs_loginStart`, `sc_loginSuccess`, configuration, play; caller-supplied UUID/name and no encryption/authentication. | Local bridge authentication is independent of future remote player authentication. No online account login invented. |
| Registry | `generated/registry_snapshot.json`, generated C arrays, `build_registries.js`, `docs/registries.md`; 256 compact block IDs and network mappings. Exact core-pack negotiation. | Reuse snapshot and fixtures; add client-side semantics only when needed. Never import proprietary assets. |
| World/chunks | `worldgen.c`, `beta173_*`, plates and edit storage; `sc_chunkDataAndUpdateLight` emits 24 sections, -64 minimum Y, palettes and lighting. `tests/chunk_packet.c` validates wire output. | Future incoming chunk decoder and mesher consume actual packets. Server generation/storage is not browser world state. M1 scene is explicitly procedural test geometry. |
| Rendering/audio | No client renderer, textures or playback engine. Server emits named sound packets and has MIDI/musicbox logic. | WebGL and original procedural geometry now; independent legal sound assets later. |
| Entities/player | `PlayerData`, `MobData`, `WorldState` in `globals.h`; `procedures.c`, `mobs.c`, `mob_packets.c`, `items.c`. | Wire fixtures and state semantics are references; packed server persistence structs are not client state models. |
| Inventory | `inventory_packets.c`, `crafting.c`, server-owned slots and predictions; security tests. | Future UI displays authoritative remote inventory and emits compatible requests; no inventory simulation in M1. |
| Input | Server interprets movement/action packets in `main.c` and `packets.c`; no keyboard/mouse/touch collection. | Shared abstract input frames from browser devices; renderer consumes free-camera movement in M1. |
| License | GNU GPL v3, upstream bareiron and compatibility/terrain notices. | Preserve LICENSE/NOTICE. New client source and original assets GPL-3.0-only. No asset scraping. |

## Runtime evaluation

| Candidate | Size / WebView | Bridge and platforms | Decision |
| --- | --- | --- | --- |
| Tauri 2 + Rust | System WebView; avoids shipping a browser engine. Actual package size must be measured per platform. | Memory-safe embedded bridge; Windows/Linux/macOS and a separately validated Android/iOS path. Requires Rust and native SDK/WebKit dependencies. | Chosen. Small explicit API surface; frontend has no native IPC permissions. |
| Electron + Node | Ships Chromium and Node; larger runtime, consistent browser behavior. | Mature desktop sockets and packaging; no direct mobile application path. | Not selected; size and future mobile strategy favor system WebViews. |
| C/C++ + thin WebView | Potentially small and close to repository language. | Requires maintained WebSocket/TLS/HTTP dependencies, native lifecycle integration and platform-specific packaging. | More hand-maintained security and platform code than justified by reusable server code. |
| Neutralino + extension | System WebView and lightweight core. | Separate bridge extension lifecycle and IPC; mobile strategy needs additional work. | Fewer benefits than an in-process Rust bridge for this task. |

No frontend framework or rendering library was added. Native dependencies are
MIT/Apache-style permissive Rust libraries; maintain their license notices in
release distributions. npm packages are build/test tools, not runtime services.

## Directory boundaries

- `client/web/`: static UI, settings, WebGL, abstract input and browser transport.
- `client/scripts/`: source-of-truth constant generation, private-pipe developer launcher.
- `client/bridge/`: embedded static assets, authenticated local WS session, cancellation.
- `client/src-tauri/`: native window, secure bootstrap injection, ownership and exit.
- `client/tests/`, `client/bridge/tests/`: browser and security/lifecycle checks.
- `client/dist/`, `client/target/`: ignored build outputs; no copied server or game assets.

Protocol codecs will sit above the `Transport` interface (connect/send/close and
callbacks). A future native transport can implement the same contract. The
bridge is concerned with bounded transport and protocol admission, never
rendering, world simulation or inventory logic. TCP framing does not align with
WebSocket frames; later codecs must incrementally assemble split/coalesced bytes.

## Milestone progression

M1 provides the standalone shell, real WebGL scene and authenticated local bridge.
M2 adds remote TCP lifecycle, bounded backpressure, DNS/connect deadlines, target
policy and a protocol-specific admission gate before forwarding bytes. M3
integrates handshake/login/configuration codecs using inspected packet fixtures.
M4 expands the unified input to gameplay actions and HUD. M5 integrates remote
chunks, meshing, camera/player movement and unloading. M6 adds supported gameplay.
M7 validates installers, signing/distribution, performance and mobile feasibility.
No milestone beyond M1 is represented as complete by this change.

Sources: [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/),
[Tauri WebViews](https://v2.tauri.app/reference/webview-versions/),
[capabilities](https://v2.tauri.app/security/capabilities/),
[Neutralino architecture](https://neutralino.js.org/docs/contributing/architecture/).
