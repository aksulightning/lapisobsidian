# Optional gameplay features

`LAPIS_WORLD_EDIT=1` and `LAPIS_TREE_CHOPPER=1` are independent **build-time**
environment variables for `build.sh` and `build-alpine.sh`. Both default to off;
only the exact value `1` enables them. They are not `server.txt` options or
runtime environment switches. ESP-IDF reads them during CMake configuration;
reconfigure after changing them. Direct C builds may pass the matching `-D` flags.

## World edit

Authenticate using `/admin <token>` first. The built-in editor supports bounded
cuboid fills and replacements, not the external WorldEdit plugin API.

| Command | Behavior |
| --- | --- |
| `/we help` | Shows syntax and supported block names |
| `/we pos1 [x y z]` | First corner; defaults to the player's block position |
| `/we pos2 [x y z]` | Second corner; defaults to the player's block position |
| `/we set <block>` | Fills the inclusive cuboid |
| `/we replace <from> <to>` | Replaces matching blocks in the cuboid |
| `/we clear` | Clears the selection without changing blocks |

Coordinates are integers, X/Z strictly inside +/-4068 and Y=0..255. Corners may
be selected in either order. A selection may contain at most **4,096 blocks**,
including for replace operations. Oversized or invalid requests change nothing.
Selections belong to the connection and current Plate; reconnecting or travelling
to another Plate clears them. Players must be alive and loaded to use the editor.

Supported names: `air`, `stone`, `dirt`, `grass_block`, `cobblestone`, `oak_planks`,
`oak_log`, `oak_leaves`, `glass`, `sand`, `gravel`, `bricks`, `obsidian`.
The `minecraft:` prefix is optional. Numeric IDs and stateful placement (such as
chests, doors or signs) are unsupported. Existing blocks, including containers,
can be overwritten; this does not generate drops or retain container contents.
Updates use the normal persistence, sidecar cleanup and same-Plate broadcast path.

**There is no undo.** If edit storage fills, the operation stops and reports
the number changed; earlier changes remain. The global edit-storage limit still
applies. Disabling the feature on a later build keeps edits already saved.

## Tree chopper

In survival, finish mining the bottom oak log with any axe while standing normally.
The log must stand on dirt, grass or podzol, and the face-connected group must
touch oak leaves. Up to **64 connected oak logs** at or above the starting log are
removed, with one normal log drop and the existing probabilistic tool-wear check
per log. Chopping stops if the axe breaks, item storage fills or an edit fails;
remaining logs stay in the world. Leaves are left in place.

Sneaking, other tools, empty hands, creative mode, bare log pillars and groups
larger than 64 logs use ordinary single-block mining. Adventure and spectator
restrictions remain in effect. Only oak logs are supported by this initial feature.
Tree detection is a foliage/ground heuristic: player-built logs connected to a
qualifying tree can also be chopped. Sneak to remove individual logs near builds.

## Verification

`./tests/run.sh` includes `./tests/optional-features.sh`, which checks all four
compile-time combinations, permissions, selection bounds/reset, replacement,
storage exhaustion, default single-block mining, sneaking, tools, drops, tool
breakage and the enabled command packet graph. `SANITIZE=1` adds ASan/UBSan.
