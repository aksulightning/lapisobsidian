# Persistent signs (Milestone 4)

Lapis Obsidian supports oak standing and wall signs on Java 1.21.8/protocol 772.
Craft one sign from two full rows of oak planks and a stick in the bottom-center
slot, or select an oak sign in creative inventory. Place on the top or side of a
solid block. Placement opens the front editor. Right-click an existing sign
without sneaking to edit the side facing your position. Both sides persist.

## Bounds and behavior

- Maximum 128 sign records, fixed at compilation by `SIGN_LIMIT`.
- Four lines per side, each at most 96 UTF-8 bytes and 90 UTF-16 code units.
  Multibyte text reaches the byte limit sooner. Empty lines are allowed.
- Text must be well-formed UTF-8. NUL, control characters, invalid Unicode scalar
  values and the section-sign formatting character are rejected. Text is literal;
  JSON, click events, commands and selectors are never evaluated.
- X/Z must be -32768..32767 and Y must be 0..255. Adjacent placement cannot wrap
  across those boundaries. Editing/placement require a living, loaded survival
  or creative player within eight blocks, measured using the core's integer
  player position. Adventure and spectator players cannot edit/place signs.
- An editor session belongs to one connection, one sign and one side. It expires
  after 60 seconds of server ticks, permits one accepted update, and clears on
  removal, disconnect or slot reuse. Another player cannot open that sign while
  the session is active. There is no permanent ownership or permission framework.
- Direct removal/replacement clears text immediately. Removing its supporting
  block also removes the sign. Support checks use the core's existing solid-block
  classification. Removing support does not spawn a dropped sign item; direct
  survival mining uses the existing inventory pickup path.

Only ordinary oak signs receive this text implementation. Other wood types,
hanging signs, dyes, glow ink, wax, waterlogging and rich text are not implemented.
Signs are non-solid; standing signs use sixteen rotations and wall signs use the
four horizontal facings. Server text is transmitted after a chunk arrives and
whenever a sign is placed or edited. Large/malformed update packets disconnect
rather than partially applying text. Replayed, unsolicited, distant or unauthorized
well-formed edits are ignored and the current sign is sent back.

## Storage and recovery

The existing `world.bin` keeps the compact oak-sign block IDs. A new `signs.bin`
sidecar stores positions, orientations and plain text. Keep `world.meta`,
`world.bin` and `signs.bin` together when backing up or moving a world, with the
server stopped. Milestone 2/3 world and player layouts remain unchanged; no
terrain regeneration or migration is needed. Missing sign files are allowed.

The file starts with eight bytes: `LOSIGN`, version byte 1, reserved byte 0.
Each record is 782 bytes: signed 16-bit X and Z in big-endian order, unsigned
8-bit Y, orientation byte (0..15 standing, 16..19 north/south/west/east), then
front/back four-by-97-byte zero-padded UTF-8 strings. No structs, pointers, NBT
or JSON are written directly. At most 128 records are accepted (100,104 bytes
including the header). Unknown versions, invalid orientations, malformed text,
nonzero padding, duplicates, excessive records and truncated data fail startup
without overwriting the file.

Changes write a bounded snapshot to `signs.bin.tmp`, check writes and close, then
rename it over `signs.bin`. Failed text writes restore the previous in-memory
text. Failed placement writes restore the prior block and do not consume the
item. Removal failures are reported on stderr. On startup, records without a
matching sign block are discarded and unsupported signs are removed. An old
sign block without a record gets a blank standing-sign record on interaction,
subject to the storage limit. Stale editor sessions never survive a restart.

This is not a transaction across the two save files or a power-loss durability
guarantee: no file/directory fsync is performed, the inherited block-write path
reports rather than propagates its errors, and interrupted multi-file changes
can lose recent text or leave a blank sign. Restoring mismatched backups can
also attach old text to a sign at the same coordinates. Interval block syncing,
if enabled, retains the core's deferred-save window. Do not copy a sign sidecar
between unrelated worlds. With `SYNC_WORLD_TO_DISK` disabled, signs are memory-only.

The sign pool uses approximately 98 KiB plus a small editor array; no per-edit
heap allocation, startup-sized allocation or new dependency is added. Lookup and
chunk transmission scan the bounded pool. Block-change cleanup only queries
supports near the changed block. Each saved edit rewrites at most about 98 KiB;
this deliberately favors a simple small store over unbounded scale or a database.

## Protocol snapshot and sources

`include/sign_protocol.h` maintains only the required dry standing/wall state
IDs and sign block-entity type, with a compile-time protocol-772 assertion. It
does not change the saved 256-entry compact palette or run a generator at build.

- PrismarineJS/minecraft-data revision
  `f5d7d74604d8c6153fd086bfe035e0630a5207cc`,
  `data/pc/1.21.8/blocks.json`: standing states 4367..4397 by twos;
  wall states 4859/4861/4863/4865 in north/south/west/east order.
- The same revision's `data/pc/1.21.8/protocol.json`: update-sign 0x3B,
  open-editor 0x35, block-entity data 0x06, placement/mining frame layouts.
- The 1.21.8 block-entity registration order (SIGN follows DROPPER, type 7),
  reflected in https://mappings.dev/1.21.8/net/minecraft/world/level/block/entity/BlockEntityType.html.
  misode/mcmeta `1.21.8-registries`, `block_entity_type/data.json`, confirms the
  `sign` identifier; that alphabetical list is not used as numeric registry order.

The modern NBT contains `front_text`/`back_text`, black color, non-glowing text,
`is_waxed=false`, and four literal string components per side. Supplementary
characters become modified-UTF-8 surrogate pairs only during NBT encoding.
Update the version assertion, states, packet layouts and wire tests together
when changing protocol. No game binaries, extracted Java source or assets are
included. Existing upstream notices apply.

## Validation and limitations

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

Tests cover placement, inventory consumption, rotation/wall facings, both sides,
UTF-8/modified-UTF-8, independent NBT decoding, restart persistence, removal,
support removal, stale records, blank-record recovery, fixed capacity, boundary
coordinates, game-mode restrictions, edit-session locks/expiry/reset, failed
writes, invalid files, the sign recipe and every truncated prefix of edit,
placement and mining packet fixtures. New modules compile with strict conversion
and shadow warnings. All earlier regression and source-only no-JAR builds pass.

ASan/UBSan pass on tested paths. They exposed two inherited unaligned inventory
accesses in the placement/eating path; these now access packed fields directly,
without changing the on-disk player layout. The empty crafting-grid index was
also bounded while adding the recipe. LeakSanitizer remains disabled in this
sandbox. A live TCP test covers sign placement/editor, Unicode text, process
restart, chunk retransmission, re-edit, removal/save cleanup and malformed-packet
disconnection. A graphical Minecraft client play session remains untested.
Windows replacement-rename behavior, ESP-IDF and Cosmopolitan are untested.
This milestone does not claim a complete audit of inherited packet/save handling.
