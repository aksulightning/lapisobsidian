# Architecture and implementation plan

## Inspection before implementation

ClassiCube's `doc/modules.md`, `doc/style.md`, `Server.c/.h`, `Protocol.c/.h`,
`main_impl.h`, `Platform.h`, `Platform_Posix.c`, renderer/world/input APIs and
Makefiles were reviewed at the pinned revision. Its separate C modules, fixed-size
types and component lifecycle provide appropriate extension points.

Lapis `main.c` dispatch, `packets.c`, `packet_input.c`, `procedures.c`, registry
generator/snapshot, inventory, interaction, entity, command, sign, door, circuit,
fluid and farming modules were inspected. Its testing branch, not a generic
vanilla packet table, is the authority for this client profile.

The implementation plan recorded before coding was: pin both sources; document
actual packets; add a separate bounded parser and nonblocking connection path;
test the unmodified server; integrate through the existing `Server` interface;
preserve Classic packet code; publish verified milestone-1 work on `testing-cube`.

## Reuse and boundaries

| Module | Role / decision |
| --- | --- |
| `LapisProtocol` | Socket-independent framing, state transitions, registry identifiers, Play login, teleport/keepalive acknowledgment, counters and errors |
| `LapisSession` | Nonblocking partial I/O, outbound queue consumption, timeouts and per-tick budget; shared by product and test probe |
| `LapisIdentity` | Offline UUIDv3 using the engine's existing BearSSL MD5; no new crypto dependency |
| `LapisBackend` | ClassiCube `Server` adapter; platform sockets, clock, loading UI, reset/close lifecycle |
| `engine/src/Protocol.c` | Original Classic/CPE implementation, unchanged |
| `patches/engine.patch` | Explicit Lapis argument, backend selection/cleanup, usage screen instead of asset-downloading launcher |
| `tools/probe.c` | POSIX automated integration driver around the same protocol/session, not a separate gameplay implementation |
| Future world module | Bounded server chunk cache and palette mapping; no terrain generation |
| Future presentation modules | Server-owned inventory, health, entities and interactions; submit actions and apply authoritative results |

Retain `Graphics`, `Builder`, `MapRenderer`, `TexturePack`, `Input`, `Window`,
`Audio`, `Gui`, `Entity` and `Model` where practical. ClassiCube's `World` is a
finite dense array, whereas Lapis streams a moving chunk window. M2 must introduce
a bounded chunk adapter/origin translation rather than allocate the whole world
or pretend that unreceived terrain exists. Keep world coordinates separate from
render coordinates to accommodate server Far Lands and negative chunk coordinates.

ClassiCube's inventory currently represents creative blocks, not survival item
stacks. Add a separate server-owned inventory model; do not reuse creative local
edits as authoritative inventory. `Game_ChangeBlock` is optimistic and must not
be called for Lapis survival actions until prediction/rollback is explicitly
implemented. M1 prohibits block actions and stays on the loading screen.

## Resource bounds and behavior

- One 512 KiB incoming frame buffer, 32 KiB outbound queue; no allocation per packet.
- At most 32 registries, 2,048 entries and 64 KiB of identifier storage.
- Inbound work per pump at most 256 KiB; scratch reads 16 KiB. Writes preserve
  unsent suffixes and never sleep waiting for the peer.
- Frame length is at most three VarInt bytes; values must be in 1..524288.
  Five-byte field VarInts reject overflow beyond 32 bits.
- Connection/login deadline 15 seconds, incomplete frame deadline 15 seconds,
  idle receive deadline 30 seconds. Queue overflow fails closed.
- Unsupported configuration semantics fail explicitly. Unimplemented Play frames
  are bounded and counted as skipped; they cannot trigger gameplay mutations.
- EOF in a frame fails; close/reset clears protocol state and the platform socket.
- Native DNS resolution currently uses synchronous `Socket_ParseAddress` and only
  the first resolved address, like the upstream connection. Numeric IPs avoid DNS
  stalls. Async DNS/address fallback remains a documented transport improvement.
- The Lapis backend adds one fixed session under 640 KiB, including registry
  storage, even before chunks are stored. Tiny-console memory tuning is not claimed.

## Milestones and acceptance gates

1. **Protocol foundation (this change):** actual status/login/configuration/Play
   connection, stored registry names, bounded diagnostics and framing tests.
   Full vanilla registry semantics and arbitrary vanilla-server compatibility are
   outside this profile.
2. **World exploration:** decode all 24 wire sections (-64..319); display terrain
   Y=0..255; bounded moving chunk window; actual mapped blocks; test negative
   boundaries, palettes, lighting, updates, teleports and disconnect cleanup.
   Emit movement only after collision/world synchronization is implemented.
3. **Basic interaction:** targeting, mining/placement sequences, server-selected
   hotbar, inventory slots, dropped items; server remains authoritative.
4. **Survival:** health/hunger, stacks, server crafting/containers, models for the
   eight supported mobs, players/items/projectiles, targeting, damage/death/food,
   mode-specific controls. Exercise actual gameplay manually.
5. **Lapis-specific:** oriented signs/doors, state overlays for circuits/trapdoors/
   plates/crops/farmland/fluids, named sound holders and MIDI/musicbox sounds.
   No client-side MIDI gameplay engine: reproduce the server's note events.
6. **Packaging:** reviewed CC0 atlas/icons/UI/models/sounds and manifest; settings;
   Linux and Windows builds; license/provenance review; clean-install and manual
   survival sessions. Do not mark this complete based on compilation.

Every stage must keep a Classic regression check and distinguish decoded packets
from rendered, interacted-with and manually validated behavior.
