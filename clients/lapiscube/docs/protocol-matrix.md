# Lapis protocol compatibility matrix

Authority: Lapis testing commit `56300de744b9b859993f64d5c00d1637e42584c6`.
Version constant: `include/protocol.h`: Java 1.21.8 / 772.
All IDs below are hexadecimal and state/direction-specific.

Connection foundation is tested against the real server. Gameplay entries
distinguish implemented parsing/presentation from real-server interaction evidence.
**Deferred** means safely frame-skipped or not sent. See validation.md for actual
acceptance results; implementation alone is not proof that gameplay works.

## Framing and connection

TCP carries VarInt frame length, VarInt packet ID, then packet payload. Integers
and floats inside payloads are big-endian unless encoded as VarInts. The pinned
server has no encryption/compression negotiation. `packet_input.c` bounds inbound
server frames and each gameplay handler validates its own input; none is changed.

| State | Direction | ID | Server function / data | LapisCube |
| --- | --- | --- | --- | --- |
| Handshake | C→S | 00 | `cs_handshake`: protocol VarInt, host string, u16 port, intent 1/2 | Implemented, 772 |
| Status | C→S | 00 | Empty request | Implemented |
| Status | S→C | 00 | `sc_statusResponse`: bounded JSON string, version + description (no players field required) | Implemented |
| Status | C→S / S→C | 01 | Opaque 8-byte ping / exact pong; server closes afterward | Implemented, token validated |
| Login | C→S | 00 | `cs_loginStart`: name + 16-byte UUID; name storage allows 15 visible bytes | Implemented, deterministic offline UUIDv3 |
| Login | S→C | 02 | `sc_loginSuccess`: UUID, name, zero properties | Implemented; identity and empty property list validated |
| Login | C→S | 03 | Login acknowledged, empty | Implemented before configuration info |
| Configuration | C→S | 00 | `cs_clientInformation`: locale, i8 view, chat mode/colors, layers, hand, filter/listing, particle status | Implemented with conservative defaults |
| Configuration | S→C | 0E | `sc_knownPacks`: exactly minecraft/core/1.21.8 | Implemented; mismatch fails |
| Configuration | C→S | 07 | `cs_knownPacks`: exact matching one-pack response required | Implemented without bypassing server validation |
| Configuration | S→C | 01 | `sc_sendPluginMessage`: optional brand string | Channel validated; opaque remainder ignored, no downloads |
| Configuration | C→S | 02 | `cs_pluginMessage`: optional client brand | Not needed, not sent |
| Configuration | S→C | 07 | `sc_registries` / `registries_bin`: key, count, entry key + has-NBT=false | Implemented, names retained in wire ID order |
| Configuration | S→C | 0D | `tags_bin`: registries, tag names, arrays of VarInt IDs | Structurally validated; membership use deferred |
| Configuration | S→C / C→S | 03 | Finish configuration / acknowledgment | Implemented only after required registries and tags |

The 11 registry groups are cat/chicken/cow/frog/painting/pig/wolf-sound/wolf
variants, damage types, worldgen/biome, dimension_type. There are 68 entries in
this revision (8 variants + 49 damage types + 10 biomes + 1 dimension). Server
identifiers may omit `minecraft:`. The cache normalizes that default namespace
for lookup/duplicate checks without assuming modern vanilla numeric biome IDs.

The only dimension type is overworld at local ID 0; its wire-height convention
is -64..319. Configuration has **no feature-flags packet** in this revision.
Defensive handlers validate 0C feature-name lists, echo configuration 04 keepalive
and 05 ping, and fail on disconnect. These defensive handlers are not evidence
that the Lapis server emits those packets. There is no full NBT registry parser
or vanilla core-pack asset bundle. Acknowledgment selects this documented Lapis
identifier-only compatibility profile; unexpected registry NBT fails closed.

## Play: server to client

Source is `src/packets.c` unless another source is listed.

| ID | Packet / source | Important wire behavior | Client status |
| --- | --- | --- | --- |
| 2B | `sc_loginPlay` | i32 entity ID, dimension-key list, distances, dimension type/name, seed, modes, death/portal/sea/chat flags | Implemented, validated and stored |
| 41 | `sc_synchronizePlayerPosition` | Teleport VarInt **-1**, XYZ doubles, velocity doubles, yaw/pitch floats, i32 flags=0 | Absolute synchronization + ack and native position/origin update |
| 26 | `sc_keepAlive` | Opaque i64 (currently zero), once per second | Implemented, exact echo |
| 27 | `sc_chunkDataAndUpdateLight` | i32 X/Z, empty heightmaps, sized section payload, block entities, light masks/arrays | All 24 sections validated; Y=0..255 cached/rendered; real 25-chunk world tested |
| 57 | `sc_setCenterChunk` | Signed chunk X/Z VarInts | Bounded cache center, eviction and native origin shift; boundaries unit-tested |
| 5A | `sc_setDefaultSpawnPosition` | Packed position + angle | Deferred M2 |
| 08 | `sc_blockUpdate`; signs/doors/circuits/farming/fluid overlays | Packed position + modern state VarInt | Authoritative cache/native updates; real mining and placement tested |
| 04 | `sc_acknowledgeBlockChange` | Action sequence VarInt | Sequence acknowledgment retained; server decides mutations |
| 39 | `sc_playerAbilities` | Flags, flying/walking speeds | Parsed; initial flying permission/mode controls, incomplete mode UX |
| 22 | `sc_startWaitingForChunks`, `command_packets.c` | Event 13 waiting for chunks; event 3 gamemode change | Gamemode stored; native world load governs loaded transition |
| 6A | `sc_updateTime` | Two i64 clocks + day-tick boolean | Validated server clock; quantized native sky/fog/sun/shadow shading; real day/night commands inspected |
| 14 | `sc_setContainerSlot` | Window/state/slot + component-aware item stack (zero components here) | Atomic server-owned stacks, including direct window -2; mirrored container/player views; crafting/chest/furnace scenarios passed |
| 59 | `sc_setCursorItem` | Item stack for carried cursor | Authoritative cursor; real transfer tested |
| 62 | `sc_setHeldItem` | Selected slot byte | Selected slot synchronization and native hotbar |
| 34 | `sc_openScreen` | Window ID, menu ID, NBT title | Native player/crafting/chest/furnace grids; player-slot aliases seeded on open; real container scenario passed |
| 61 | `sc_setHealth` | Health float, food VarInt, saturation float | Health/food/saturation state and HUD; saturation can be -0.4 in Lapis; eating and lethal damage verified |
| 4B | `sc_respawn` | Dimension/mode/death/portal/sea/data-kept fields | Validated reset and native reload path + Enter request; two-client lethal damage/respawn passed |
| 3F | `sc_playerInfoUpdateAddPlayer`, `command_packets.c` | Add-player + mode or mode-only action mask | Deferred M4 |
| 01 | `sc_spawnEntity` | Entity ID, UUID, type, XYZ, angles, object data, velocity | Bounded snapshots and original shared native models; species detail incomplete |
| 5C | `sc_setEntityMetadata`, `mob_packets.c` | Typed indexed metadata terminated by FF; items, pose, creeper fuse, arrows | Lapis metadata types 0/1/7/8/21 parsed; partial visual application |
| 02 | `sc_entityAnimation` | Entity ID + animation | Deferred M4 |
| 1F | `sc_teleportEntity` | Entity position/velocity/angles/grounded synchronization | Native interpolated entity position/rotation |
| 2F | `sc_mob_move`, `mob_packets.c` | i16 deltas at 1/4096 block, yaw/pitch, grounded | Signed fixed-point deltas decoded and interpolated; unit tests |
| 31 | `sc_updateEntityRotation` | Entity ID, yaw/pitch bytes, grounded | Rotation synchronization |
| 4C | `sc_setHeadRotation` | Entity ID + head yaw | Head rotation retained; model animation limited |
| 5F | `sc_mob_equipment`, `mob_packets.c` | Skeleton main-hand bow stack | Deferred M4 |
| 19 | `sc_damageEvent` | Entity ID, damage registry ID, sources, optional position | Deferred M4 |
| 1E | `sc_entityEvent` | i32 entity ID + status | Deferred M4 |
| 46 | `sc_removeEntity` | VarInt count/IDs | Native entity removal and fixed slot reuse |
| 75 | `sc_pickupItem` | Collected ID, collector ID, count | Deferred M3/M4 |
| 72 | `sc_systemChat` | Anonymous NBT TAG_String using modified UTF-8 + overlay boolean | Validated modified UTF-8 → UTF-8 → engine CP437; UTF-8 wire text retained, font repertoire limited |
| 10 | `sc_commands`, `command_packets.c` | Brigadier node graph | Deferred M4; command strings need not use this graph to be sent |
| 06 | `sc_sign`, `sign_packets.c` | Packed position, sign block-entity type 7, bounded sign NBT | Exact Lapis NBT profile, 128-sign cache, native reading overlay; Unicode echo verified |
| 35 | `sc_signEditor`, `sign_packets.c` | Packed position + front/back boolean | Native four-line editor; no speculative cached text mutation |
| 6E | `mob_packets.c`, `notes.c` | Inline named sound holder=0, optional range, category, fixed XYZ, volume/pitch/seed | Inline name/category/position/volume/pitch parsed; original synthesis including note timbres; listening QA pending |
| 29 | `sc_firecracker`, `mob_packets.c` | Particle 29 firework, position/spread/speed/count | Deferred M4/M5 |

No chunk-unload, chunk-batch, standalone update-light, recipe synchronization or
feature-flags emission was found in the inspected revision. Do not invent them
as Lapis requirements. A bounded client cache still needs eviction on center
changes. Creeper behavior is the server's non-destructive firework burst, not a
vanilla explosion. Plates additionally use ghast, zombified piglin and fireball
entities and multiple world keys; generic entity/world reset paths exist, but
plate travel and those additional species remain unverified.

### Chunk layout facts for milestone 2

`sc_chunkDataAndUpdateLight` sends 24 sections: four single-state bedrock sections
below Y=0, then twenty sections starting at Y=0. The meaningful survival terrain
is Y=0..255. Each section has a u16 non-air count, block paletted container and
biome paletted container. Below-zero sections use bits=0/state=85. Other sections
use bits=8, palette count=256 and palette VarInts, then 4096 packed bytes. Each
biome container is single-valued (bits=0 and server-local biome ID).

This protocol revision omits the old explicit paletted-container long-array
length. Packed word counts are inferred from bits/entry and section size.
Refer to `tests/chunk_packet.c` before decoding word ordering: wire bytes are
big-endian 64-bit containers, not an arbitrary flat voxel order. The decoder also has automated tests for every valid block palette bit width
(0 and 4..15), truncation and bounds.

The server emits no initial block entities, and sends sign/entity and oriented
block-state overlays after each chunk. Light uses 26 sky arrays, with the first
eight dark and the next eighteen full bright; no block-light arrays are sent.
The client validates these arrays and uses ClassiCube terrain shading as an
explicit approximation; it does not claim modern light-level parity. No terrain
is generated locally and wire registry IDs are kept separate.

## Play: client to server

Authoritative dispatch: `src/main.c:handlePacket`.

| ID | Server handler | Fields / constraints / use | Client status |
| --- | --- | --- | --- |
| 00 | No Play branch in case 00 | Teleport acknowledgment VarInt is consumed/discarded by current dispatch | Sent correctly by M1; no claim server verifies it |
| 1B | Dispatch discards 8-byte payload | Keep-alive response, currently ignored by server | Implemented |
| 2B | `cs_playerLoaded` | Empty; completes join and entity/inventory synchronization | Native and probe send after decoded terrain and both spawn teleports |
| 1D | `cs_setPlayerPosition` | XYZ doubles + flags, exactly 25 bytes | Deferred M2 |
| 1E | `cs_setPlayerPositionAndRotation` | XYZ doubles + yaw/pitch floats + flags, exactly 33 bytes | Native position/rotation/ground state at tick rate; actual movement smoke |
| 1F | `cs_setPlayerRotation` | Two floats + flags, exactly 9 bytes | Deferred M2 |
| 20 | `cs_setPlayerMovementFlags` | One flags byte | Deferred M2 |
| 29 | `cs_playerCommand` | Entity VarInt, action byte, boost VarInt; server actions **1/2 set/clear sprint** | Native sprint action 1/2 using Lapis-specific semantics |
| 2A | `cs_playerInput` | One flag byte; bit 20 hex controls sneaking | Native sneaking bit 20 hex |
| 28 | `cs_playerAction`, `sign_packets.c` | Action 0..6, packed position, face, sequence; mining/drop | Start/abort/finish mining, drop-one, release-use; mining/drop real-server tested |
| 3F | `cs_useItemOn`, `sign_packets.c` | Hand, position, face, three cursor floats, two booleans, sequence | Native cursor/face/position action; placement real-server tested |
| 40 | `cs_useItem`, `sign_packets.c` | Hand, sequence, yaw/pitch; finite angles, pitch ±90; food/bucket use | One request per native press, release action 5; held food verified in real-server scenario and native window; buckets unverified |
| 34 | `cs_setHeldItem` | u16 slot, exactly 2 bytes | Native hotbar selection; real interaction test |
| 11 | `cs_clickContainer`, `inventory_packets.c` | Window/state, clicked i16 slot, button/mode, bounded changed slots, cursor stack | Zero predictions, absent cursor HashedSlot; server-owned results; real inventory transfer tested |
| 12 | `cs_closeContainer` | One window byte | Sent on UI close; real inventory test |
| 37 | `cs_creativeSlot`, `command_packets.c` | Slot + stack; gated by living/loaded/creative mode | Deferred M4; never sent for survival item creation |
| 19 | `cs_interact`, `mob_packets.c` | Entity VarInt, action, optional hit position/hand, sneaking | Attack action + native ray targeting; two actual clients verified lethal sword damage and respawn |
| 3C | `cs_swingArm` | Hand VarInt | Deferred M4 |
| 0B | `cs_clientStatus` | Action 0 requests respawn | Enter on death requests respawn; real-server death/respawn scenario passed |
| 06 / 07 | `cs_chatCommand`, `command_packets.c` | Unsigned/signed command forms with strict payload checks | Unsigned 06 command text implemented; signed 07 not sent |
| 08 | `cs_chat`, `command_packets.c` | Text with timestamp/salt/signature/ack fields, bounded validated UTF-8 | Validated UTF-8 chat; native CP437 input encoded to UTF-8; real ASCII chat round trip |
| 3B | `cs_updateSign`, `sign_packets.c` | Position, front/back, four strings | Bounded UTF-8 lines; placement/edit/server echo tested, including surrogate-pair NBT |
| 0C | Dispatch ignored | Client tick | Not sent |

Position validation rejects NaN/Inf, enforces the server's Y limits and runs world
border/plate guards. Mining, placement, containers, admin commands and creative
slots retain their existing security and mode checks. This change modifies no
server dispatch, validation, authentication or gameplay code.

## Reference-server limitation observed in acceptance

When two clients join simultaneously, `procedures.c:handlePlayerJoin` may broadcast
Play chat/player/entity packets to the other connection before it leaves
Configuration. The client correctly rejects the unexpected state. The two-client
survival harness waits for the first loaded notification before joining the next
client; no security/state checks were weakened. Concurrent-join correction belongs
in the reference server and is outside this client change.
