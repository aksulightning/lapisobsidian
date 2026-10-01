# Overworld mobs and plant fixes

The server keeps the existing fixed mob population (`MAX_MOBS`, 16 by default).
Supported types are chicken, cow, pig, sheep, zombie, skeleton, spider and creeper.
The existing exploration trigger still attempts one spawn in four newly visited
chunks. Daytime surface attempts choose passive animals; night/underground
attempts choose zombies, skeletons, spiders or creepers. Spiders remain neutral
regardless of when they spawn. No world-generator or save-format change is needed.

## Behaviors

- **Zombies** approach survival/adventure players and attack in melee. Skeletons
  and zombies burn in daylight when their column has no cover.
- **Skeletons** hold a bow and fire visible arrows at targets within 16 blocks
  of Manhattan distance and with line of sight, at most once per two seconds.
  Arrows have gravity, stop at solid blocks, deal four health points before the
  existing armor reduction, and disappear on impact or after five seconds.
  Creative, spectator, dead, loading and disconnected players are not targets.
- **Spiders** wander without attacking until a survival/adventure player hurts
  them. They retaliate against that player, not simply the nearest person.
  Anger expires after 15 seconds and clears when the attacker disconnects or
  becomes untargetable. They deal two health points before armor reduction.
- **Creepers** approach, hiss and swell when a target is within three blocks and
  visible. After a two-second fuse they play a firework blast sound and a small
  particle burst, then disappear. Leaving range or blocking sight cancels the
  fuse. The burst changes no blocks, deals no damage, applies no knockback, and
  produces no loot. Killing one normally can drop gunpowder.
- Existing animals retain simple wandering, panic, shearing and item drops.
  Skeleton deaths drop bones/arrows, spider deaths drop string, and ordinary
  zombie/animal loot continues to use the dropped-item system.

Distances and collision use the core's integer player positions. AI runs at
one-second intervals, with one-block cardinal walking and step-up/down checks;
there is no general pathfinder, diagonal corner cutting, climbing spider AI,
breeding, equipment progression or modern combat system. Mob/arrow state is
transient across restarts. Player-fired bows and collectible embedded arrows
are not implemented. Despawn uses the existing 256-block distance cap.

## Administrator spawning

After authenticating with `/admin <token>`:

```
/spawnmob skeleton
/spawnmob spider 10 70 10
/spawnmob creeper 12 70 10
```

Syntax: `/spawnmob <type> [x y z]`. Names are lowercase and restricted to the
eight types above. Omitted coordinates mean the caller's X/Y and Z+2. Explicit
coordinates must be integers, X/Z=-32768..32767 and Y=1..253. Spawning requires a
living, loaded administrator, solid support, clear body space, no player/mob
overlap, and a free mob slot. Failure returns a message without spawning.
Spiders additionally check adjacent clearance for their wider bodies. A position
that lacks ground or lies inside an obstacle is rejected; the command does not
search the whole world for another location. Relative coordinates, selectors,
custom health and batch counts are intentionally unsupported.

## Grass, flowers and seeds

Short grass (and fern) broken without shears has a **1-in-8 chance** to drop one
wheat seed. Shears preserve the plant itself. A single short-grass item crafts
into one wheat seed in either crafting grid. Place it manually and take the
output with a normal click; shift-click output is not supported for this custom
recipe. The server consumes the actual ingredient and updates the cursor, rather
than relying on the modern client to know a custom recipe. The recipe book is
not populated with this recipe. Seeds now plant wheat on farmland; see
[farming and bread](farming-and-controls.md).

Single-block flowers and mushrooms now break on the client's initial mining
action and drop themselves. They are also classified as passable and dependent
on support, so mining their supporting block removes the plant and drops it.
The fix covers the compact palette's flowers, including generated dandelions
and poppies. It does not add two-block flowers or new vegetation generation.

## Implementation, compatibility and tests

`src/mobs.c` reuses `mob_data` and moves the inherited tick/spawn logic into a
small gameplay module. A side array holds targeting, cooldown, fuse and viewer
state without changing `MobData` or `PlayerData`. A fixed 32-entry arrow pool
adds about 1 KiB; the mob side array, including sound timers, adds 256 bytes
on the tested build. AI stays at one-second intervals.
Projectile collision work is bounded to 40 short steps per update after a stall.
No per-projectile allocation, scripting engine, database or dependency is added.
Nearby-client tracking handles spawn, equipment, metadata and removal.

`src/mob_packets.c` owns bow equipment, fuse metadata, named sounds, firework
particles and bounded attack-packet parsing. An attack/interact frame is fully
validated before applying damage or retaliation; distant targets, invalid entity
IDs and untargetable states are rejected or ignored. This is not a general
adversarial audit of the inherited server. Protocol IDs are pinned by a compile
assertion to Java 1.21.8 / 772. Packet/entity/particle fields were checked against
PrismarineJS minecraft-data's versioned `protocol.json`, `entities.json`,
`particles.json` and `sounds.json`; see [registry provenance](registries.md).
Only five additional item constants (bow, arrow, bone, string, gunpowder) are
retained in the registry snapshot. Sounds come from the connected client's
existing resources; the repository contains no sound assets.

Run from the repository root:

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
node build_registries.js --check  # optional maintainer snapshot check
```

`tests/mobs.c` covers plant mining, grass RNG/drop behavior, seed recipe pickup
and truncation, mob placement, arrows hitting players/walls, projectile limits,
sunlight/shade, neutral retaliation/expiry, fuse cancellation, harmless bursts,
packet framing, invalid attacks, natural spawn selection and admin spawning.
Command tests cover names, permissions, missing/excessive arguments, numeric
bounds and the expanded client command tree. Tests use local sockets and decode
protocol packets; a graphical Minecraft client play session remains a manual
compatibility check.

The normal build, full C regression suite, source-only no-JAR build, strict
new-module warnings and ASan/UBSan suite passed. The deterministic registry
check also passed. A live TCP smoke test additionally verified protocol-772
login/play, administrator permission rejection/authentication, spawning all
three new mobs, skeleton bow equipment, projectile movement and arrow damage.
Visual appearance and audible playback still need a graphical-client session.

## Mob voices

All eight supported mobs emit their named hurt/death sounds. Chickens, cows,
pigs, sheep, zombies, skeletons and spiders also emit ambient sounds on separate,
staggered timers (roughly 8–13 seconds). Creepers keep their existing hiss and
firework burst; they have no ambient voice. Ambient timers never consume the
world or gameplay random stream. Passive animals and neutral spiders use the
neutral sound category; zombies, skeletons and creepers use hostile. Sounds are
sent only to loaded clients within 32 blocks on each axis; ambient voices use
a tighter Manhattan-distance check. No audio files are stored or downloaded.
