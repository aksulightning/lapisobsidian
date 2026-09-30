# World boundary and Far Lands

The playable square is **-4068 < X < 4068, -4068 < Z < 4068**.
Reaching either limit returns a player to the normal spawn at X/Z 8.5,
with Y selected by the existing spawn-height rule. This applies to every
game mode, including administrators, creative and spectator. There is no
damage wall or invisible collision wall: the boundary acts as a teleport gate.
It also redirects `/tp` destinations outside the square and saved login
positions outside it. A chat message explains a movement/command return.
Fall distance and visited-chunk history reset. Inventory and health are retained.
Spawn uses the existing terrain-height rule, not a newly protected spawn area.

Far Lands begin where **abs(X) >= 3940 or abs(Z) >= 3940**. This leaves a
128-block outer region before the gate. The custom terrain has tall, perforated
stone walls, overhangs, grass ledges and water below sea level. It stays within
Y=0..127, retains the bottom 16 blocks, and uses the world seed and coordinates.
It is inspired by the appearance of the Far Lands, not an implementation of
the historical Java overflow bug. No integer overflow is intentionally invoked.
The region continues beyond the gate so chunks visible from inside still exist.

## Compatibility

Generator version 3 automatically upgrades version 2 `world.meta` after
validating the saved protocol, seed and mirror setting. Only the version byte
changes; seed, flags, world edits, doors, signs and player storage retain their
formats. Version 1 remains unsupported. Back up the world files before upgrading:
outer terrain changes even in previously explored places because terrain is
regenerated rather than stored as complete chunks. Existing player edits still
override generated terrain. Structures built in the outer region can intersect
the new walls or holes. A version-2 binary will reject upgraded metadata.

Terrain strictly inside +/-3940 is unchanged. Mirroring still uses X -> -X-1
for terrain samples; the Far Lands region itself stays at the same physical
coordinates on either side. The storage range is still signed 16-bit X/Z;
the new playable boundary does not rewrite saved coordinates or allocations.

## Streaming fixes

The previous movement path skipped all chunk updates when revisiting a chunk
in a small history. It also multiplied the entire chunk displacement by view
distance, leaving holes when a movement crossed multiple chunks. The replacement
always sends the new view center and each chunk in the new view absent from the
previous view. Work is bounded by one view square, even for a large displacement.
History now only suppresses repeated natural-spawn attempts. These are concrete
missing-terrain bugs; a cutoff specifically at 9999 was not independently
reproduced. The serializer is tested at the old reported 9999/10000 area too.

`src/world_border.c` shares teleport and chunk-view logic with commands and
movement. A byte-sized flag per player suppresses queued pre-return positions
until a movement arrives near spawn, avoiding repeated expensive chunk reloads.
Disconnect/session reset and subsequent administrator teleports clear the flag.
`src/beta173_farlands.c` applies two small seeded noise fields after terrain
mirroring, before the existing edit overlay. No heap storage or dependencies
are added, and generation remains chunk-local and order independent.

## Validation

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

Tests cover exact/fractional limits, all edges and corners, queued movement,
spawn position and fall reset, bounded view updates, revisits and large jumps,
seed determinism, a pinned terrain hash, unaffected inner/deep terrain, mirrored
onsets, version-2 metadata upgrades and actual chunk packet contents at Far
Lands and distant coordinates. A graphical client remains necessary to review
the terrain's appearance; protocol tests do not constitute a visual playtest.

The normal build, full ASan/UBSan suite and expanded chunk-packet tests passed.
A live protocol-772 TCP check also passed version-2 upgrade, all four gate
returns, creative travel through the Far Lands, queued outside movements,
outside `/tp` redirection and revisited/multi-chunk movement. The precise cause
of the reported cutoff at 9999 remains unconfirmed; the streaming defects above
are fixed independently of the new playable boundary.
