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
| Play inbound | Join, absolute teleport/confirmation, chunk sections/biomes, block update, keepalive, slot/cursor/hotbar, health/food, system chat, game mode, respawn, container open, typed entity spawn/position/deltas/rotation/removal, supported metadata/equipment, inline sound events, time |
| Play outbound | Loaded indication, position/look/ground flags, input/sprint, swing, dig/use/place, selected slot, creative stack, container click/close, chat/command, respawn request, entity attack/interact, cancel digging/eating and item drops |

The adapter translates upstream packet objects to current wire packets; it
never sends protocol-47 packets to the server. Rendering IDs and item IDs are
mapped explicitly; no proprietary assets are tied to protocol identifiers.
Edits appear only upon server updates. Server inventory updates drive hotbar
contents. Unknown items use a labeled original fallback preview; unsupported
components fail explicitly. Unknown packet IDs are ignored, rather than
misparsed as unrelated packets.

Current limits: intended for this server snapshot only; no compression,
encryption, Microsoft authentication, arbitrary registry NBT, relative teleport
flags, resource packs, skin fetching, signed chat or external servers. Server mob IDs use distinct original procedural animal, humanoid, spider,
creeper and ghast models; dropped items and arrow/fireball projectiles have
separate small models. Relative updates accumulate from the last wire position
and interpolate without simulating remote physics. Spawn/body/head rotation,
player flags, sheep shearing, creeper fuse state, item stacks, main-hand equipment,
hurt and pickup events are handled. Designs and animation remain simplified;
unknown entity types use an explicitly generic small cube. The upstream player
renderer remains for remote players.

Additional blocks use generic cubes/translucent cubes, so doors/slabs and other
complex collision shapes are approximate. Flowing water maps to the same
non-solid liquid as source water; lava variants are non-solid. Snow cover and
moss carpet use thin, selectable, passable surfaces so they cannot hide or trap
mobs; full snow blocks remain solid. Server lighting
arrays are not applied; the upstream ambient/skylight model remains. Survival
mining uses a client material/tool timing table, cancels on release/retarget/tool
change, and applies edits only upon server updates. This is not a claim of exact
vanilla timing/enchantments. Server drops, durability, inventory, damage, food
and crafting remain authoritative. Eating sends one start and a release action,
so repeated mouse events cannot reset the server's eating timer. Sprinting is
suppressed at low food; dead players cannot move or interact and R requests
respawn. Inventory left/right/Shift-click and 2×2/3×3 crafting are exposed; drag
painting and number-key slot swaps are not exposed. Touch menus remain upstream;
mode 2 has no full mobile movement controls.
Mode 1 remains the supported touch client.

`tests/web_integration.mjs --client2` exercises the actual server listeners and
shared authoritative world with two players, commands, inventory, digging,
placement, movement and reconnect. `tests/web2_browser.cjs` tests the mode 2
application itself; it does not substitute a standalone practice world.

Inline sound holders (`0x6e`) are decoded from the server, including note/musicbox,
mob and bucket effects. Original synthesized cues replace recordings; pitch changes
playback rate, not only a filter. First-use sound pools play immediately. Sound
pools are bounded and positional attenuation uses a 3-block reference distance.
