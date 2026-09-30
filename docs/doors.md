# Oak doors

Craft one oak door from six oak planks in two adjacent columns of a crafting
table. Place it on a block's top face with two replaceable spaces above. The
door faces the placing player's horizontal direction. Right-click either half
without sneaking to open or close both halves. Holding another item does not
consume it when interacting. Creative placement does not consume the door.

Breaking either half removes its partner. Survival mining drops one collectible door;
creative mining returns none. Replacing a half preserves the replacement block
and removes its partner. Losing support removes both halves without spawning
an item. Neither half can be placed overlapping a loaded player's body. Placement
and hand interaction require a living, loaded player within eight blocks, using
the core's integer player position. Adventure permits interaction but not
placement/mining; spectator permits neither.

The first implementation supports ordinary oak doors with a fixed left hinge.
Automatic double-door hinge selection/linking, iron-door activation, redstone,
sounds, other wood variants and trapdoors are not implemented here. Existing
other-door palette entries do not imply functional two-block support. Players
use the modern client's door collision shape; mob and projectile collision treats open oak doors as passable. Support
checks use the existing core classification, with door blocks explicitly excluded.

## Storage and compatibility

`include/doors.h` and `src/doors.c` implement a fixed 256-entry pool (2 KiB on the
tested build) without per-door allocation. Each door uses two ordinary block
changes with compact oak-door ID 144. Coordinates, facing and open state are saved
separately in `doors.bin`; the world and player save layouts are unchanged.
Startup loads door state after block edits and before sign cleanup. No generator
version change, terrain regeneration or migration is required.

Keep `world.meta`, `world.bin`, `signs.bin` and `doors.bin` together for backups
and world moves, with the server stopped. The sidecar starts with eight bytes:
`LODOOR`, version byte 1, reserved byte 0. Each seven-byte record stores signed
16-bit X/Z in big-endian order, lower Y (1..254), facing (0..3 = north/south/west/
east), and open (0/1). Maximum file size is 1,800 bytes. Invalid versions, heights,
facings, booleans, overlapping pairs, excessive records and partial records fail
startup without first changing world blocks or overwriting the file.

Startup removes incomplete or unsupported pairs and drops untracked oak-door
blocks, including legacy one-block doors and halves left by interrupted placement.
Thus deleting or losing `doors.bin` removes existing oak-door blocks at the next
startup. It must be included in backups. Stale data never becomes an extra item.

Writes use a checked temporary snapshot and rename. A failed toggle write restores
the old open state. Failure to create either half or save placement restores the
previous target cells and does not consume an item. Failed removal saves are
reported. As with signs, this is not a transaction across the sidecar and
`world.bin`, nor a power-loss guarantee: the inherited block writer reports its
errors rather than propagating them, there is no fsync, and interval block syncing
can defer edits. Interrupted writes can lose doors/state; mismatched backups can
associate old state with matching blocks. Disk syncing disabled means memory-only
door state. Windows replacement-rename, ESP-IDF and Cosmopolitan are untested.

## Integration and protocol

Placement/interaction use the existing validated Use Item On packet path; there
is no new command, runtime, plugin API or network parser. Successful block-change
hooks remove invalid pairs. Chunk transmission appends two orientation/state
updates; ordinary block updates also consult the door record. No block entity
or NBT is required. The Beta-style recipe returns one door, in either horizontal
alignment of the two crafting columns. Sprinting, signs, world generation and
`--mirror-horizontal` remain unchanged.

The immutable facing bases in `src/doors.c` are maintained protocol data from
PrismarineJS/minecraft-data revision
`f5d7d74604d8c6153fd086bfe035e0630a5207cc`,
`data/pc/1.21.8/blocks.json`. The oak-door range is 4686..4749, with property order
facing (N/S/W/E), half (upper/lower), hinge (left/right), open (true/false), powered
(true/false). This module selects only left-hinged, unpowered states. A protocol
772 compile-time assertion requires review when upgrading. Wire fixtures cover
all four facings, both halves and both open states. No registry regeneration,
Java, vanilla server binary or additional dependency is needed.

## Validation

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

Normal and restricted-PATH no-JAR builds pass. The door module compiles with
`-Wall -Wextra -Wconversion -Wshadow -Werror`. The full regression suite and
ASan/UBSan pass, including both halves, state bytes, toggles, crafting, survival
single-item returns, creative consumption, permissions, occupancy, coordinates,
save-write rollback, invalid saves, pool/edit limits and stale cleanup.
LeakSanitizer remains disabled in this sandbox.

A live TCP test exercises creative inventory, two-block placement, upper/lower
interaction, process restart, open-state transmission with chunks and paired
removal. A graphical Minecraft client play session remains untested.

Changed files: new `include/doors.h`, `src/doors.c`, `tests/doors.c` and this guide;
updated `src/crafting.c`, `src/main.c`, `src/packets.c`, `src/procedures.c`,
`tests/run.sh`, README and registry/milestone documentation.
