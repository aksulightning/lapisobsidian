# Milestone progress

All milestones are in progress except the verified M1 connection foundation.
“Started” describes working code, not completion of the original acceptance list.

| Milestone | Implemented start | Evidence | Remaining work |
| --- | --- | --- | --- |
| 1: protocol foundation | Offline status/login/configuration/Play, registry identifiers, keepalive/teleport, bounded nonblocking I/O | Actual unchanged server; framing/adversarial tests; reconnect | Async DNS, broader transport/platform validation; no generic vanilla claim |
| 2: exploration | 24-section palette decoder, Y=0..255, bounded streamed cache, negative-coordinate origin translation, native terrain/collision/movement and updates | All palette bit widths tested; 25 real chunks decoded; windowed world/look/move screenshots inspected | Efficient recenter meshing, lighting/day/night, full special models, Far Lands/long-distance sessions |
| 3: interaction | Targeting, sequenced digging/placement, authoritative hotbar/stacks, pickup/drop, cursor transfers | Real survival-server removal → pickup → transfer → drop → pickup → placement with inventory consumption | Tool hardness/progress, equip animation, complete item-use coverage and long inventory consistency sessions |
| 4: survival | Health/food HUD, inventory/2×2/3×3/chest/furnace grids, request-only crafting clicks, food/release/attack/respawn actions, entity snapshots and original shared animated models | Parser/action tests, actual inventory round trip, native inventory screen inspected | Craft/container/combat/eating/death end-to-end tests; species-specific art, armour/equipment/metadata effects; complete game modes |
| 5: Lapis features | Door/trapdoor collision variants, crops/farmland, simple signs/redstone/plates/fluid states, named sounds and note timbres for server musicbox events | Block mapping from inspected source; real overlays decoded; sound parsing tests | Sign text/editor, circuit geometry, fluid levels, particles, note/musicbox listening QA and interaction acceptance |
| 6: polish/package | Verified Kenney CC0 atlas/icons, original geometric art and 16 synthesized WAVs, hashed manifest, Linux/Windows packaging and CI | Native Linux rendering/build and manifest verification | Windows runtime testing; performance tuning, species sounds/art, accessible/resizable UI, full clean-install survival acceptance and final provenance review |

Priorities next: run a complete fresh survival session; fix mining timing and
container/food/combat issues found there; validate movement across several cache
boundaries and server teleports; then improve art, lighting and mesh updates.
