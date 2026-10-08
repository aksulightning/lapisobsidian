# Optional features: world edit and tree chopper

World edit adds administrator commands for filling and replacing blocks in a
selected region. Tree chopper lets survival players fell an oak tree by mining
its bottom log with an axe. Both features are optional and disabled by default.

## Compile-time configuration

`LAPIS_WORLD_EDIT=1` and `LAPIS_TREE_CHOPPER=1` are independent **build-time**
environment variables for `build.sh` and `build-alpine.sh`. Both default to off;
only the exact value `1` enables them. They are not `server.txt` options or
runtime environment switches. ESP-IDF reads them during CMake configuration;
reconfigure after changing them. Direct C builds may pass the matching `-D` flags.

Run the appropriate command from the repository root:

| Features to include | Build command |
| --- | --- |
| Neither | `LAPIS_WORLD_EDIT=0 LAPIS_TREE_CHOPPER=0 ./build.sh` |
| World edit only | `LAPIS_WORLD_EDIT=1 LAPIS_TREE_CHOPPER=0 ./build.sh` |
| Tree chopper only | `LAPIS_WORLD_EDIT=0 LAPIS_TREE_CHOPPER=1 ./build.sh` |
| Both | `LAPIS_WORLD_EDIT=1 LAPIS_TREE_CHOPPER=1 ./build.sh` |

For Alpine Linux, use the same variables with `build-alpine.sh`, for example:

```sh
LAPIS_WORLD_EDIT=1 LAPIS_TREE_CHOPPER=1 ./build-alpine.sh --static
```

Start the resulting binary with `./lapis-obsidian`. Rebuild and restart to change
which features are available. Setting either variable only when starting the
server cannot change an existing binary. Empty values, `0`, `true` and `01` all
leave the corresponding feature disabled. See [Alpine build requirements](build-alpine.md)
for native and cross-compilation setup.

## World edit

Authenticate using `/admin <token>` first. The built-in editor supports bounded
cuboid fills and replacements, not the external WorldEdit plugin API.
The server must have `LAPIS_ADMIN_TOKEN` configured at startup; see
[administrator authentication](commands.md) for setup. This runtime token is
separate from the compile-time feature flags. Builds without world edit do not
advertise or accept `/we`.

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

### Example: fill and replace a region

After authenticating, run these commands in-game:

```text
/we pos1 10 70 10
/we pos2 14 72 14
/we set stone
/we replace stone oak_planks
/we clear
```

The two corners select a 5-by-3-by-5 region (75 blocks). The first edit fills it
with stone; the second replaces stone with oak planks. Clearing the selection
leaves the placed blocks intact. `/we set air` removes blocks in a selection.

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

No administrator login or additional command is needed for tree chopping. With
the feature compiled in, equip an axe in survival mode, stop sneaking, and finish
mining the lowest oak log directly above suitable ground. Logs must share faces;
diagonal contact alone does not connect them. Starting higher on the trunk uses
normal mining because the supporting block is another log.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `/we` is unknown | Build with `LAPIS_WORLD_EDIT=1` and restart using that binary. |
| World edit reports permission denied | Authenticate with the server's configured admin token. |
| World edit asks for both corners | Select `/we pos1` and `/we pos2` again after reconnecting, travelling or clearing the selection. |
| Selection exceeds the limit | Reduce the inclusive cuboid to at most 4,096 blocks, even for replace operations. |
| A block name is unsupported | Use a name from the supported list above; numeric IDs are not accepted. |
| Only one log breaks | Check the build flag, survival mode, axe, sneaking, ground, foliage and 64-log limit. The axe may also have broken. |
| An operation stops partway | Check the reported edit-storage limit; chopping also stops when item storage fills or the axe breaks. Earlier changes remain. |

## Verification

`./tests/run.sh` includes `./tests/optional-features.sh`, which checks all four
compile-time combinations, permissions, selection bounds/reset, replacement,
storage exhaustion, default single-block mining, sneaking, tools, drops, tool
breakage and the enabled command packet graph. `SANITIZE=1` adds ASan/UBSan.
