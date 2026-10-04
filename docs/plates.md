# Experimental Plates

Plates are independent worlds on the same server. Players can occupy different
Plates simultaneously. Enable the feature in `server.txt`, then restart:

```ini
experimental_enable_plates=true
```

The default is `false`. With it disabled, the server uses its original single
world and save paths. Enabling creates a separate `plates/` directory and a
protected default Plate named `hub`. Existing root-level saves are not moved,
converted or overwritten. Disabling and restarting returns to those saves.

## Commands

Everyone can use:

```text
/plate list
/plate go hub
/plate go survival
```

Administrators authenticated with the existing `/admin` command can use:

```text
/plate create survival 12345 betanium
/plate create building 0 flatworld
/plate create islands -17 skybox
/plate create furnace 987 volcanic
/plate create another_hub 123 hub
```

Creation takes exactly `/plate create <worldname> <seed> <worldtype>`. Seeds are signed
64-bit decimal integers. Names contain 1–24 lowercase ASCII letters, digits,
underscores or hyphens; `list`, `go`, and `confirm` are reserved. Existing names
and directories are rejected. There are at most **eight Plates**, including hub.
Creating a Plate does not move players; use `/plate go <name>` afterwards.

To remove a Plate:

```text
/plate remove islands
/plate confirm
```

Confirmation belongs to the requesting admin, expires after 30 seconds and
cannot apply to a replacement Plate with the same name. All players must first
leave the Plate, and the default hub cannot be removed. Removal drops it from
the active catalog and archives its directory as `plates/removed-<name>-<time>`.
Archived files can be backed up or deleted manually with the server stopped.
They do not count toward the eight-Plate limit.

## World types

| Type | Terrain | Mob policy |
| --- | --- | --- |
| `hub` | Original blue virtual arena with concentric decks, glowing grid lines and towers, inspired by Sector Five | No mob spawning, including `/spawnmob` |
| `betanium` | Existing seeded Beta 1.7.3-inspired generation and Far Lands | Existing overworld mobs |
| `flatworld` | Bedrock, stone, dirt and a grass surface at Y=64 | No mob spawning, including `/spawnmob` |
| `skybox` | Small floating island with a tree and contained water/lava pools | Existing overworld mobs where space permits |
| `volcanic` | Seeded netherrack terrain, lava seas, quartz ore and a ceiling with glowstone | Only zombie pigmen and ghasts |

The hub is an original block-built interpretation, not a copied map or asset
pack. Hub, flatworld and skybox use fixed templates; their seeds are retained
but do not change those templates. Volcanic terrain varies with its seed.

`/spawnmob zombie_pigman` uses the modern client's zombified-piglin model. Pigmen
are neutral until attacked, then retaliate temporarily. Ghasts float, make
sounds and fire bounded projectiles that damage players without destroying
blocks or spreading fire. These are basic behaviors; there are no portals,
piglin trading, group anger, ghast fireball reflection or Nether progression.

## Isolation and persistence

Each loaded Plate owns its block edits, mobs/projectiles, item entities, signs,
doors, redstone, farmland, fluid queue, musicbox playback and terrain cache.
Ticks select one Plate at a time without loading saves on every packet or
copying entire world buffers. World actions, visibility, item pickup, damage,
chat and player-name teleport lookup are scoped to the current Plate. Time is
independent while running; clocks and transient entities reset on restart.

Each Plate has its own `world.bin`, `world.meta`, `signs.bin`, `doors.bin`,
`circuits.bin` and `farming.bin` under `plates/<name>/`. The existing packed block
and sidecar record formats are retained. `plates/index.txt` is a versioned,
validated catalog; `plates/players.bin` holds shared player profiles. Inventories
are shared across Plates. New connections always enter hub with their saved
inventory. The music library in `songs/` is shared, while playback is isolated.
Do not hand-edit the catalog or move world directories while the server runs.

Travel closes inventory interfaces before switching, clears sign/musicbox
selections and entity visibility, sends a distinct protocol dimension key and
reloads chunks. Inventory operations must have room to return any held stacks.
Travel is limited to once every two seconds. Falling into the hub or skybox void
returns the player to spawn. The existing +/-4068 player border applies to all
Plates. `/seed`, `/worldinfo`, `/spawn`, and `/time` refer to the current Plate.

## Resource and compatibility limits

State is allocated once on first visit. Unvisited Plates do not allocate gameplay
or chunk buffers. Loaded Plates stay loaded until removal or shutdown. Existing
limits (20,000 block edits, 16 mobs, 128 dropped items and other module caps) apply
per Plate. RAM and tick work therefore grow with the number of loaded Plates;
there is no full-world allocation or pregeneration. Disabling Plates retains
legacy static storage and behavior.

All Plates use the existing protocol-772 overworld dimension type and biome
registry. Volcanic is Nether-like terrain, not a protocol Nether dimension;
its sky, lighting and client presentation retain overworld semantics. Horizontal
mirroring affects betanium terrain; templates and volcanic terrain are not
mirrored. World metadata still guards the configured mirror setting.
This experiment requires the desktop filesystem build; ESP and memory-only
builds reject enabled Plates clearly. No new runtime dependencies or vanilla
server JAR are required.

Tests exercise two-world state/packet isolation, real travel/chunk framing,
configuration, permissions, safe names, catalog limits, confirmed removal and
name reuse, restart recovery, sidecars, profiles, volcanic mob policy and
non-destructive projectiles. Run:

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

A graphical Java 1.21.8 client is still needed to assess hub appearance, dimension
transition visuals and the simplified ghast presentation.
