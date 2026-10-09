# Milestone progress

All milestones are in progress except the verified M1 connection foundation.
“Started” describes working code, not completion of the original acceptance list.

| Milestone | Implemented start | Evidence | Remaining work |
| --- | --- | --- | --- |
| 1: protocol foundation | Offline status/login/configuration/Play, registry identifiers, keepalive/teleport, bounded nonblocking I/O | Actual unchanged server; framing/adversarial tests; reconnect | Async DNS, broader transport/platform validation; no generic vanilla claim |
| 2: exploration | 24-section palette decoder, Y=0..255, bounded streamed cache, negative-coordinate origin translation, native terrain/collision/movement and updates, server-clock day/night shading | All palette bit widths tested; 25 real chunks decoded; windowed world/look/move screenshots inspected | Efficient recenter meshing, modern light propagation, full special models, Far Lands/long-distance sessions |
| 3: interaction | Targeting, sequenced digging/placement, authoritative hotbar/stacks, pickup/drop, cursor transfers, tool-sensitive mining progress/cancellation | Real survival-server removal → pickup → transfer → drop → pickup → placement with inventory consumption | Mining pacing/art refinement, equip animation, complete item-use coverage and long inventory consistency sessions |
| 4: survival | Health/food HUD, inventory/2×2/3×3/chest/furnace grids, request-only crafting clicks, food/release/attack/respawn actions, entity snapshots and original shared animated models | Two-client real-server crafting/chest/furnace/eating/combat/death/respawn scenario; native food and inventory inspected | Fresh-world progression and full-inventory edge cases; species-specific art, armour/equipment/metadata effects; complete game modes |
| 5: Lapis features | Door/trapdoor collision variants, crops/farmland, sign text/editor, simple redstone/plates/fluid states, named sounds and note timbres for server musicbox events | Block mapping from inspected source; real overlays decoded; sound parsing tests; real sign Unicode echo and native editor | Text on sign planes, circuit geometry, fluid levels, particles, note/musicbox listening QA and interaction acceptance |
| 6: polish/package | Verified Kenney CC0 atlas/icons, original geometric art and 16 synthesized WAVs, hashed manifest, Linux/Windows packaging and CI | Native Linux rendering/build, manifest verification, survival acceptance in CI | Windows runtime testing; performance tuning, species sounds/art, accessible/resizable UI, full clean-install survival acceptance and final provenance review |

Priorities next: test fresh-world survival progression and full-inventory close
recovery; validate several streamed chunk boundaries, Far Lands and long sessions;
then improve species art, particles, equipment, lighting and incremental meshing.
Do not treat the seeded acceptance scenario as completion of these gates.
