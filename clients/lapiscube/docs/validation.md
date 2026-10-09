# Validation — 2026-10-09

Reference server: testing `56300de744b9b859993f64d5c00d1637e42584c6`.
Engine: ClassiCube `d41c3f7eef2038f59702b58bdb373483fb0d28f9`.
Linux/GCC, isolated loopback servers, seed 42, ordinary survival. Server source,
security checks and gameplay rules were not changed. No proprietary game client
or assets were used.

## Executed checks

| Check | Actual result |
| --- | --- |
| Unchanged server build | Passed |
| `make test` | 2,396 protocol assertions + 8,153 world/gameplay assertions passed under strict C89 warnings |
| ASan/UBSan (`SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS`) | Both suites passed; leak scanning disabled because `/proc` is unavailable in this runner |
| `make integration` | Actual status/pong, configuration, Play, decoded chunks, keepalive, same-identity reconnect and interaction sequence passed |
| Core C++ build | Pure modules compiled with C++11 and warnings as errors |
| Linux X11/OpenGL build | Linked successfully using installed versioned runtime library names; packaged builds normally use development libraries |
| Windowed visual/input smoke | Actual native world rendered under Xvfb/Mesa; world, look, movement/jump and inventory screenshots inspected |
| `make survival` | Two real clients passed 2×2 and 3×3 crafting, chest deposit/reopen/withdraw, furnace recipe checks/output, eating, Unicode sign editing, command teleport, lethal sword damage and respawn |
| Native survival visual/input smoke | Held food consumed once; sign editor/saved reading overlay, mining progress/cancellation and server-command day/night shading inspected |
| `make progression` | Passed from an empty inventory/new world: natural logs → planks → placed workbench → wooden pickaxe → mined cobblestone; earned inventory retained on same-identity reconnect |
| M7 native inventory check | Fresh-world earned stacks displayed; number-key swap, drop-one and crafting close/reopen inspected in the X11 client |
| Lapis skins | Lapis skin request path disabled; final visual run produced no skin HTTP request |
| Native terminal and Classic regression | Passed: decoded native world/loaded transition and original 131-byte Classic login |
| Linux package | Created 1.5 MiB ZIP; hashes and ZIP CRCs verified; packaged binary loaded the actual world and inventory in a fresh server visual smoke |
| Windows | MinGW cross-build and package passed in GitHub CI; no Windows runtime session yet |

The sanitizer configuration retains address and undefined-behavior checks. Core
parser/model code is instrumented; the complete engine and existing BearSSL
objects were not instrumented. The environment emitted symbolization warnings.

Real-server interaction sequence (not a mocked protocol peer):

1. Join and decode 25 server chunks; synchronize both spawn teleports.
2. Start/finish mining spawn ground; wait for authoritative air update and dirt pickup.
3. Click held stack to the server cursor; transfer to hotbar slot 8 and wait for updates.
4. Drop one item; wait for removal from inventory and subsequent server pickup.
5. Place it back; require a block update and inventory consumption.
6. Send unsigned chat and require its server round trip.

```text
result=PASS state=4 registries=11 entries=68 tags=1 joined=1 loaded=1 chunks=25 teleports=2 keepalives=3 delegated=107 queued=0
decoded=25 block_updates=2 gameplay_updates=62 health=20 food=20 exercise_stage=9
```

The diagnostic `delegated` count means frames routed out of the connection core;
world/gameplay/entity modules handle many of them. It is not a missing-feature
count. The test probe is an integration harness, not the game executable.

New deterministic tests cover palettes at bits 0 and 4..15, inferred packed-word
lengths/order, all stored heights, negative chunk boundaries, eviction, truncated
chunk/light payloads, atomic inventory validation, no local click predictions,
action wire format/bounds, health, sound payloads, negative entity IDs, relative
movement, metadata/removal and respawn reset. Existing framing/identity/state,
backpressure/deadline and 2,000-input adversarial tests remain.

The visual smoke used the native X11/OpenGL binary and actual server, sent key
input for looking, walking/jumping and inventory, and captured screenshots. It is
a scripted visual inspection, not a full human-played survival acceptance session.
Mesa software rendering emitted the engine's performance/driver warnings. The
initial GUI/art is rough and needs further visual polish. Audio was muted in this
run; no listening acceptance is claimed. Local raw logs/screenshots are under
`build/` and are not packaged as game resources.

## GitHub CI record

Commit `babb558d321b5ee8d1b72eb66e9bea626e7268a4` passed both Linux and Windows
jobs in [LapisCube native client run 37977514794](https://github.com/aksulightning/lapisobsidian/actions/runs/37977514794).
Linux ran the unit/sanitizer/real-server/native-Classic checks and built a windowed
package. Windows cross-compiled with MinGW and packaged successfully. Both
`LapisCube-linux` and `LapisCube-windows` artifacts were uploaded. The CI sanitizer
command used normal leak detection, without the local sandbox exception.
This is build/test evidence; Windows runtime gameplay remains untested.

## Expanded survival acceptance

`make survival` writes an isolated saved-game fixture using the pinned server's
save structures, with a level test platform, workstations, items and two saved
players. It runs the **unchanged** server and shared product decoders/action API.
The client never injects inventory state or predicts crafting results. This proves
these server interactions, not resource gathering or a fresh-world survival run.

The first player crafts planks in 2×2, then a furnace in 3×3; deposits/reopens and
withdraws that furnace from a chest; verifies an unsupported smelting recipe
retains its inputs; smelts wood into charcoal; holds one food-use request until
hunger rises and the stack drops by one; places/edits a sign and receives accented
text plus an emoji encoded as modified-UTF-8 surrogate pairs; attacks the second
player; and uses `/spawn` then the server respawn path. The second player separately
observes lethal damage and requests respawn, verifying health and world reload.

```text
survival: 2x2 crafting consumed four logs and produced sixteen planks
survival: 3x3 crafting produced a furnace with no local predictions
survival: chest deposit, reopen and withdrawal preserved the stack
survival: unsupported smelting recipe retained inputs and produced no output
survival: furnace consumed fuel and input and returned server charcoal
survival: held food consumed once and raised authoritative hunger
survival: sign placement and UTF-8/modified-UTF-8 edit echo passed
survival: server command teleport synchronized
survival: respawn rebuilt the world and reset inventory/health
combat: second real client confirmed lethal damage and respawn
```

This exposed and fixed container player-slot initialization, repeated native Use
resetting the eating timer, and rejecting Lapis's valid negative saturation.
Unit tests additionally cover Unicode validation/surrogates, every truncation of
the sign NBT fixture, unchanged authoritative sign text before echo, chunk text
invalidation, all three menu aliases, mining tool/material/mode pacing, clock
bounds and dawn/day/dusk/night levels. New modules use the same sanitizer checks.

A native X11/Mesa session used the same seed/save supplies. Right-button holding
consumed one apple and raised food from 10 to 13. Four-line sign editing and its
saved overlay were inspected. Mining progress and release cancellation were
inspected. `/time set night` and `/time set day` changed native shading after
normal administrator authentication on the isolated test server; permissions
were unchanged. UI text remains CP437; unsupported glyphs may be substituted,
while untouched sign lines retain their original Unicode when saved.

Simultaneous initial joins revealed a reference-server issue: its join broadcast
can send Play packets to another client still in Configuration. The harness now
joins clients sequentially; strict client state validation remains intact. This
limitation is recorded in the packet matrix, not hidden by accepting wrong-state
packets. Repeated sequential reconnects remain covered by `make integration`.

## Milestone 7: fresh-world progression

Unlike the earlier seeded survival fixture, `tests/progression.py` creates a new
server directory containing only `server.txt`, selects seed 42/survival, and starts
without an administrator token. The client asserts that all 46 inventory cells
are empty. A bounded test-only walker searches the received terrain for a route
to a natural tree and sends small position steps. No terrain or inventory is
created by the client, and no server command grants supplies or movement.

The real run passed these steps:

1. Walk from spawn (8,68,8) to the natural tree at (9,69,2).
2. Mine and pick up four logs, using the product's mining delays.
3. Put logs into crafting, close without crafting, then refresh; require returned
   logs and authoritative empty input/result cells.
4. Craft sixteen planks, a workbench and four sticks. Use the new hotbar swap,
   place the workbench and require its block update and item consumption.
5. Open the placed workbench and craft a wooden pickaxe through ordinary clicks.
6. Drop and recover the remaining plank stack using the new slot-drop request.
7. Dig through natural soil, then use the pickaxe to mine/collect cobblestone.
8. Reconnect with the same identity; require seven planks, two sticks and one
   cobblestone. Tool survival is not assumed: this server uses random tool wear.

Both probe runs reached `progression_stage=30` and `result=PASS`. This verifies
first-tool progression and same-running-server reconnect, not disk persistence
across restart or a complete survival session. The test walker does not exercise
the native collision/jump implementation; longer native travel remains M8 work.

The native visual session first ran that fresh-world sequence, then connected the
windowed client as the same player. Inventory keyboard swapping, dropping one
plank, returning the stack, placing planks into crafting, closing and reopening
were inspected. The returned stack appeared in the inventory and stale crafting
cells cleared. A follow-up layout fix keeps shortcut text inside the panel and
hides the overlapping gameplay HUD while inventory is open. Audio was muted.

New strict-C89/sanitizer tests validate the no-op refresh wire format and cached
reports, no local slot mutation, truncated-response behavior, hotbar/drop packet
fields, per-window outgoing slot bounds and rejected closes. The earlier protocol,
seeded survival, status/reconnect/interaction and native Classic checks remain
separate regression gates. Linux CI now also runs `make progression`.

## Milestones 8–10: streaming, entity presentation and package usability

The extension slices were tested against the same unchanged reference server.
No command, movement, combat, inventory or authentication check was removed.

- `make test`: 2,396 protocol + 8,337 world/gameplay assertions (10,733 total),
  plus two Python package-gate tests. New cases cover dirty-column coalescing,
  eviction and coordinate extremes, atomic/truncated damage/equipment packets,
  unknown entity IDs and the independent swing/attack request sequence.
- `make sanitize` with the documented sandbox flag passes the same C assertions.
  Address/undefined-behavior checks remain enabled; this runner cannot perform
  the leak scan. Linux CI uses ordinary ASan/UBSan including its normal leak scan.
- `make travel` passes six administrator-authorized teleports: (17,17), (-17,-17),
  (33,-33), (3950,8), (-3950,-8), then the saved platform at (8,8). It decodes 175
  chunks, checks all cache coordinates after each move, and sees 119 evictions.
  The server's Far Lands begin at +/-3940, within its +/-4068 border.
- That scenario then spawns chicken, cow, pig, sheep, zombie, skeleton, spider
  and creeper through ordinary authenticated commands. All eight snapshots,
  skeleton main-hand bow item 858 and actual cow damage are required for success.
  This is an isolated saved-platform/presentation test, not survival progression.
- The existing status/login/reconnect/interaction, seeded crafting/container/
  food/sign/combat/death/respawn and empty-inventory first-tools tests pass. The
  second combat client now requires server animation, damage and death events
  before requesting respawn. It exposed a missing outgoing swing request, which
  was added without changing server attack rules.

`tests/windowed.py` now extracts the Linux ZIP into a new path containing spaces
and invokes `connect.sh` from a different working directory. A real X11/Mesa client
joins the server, opens inventory at 320x240, 640x360 and 1024x640, spawns and views
mobs, attacks a cow, visits signed boundaries and both Far Lands, and exits through
the window-close event. The screenshots were inspected: inventory stays on-screen,
models have distinct geometry, the cow turns red on server damage, and received
terrain appears at the travel destinations. Only the inventory sizes have this
visual acceptance; broader sign/menu accessibility remains an open gate.

Native travel testing exposed a packet-ordering problem: full-view chunk delivery
precedes the server's teleport, allowing old movement reports to trigger extra
recenters. The backend now gates movement during full-view synchronization and
locally requested travel, with a deadline and rejection recovery. On the same
five-teleport native script, full map copies fell from 16 to 6 (initial load plus
five destination changes); column copies fell from 494 to 189. These are measured
copy counts, not a claimed frame-rate improvement. The test requires exactly six
full copies, so an ordering regression fails CI.

The final recorded native run took 47.82 seconds, used 21.54 user + 1.28 system CPU
seconds, and peaked at 102,748 KiB RSS (about 100.3 MiB). It used software Mesa under
Xvfb with a 30 FPS limit and scripted input/screenshots. It is not a hardware or
idle-power benchmark, and no frame-time distribution was measured. Origin changes
still rebuild the dense map; Fancy lighting still uses conservative global mesh
refreshes. Long walking sessions and hitch elimination remain acceptance work.

The build has separate object directories for native, terminal and Windows modes.
Packaging refuses a terminal-mode stamp, mismatched ELF/MZ format, missing Linux
execute permission or a modified asset. `release.json` includes source/engine
revisions, dirty-source status and every packaged file's SHA-256. The ZIP checker
requires exact file membership, matching hashes, launchers, resources and notices;
it detects integrity errors, not release authenticity (there is no signature).
CI now runs travel, extracted-package windowed acceptance and ZIP validation,
and uploads native review images/metrics as a separate evidence artifact.

All native audio was muted. Windows remains cross-build/package evidence until
an actual Windows session is performed. Particles, full equipment, detailed mob
animations, final asset review and a complete human survival playthrough are not
claimed by this milestone start.

## Remaining acceptance gates

- Long-distance/chunk-boundary walking, cache hitches and long sessions beyond
  the tested signed-boundary/Far Lands teleport routes.
- Progression beyond the first tools, server-restart persistence, full-inventory
  crafting-close recovery, buckets,
  armour/equipment and complete mode-specific gameplay sessions.
- Mining pace balancing and animation; server owns drops/durability but does not
  provide a timed hardness table. Current non-instant timings are client choices.
- Further species art/metadata, armour, drops/projectiles, particles, text on sign planes,
  redstone/fluid interactions and musicbox/audio listening QA.
- Full Classic world/CPE regression, beyond the original login and untouched
  packet implementation. Atlas slots have independent compatibility substitutes.
- Windows runtime, other engine ports, clean-machine dependencies and performance.
- Full Unicode font/input, broader menu/accessibility testing and final asset/provenance review.

No generic vanilla-772 or completed M2–M6 compatibility claim is made. Finish
these gates before calling the project a complete survival release.
