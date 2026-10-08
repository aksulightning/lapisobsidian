# Client asset sources

The machine-readable inventory is `client/public/assets/manifest.json`:
**47 files**, each with exact path, creator, source, license, evidence, use,
modification status and SHA-256. Reviewed 2026-10-08. This is an inventory of
selected files, not an assertion that every asset in a source repository is safe.

## Selected external terrain group

Creator: **ARoachIFoundOnMyPillow**. Work: **16x16 Block Texture Set**.
Original source: https://opengameart.org/content/16x16-block-texture-set
Original archive: https://opengameart.org/sites/default/files/blocks_2.zip
Exact license: **CC0 1.0 Universal**; the original creator page explicitly links
CC0. License: https://creativecommons.org/publicdomain/zero/1.0/legalcode

Mirror: https://github.com/EBonura/voxide, commit
`066f483baacbf926507b4fcaa63caab46e524f51`.
Each source path below is under `assets/pack/blocks/blocks/`; mirror credits are
[assets/pack/CREDITS.md](https://github.com/EBonura/voxide/blob/066f483baacbf926507b4fcaa63caab46e524f51/assets/pack/CREDITS.md).
The credits identify the original pack and CC0. Every selected PNG was compared
byte-for-byte to the same filename under `blocks/` in the original archive; all
13 matched. A nearest-scaled contact sheet was manually reviewed. No modification
was made other than local filename changes. Runtime atlas placement leaves pixels
unchanged. Uses: block faces and block-item icons.

| Local `client/public/assets/textures/terrain/` | Original filename |
| --- | --- |
| loam.png | dirt.png |
| meadow.png | grass_top.png |
| meadow-edge.png | grass_side.png |
| rock.png | stone_generic.png |
| pebble.png | gravel.png |
| dune.png | sand_ugly.png |
| bark.png | oak_log_side.png |
| rings.png | oak_log_top.png |
| boards.png | oak_planks.png |
| foliage.png | oak_leaves.png |
| frost.png | snow.png |
| void.png | obsidian.png |
| pane.png | glass.png |

No VoXide source code, converted Rust atlas, audio bank, disc images, other
textures or sounds are included. Sound recordings referenced in its credits were
not imported; each would need its own primary-source verification before reuse.

## Original homogeneous groups

`client/public/assets/textures/original/*.svg`: **33 original 16x16 pixel designs**,
created for Lapis Obsidian Client. Generator: `client/scripts/generate-originals.mjs`,
including exact palettes, shape algorithms and deterministic seeds (tile index
+17). Creator: Lapis Obsidian Client contributors. License: CC0-1.0, dedication
in `client/public/assets/LICENSE.md`. Uses: missing textures, additional block
materials, fluids, vegetation, ores, doors/signs/containers, tools/weapons/items,
player/entity surfaces, particles and UI art. No imported illustrations or
proprietary placeholders. The explicit manifest lists every filename and hash.

`client/public/assets/audio/events.json`: **original sound design parameters**,
same creator and CC0 dedication, no recordings. Unmodified source is that file;
`src/audio/audio.mjs` interprets the parameters with independently written GPL
code. Includes step/break/place/tool/collect/click/hurt/entity/ambient/water/door/
eat/note designs. Unknown events and failed loads produce silence. These are
original abstract synthesized effects, not imitations or extracts of proprietary
recordings. The code's license is distinct from its sound definitions.

CSS panels/buttons, standard geometric entity models and selection markers are
original source-rendered UI/geometry, rather than retained external asset files.
Generator/rendering code remains GPL. No logo, font file or splash art is imported.

## Other reviewed source

[Tiddybub/2d-assets](https://github.com/Tiddybub/2d-assets), commit
`e0cbe0d995554a490d4c182fe9beb8769ffbb606`: reviewed root README, LICENSE and terrain
pack index. This is a large curated collection with per-pack SOURCE.md records;
its blanket CC0 statement was not used as evidence for arbitrary files. No files
were selected: the small original-creator-verified terrain subset above and
original missing art provide the current consistent palette without another pack.
Do not infer that unreviewed collection files have been approved.
