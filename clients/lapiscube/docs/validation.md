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
| `make test` | 2,396 protocol assertions + 7,828 world/gameplay assertions passed under strict C89 warnings |
| ASan/UBSan (`SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS`) | Both suites passed; leak scanning disabled because `/proc` is unavailable in this runner |
| `make integration` | Actual status/pong, configuration, Play, decoded chunks, keepalive, same-identity reconnect and interaction sequence passed |
| Core C++ build | All six pure modules compiled with C++11 and warnings as errors |
| Linux X11/OpenGL build | Linked successfully using installed versioned runtime library names; packaged builds normally use development libraries |
| Windowed visual/input smoke | Actual native world rendered under Xvfb/Mesa; world, look, movement/jump and inventory screenshots inspected |
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

## Remaining acceptance gates

- Long-distance/chunk-boundary walking, Far Lands, cache hitches and teleport travel.
- Tool-specific mining durations, food/buckets, crafting results, chest/furnace use,
  armour/equipment, combat/damage/death/respawn and mode-specific gameplay sessions.
- Species art/metadata, drops/projectiles, particles, signs, redstone/fluid interaction
  and musicbox/audio listening QA. Shared models/synthesis are initial coverage.
- Full Classic world/CPE regression, beyond the original login and untouched
  packet implementation. Atlas slots have independent compatibility substitutes.
- Windows runtime, other engine ports, clean-machine dependencies and performance.
- Unicode chat conversion, accessibility/resizing and final asset/provenance review.

No generic vanilla-772 or completed M2–M6 compatibility claim is made. Finish
these gates before calling the project a complete survival release.
