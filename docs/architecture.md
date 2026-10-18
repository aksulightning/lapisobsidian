# Source map

The server keeps a flat `src/` directory and its existing `include/` interfaces.
Files are grouped by responsibility and named after the behavior they own.
Start with the relevant row rather than reading the entire server.

| Responsibility | Implementation |
| --- | --- |
| Process loop and simulation cadence | `main.c`, `server_tick.c` |
| Configuration, world startup and final saves | `server_startup.c` |
| TCP listener and browser/local acceptance | `server_network.c` |
| ESP-IDF Wi-Fi and task entry | `esp_main.c` |
| Framed input, encrypted IO and wire primitives | `packet_input.c`, `transport.c`, `wire.c` |
| Packet routing and movement validation | `packet_dispatch.c`, `player_movement.c` |
| Handshake, login and configuration codecs | `login_packets.c` |
| World/chunk and player movement codecs | `world_packets.c`, `movement_packets.c` |
| Entity codecs | `entity_packets.c`, `mob_packets.c` |
| Container/lifecycle updates and inventory actions | `inventory_updates.c`, `inventory_packets.c` |
| Player connection lifecycle and spawn metadata | `player_session.c`, `player_spawn.c` |
| Inventory slots, stack/armor/food rules | `inventory_slots.c`, `item_rules.c` |
| Block edits, collision and replacement rules | `block_changes.c`, `block_rules.c` |
| Mining, item use and combat | `mining.c`, `player_use.c`, `combat.c` |
| Dropped-item simulation and file format | `items.c`, `item_storage.c` |
| Drop transactions and durable filesystem operations | `drop_store.c`, `durable.c` |
| Authenticated sessions and profile validation | `auth.c`, `auth_profile.c` |

`packets.h` and `procedures.h` remain the shared codec/gameplay interfaces so
existing callers do not need forwarding wrappers. `packet_dispatch.h` and
`server_runtime.h` describe the process boundaries; `items_internal.h` is private
to item simulation and storage. Packet formats, save formats, feature flags and
transaction ordering are unchanged by this reorganization.

The LapisCube client remains in `clients/lapiscube/`. `LapisGui.c` owns inventory
and sign-screen input/layout, while `LapisGuiDraw.c` owns shared drawing, cached
text textures and the survival HUD. The pinned engine stays unmodified; staging
copies the integration sources before applying the existing patches.

Source files target 120–250 lines where responsibilities permit it. Generated
registry/asset tables and tests are not split to meet an arbitrary line count.
Unrelated legacy subsystems keep their existing organization.

Native shell builds and standalone WebAssembly compile `src/*.c`; ESP-IDF uses
the source glob in `src/CMakeLists.txt`. The client staging step includes all
client `.c` and `.h` files. Focused integration fixtures in `tests/run.sh` have
explicit link inputs and must be updated when their implementation moves.

CI runs the four drops/authentication combinations, regression tests and
sanitizers, native client packaging, multiplayer browser acceptance and both
standalone persistence modes. `tests/packet_dispatch.c` specifically exercises
movement frame validation and flags/rotation-only early returns.
