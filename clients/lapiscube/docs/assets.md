# Independent asset collection: investigation and next steps

No gameplay assets have been imported or generated for milestone 1. The empty
machine-readable manifest describes that actual state. Do not package an upstream
Minecraft asset cache as a substitute.

Initial source review (2026-10-09):

| Candidate | Evidence reviewed | Admission decision |
| --- | --- | --- |
| [Kenney Voxel Pack](https://kenney.nl/assets/voxel-pack) | Author's page explicitly lists Creative Commons CC0, 190 files and 128×128 tiles | Suitable candidate; inspect downloaded archive/license and specific files before import; choose a cohesive downsampling treatment |
| [Minetest conversion](https://github.com/minetest-mods/voxelpack_by_kenney) | README only states it is a WIP texture pack using Kenney textures; README blob `d753cc2537176c7cbdc180128dcc23f5f0c27e2b` | Not enough per-file/derivative licensing evidence from README; prefer author archive or verify every conversion's provenance |
| [Kenney Impact Sounds](https://kenney.nl/assets/impact-sounds) | Author's page lists Creative Commons CC0, 130 files | Candidate for material/contact sounds; archive and per-file mapping still needed |
| [Freesound](https://freesound.org/help/faq/#licenses) | Official FAQ lists CC0, attribution, noncommercial and legacy licensing | Only individually verified CC0 recordings; website membership is not proof |
| [OpenGameArt](https://opengameart.org/content/faq) | Official FAQ describes multiple license choices and compliance | Only individually verified CC0 submissions by default; no downloads approved by site-wide assumption |

For each admitted file, record `source_url`, `author`, exact `license` and
`license_url`, `modifications`, `destination_filename`, output `sha256`,
`attribution`, and `redistribution_notes` under `assets/manifest.json`.
Non-CC0 candidates require an explicit documented licensing decision. Keep the
source license evidence alongside the eventual bundle. Originals synthesized by
the project should have their generator path/revision as source and CC0 dedication.

M2 asset work starts with an original atlas and mapping for terrain, fluids,
plants and transparent blocks. Later add oriented utility blocks, item icons,
eight mob models/animations, players, drops/projectiles, and survival UI. A
packet identifier such as `minecraft:block.note_block.harp` is a mapping key,
not permission to ship the corresponding official sound file.

For sound, implement a deterministic small synthesis pipeline for UI clicks,
pickup/drop/eating/damage/impact/arrow effects and note timbres, supplemented by
verified CC0 material/animal recordings. Cover grass/stone/wood/dirt/sand/gravel
footsteps, break/place, fluids, doors/trapdoors/levers/plates, combat and mobs.
Lapis musicbox already emits named note events; map those to independent local
instruments, volume and pitch through ClassiCube's audio facilities. No official
Minecraft sounds or General MIDI soundfont of unverified licensing should be used.

The complete collection, mappings, listening/visual QA and installation packaging
remain milestones 2–6; these investigation notes are not an asset delivery.
