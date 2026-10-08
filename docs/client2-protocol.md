# Alternative client protocol compatibility

Source verification: `src/main.c`, `src/packets.c`, `src/procedures.c`,
`src/registries.c` and `include/registries.h`. The native server speaks protocol
772, advertises Java 1.21.8, negotiates the built-in `minecraft:core:1.21.8`
known pack, sends compact registry names with absent NBT data, and uses offline
name/UUID login. This client does not change those formats. Upstream protocol
47 NetworkManager/login handlers are excluded from the reachable build.

`client2/adapter/wire.mjs` implements big-endian numbers, signed VarInts, packed
block positions, length-framed streaming and the exact server compact section
palette format (single-state or 256-entry byte palette, reversed bytes within
64-bit words). Messages need not align to packet boundaries. Buffer and packet
limits reject malformed data. The core pack/version and registries are checked.

| State | Supported behavior |
| --- | --- |
| Handshake/login | Version 772, offline username/UUID, login success/acknowledgement |
| Configuration | Client information, known pack, compact registry records, finish/acknowledgement |
| Play inbound | Join, absolute teleport/confirmation, chunk sections/biomes, block update, keepalive, slot/cursor/hotbar, health/food, system chat, game mode, respawn, container open, entity spawn/position/deltas/removal, time |
| Play outbound | Loaded indication, position/look/ground flags, input/sprint, swing, dig/use/place, selected slot, creative stack, container click/close, chat/command, respawn request |

The adapter translates upstream packet objects to current wire packets; it
never sends protocol-47 packets to the server. Rendering IDs and item IDs are
mapped explicitly; no proprietary assets are tied to protocol identifiers.
Edits appear only upon server updates. Server inventory updates drive hotbar
contents. Unknown items use a labeled original fallback preview; unsupported
components fail explicitly. Unknown packet IDs are ignored, rather than
misparsed as unrelated packets.

Current limits: intended for this server snapshot only; no compression,
encryption, Microsoft authentication, arbitrary registry NBT, relative teleport
flags, resource packs, skin fetching, signed chat or external servers. Non-player
entities currently use an original neutral humanoid model; metadata, item entity
models, equipment, player-list entries and complex animations are incomplete.
Additional blocks use generic cubes/translucent cubes, so doors/slabs and other
complex collision shapes are approximate. Server lighting arrays are not applied;
the upstream ambient/skylight model remains. Survival digging uses a conservative
750 ms finish request; server tool durability/break timing remain authoritative.
Container clicks support left click and cursor state; shift/right-click crafting
UI is not yet exposed. Creative blocks and ordinary slot synchronization work.
Touch menus are retained upstream; mode 2 has no full mobile movement controls.
Mode 1 remains the supported touch client.

`tests/web_integration.mjs --client2` exercises the actual server listeners and
shared authoritative world with two players, commands, inventory, digging,
placement, movement and reconnect. `tests/web2_browser.cjs` tests the mode 2
application itself; it does not substitute a standalone practice world.
