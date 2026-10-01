# Dropped items

Mining now leaves an item entity at the broken block instead of immediately
putting the result in the miner's inventory. Existing chickens, cows, pigs,
sheep and zombies leave their existing loot in the world, including deaths
without a player killer. Shearing also drops wool. Mining either oak-door half
still produces just one door. Creative block breaking produces no loot.

Q drops one held item; Ctrl-Q drops the held stack. Inventory Q/Ctrl-Q and
clicking a carried stack outside the inventory also work. Creative inventory
outside-drop packets are accepted only for living, loaded creative players.
Spectators cannot drop or collect items. Adventure players can drop and collect.

Items fall onto solid blocks. Nearby living players collect as much as fits in
the 36 main/hotbar inventory slots; leftovers remain visible. Armor slots are
not extra storage. Pickup animations are broadcast to nearby viewers. Mining
and mob loot have a 500 ms pickup delay; intentional player drops have a
1500 ms delay. These are evaluated on the existing server tick (one second by
default), rather than increasing the whole server's tick rate.

## Bounded behavior

- `ITEM_ENTITY_LIMIT` is 128, backed by a fixed C array (3584 bytes on the tested
  64-bit build). There is no per-item heap allocation or new dependency.
- Compatible stacks in the same horizontal block cell merge within half a
  block vertically. Merging keeps the older age and longer pickup delay.
- Items expire after five minutes, disappear below Y=0, and are destroyed by
  lava. They continue aging when the last player disconnects.
- Gravity has at most 20 collision steps per server tick, with each step less
  than a block. Lag does not cause an unbounded physics catch-up loop.
- If the pool is full, an inventory drop leaves the inventory unchanged.
  Mining a block with loot leaves that block intact unless its drop fits in
  an existing stack. Environmental/mob loot may be omitted at capacity.
- Only nearby clients receive item spawn, metadata, movement and removal
  packets. Disconnect clears tracking; entering range synchronizes existing
  items. Entity IDs start at -1024, separate from players and inherited mobs.

## Protocol and safety

`src/items.c` and `include/items.h` handle item lifecycle, visibility and pickup.
`src/inventory_packets.c` parses the protocol 772 Click Container frame into a
bounded 4096-byte static receive buffer and at most 64 changed slots, before
mutating state. Inventory drop results use server-owned stacks, ignoring client
predictions. Invalid slots, duplicate changes, malformed VarInts, truncated
frames, invalid item IDs and excessive counts fail before any mutation.
Crafting slots use their own arrays; packed chest records are accessed with
`memcpy`, after checking the stored pointer against existing chest records.

Item entity ID 69, metadata indices 5/8 and serializer IDs 8/7 are minimal
Java 1.21.8 compatibility data, checked against PrismarineJS minecraft-data's
[`entities.json`](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/entities.json)
and [`protocol.json`](https://github.com/PrismarineJS/minecraft-data/blob/master/data/pc/1.21.8/protocol.json).
The implementation has a protocol-version compile assertion. No game assets
or server binaries are required. Existing notices continue to apply.

## Intentional limits

Drops are transient and are not saved across a server restart. The world and
player save layouts are unchanged. There is no horizontal throw trajectory,
water current, item damage, hopper handling or precise slab/stair collision.
Collision uses the compact server block classification, with open oak doors
passable. Inventory stack sizes follow the existing server gameplay table.
Death-inventory scattering remains outside this milestone. Inventory actions now
use server-owned stacks, including ordinary clicks, crafting and transfers.
See [inventory and packet hardening](security-hardening.md) for behavior and limits.

## Validation

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

The item tests independently decode spawn/metadata/pickup/removal packets and
exercise gravity, lifetime, lava/void cleanup, partial and competing pickups,
merging, viewer tracking, negative coordinates, mining, mob loot, Q/Ctrl-Q,
cursor drops, capacity rejection and malformed inventory frames. Existing sign
and door tests now collect their drops instead of expecting instant delivery.
The normal build, complete regression suite, source-only no-JAR build and
ASan/UBSan suite passed for this milestone. Additional normal-click, packed
chest, component-hash and creative-drop tests also passed under ASan/UBSan.
The new C modules and item test compile with
`-Wall -Wextra -Wconversion -Wshadow -Werror`.
A real modern-client rendering/play session remains a separate manual check.
