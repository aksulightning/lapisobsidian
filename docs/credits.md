# Asset credits and license audit

Reviewed on **2026-10-10** against `testing-cube` commit
[`04b9fea43097d362e068ba04f0e2b9778285ceb2`](https://github.com/aksulightning/lapisobsidian/tree/04b9fea43097d362e068ba04f0e2b9778285ceb2).

**The 22 media files in `clients/lapiscube/assets/` are covered by CC0-1.0
notices.** The pinned ClassiCube engine also embeds icons and a fallback font
without a separate public-domain/CC0 declaration in the inspected sources, so
this is not a claim that every asset in the full client is CC0. Replacement
candidates for those exceptions and optional texture/audio improvements are
listed below. No candidate assets were imported by this documentation change.

## Assets currently included

Paths in this table are relative to `clients/lapiscube/`.

| Asset | Creator / source | License and modifications |
| --- | --- | --- |
| `assets/terrain.png` | Kenney Vleugels / [Kenney Voxel Pack](https://kenney.nl/assets/voxel-pack) | **CC0-1.0**. 141 tile/item images resized from 128 to 32 pixels and packed into a 512 × 1024 atlas; water/ice transparency adjusted, cloth tinted, Classic/CPE cells mapped to substitutes. |
| `assets/gui.png`, `assets/gui_classic.png`, `assets/icons.png`, `assets/lapis-entities.png` | LapisCube contributors / [asset generator](../clients/lapiscube/tools/build_assets.py) | **CC0-1.0**. Original generated UI/entity artwork. |
| `assets/web-icon.svg` | LapisCube contributors / [original vector icon](../clients/lapiscube/assets/web-icon.svg) | **CC0-1.0**. Copied to `icon.svg` in the web package. |
| `assets/audio/*.wav` (16 files, enumerated below) | LapisCube contributors / [asset generator](../clients/lapiscube/tools/build_assets.py) | **CC0-1.0**. Original deterministic synthesized mono, 16-bit PCM audio at 22,050 Hz. |
| Geometric player, mob, item and projectile designs | LapisCube contributors / [LapisMobs.c](../clients/lapiscube/src/LapisMobs.c) | **CC0-1.0** for the original model designs; rendered from code. |
| Eight runtime sound banks | LapisCube contributors / [LapisAudio.c](../clients/lapiscube/src/LapisAudio.c) | **CC0-1.0** for the synthesized audio output; these are separate from the WAV files. |

The WAV files are `cloth.wav`, `door.wav`, `eat.wav`, `glass.wav`, `grass.wav`,
`gravel.wav`, `hurt.wav`, `lava.wav`, `metal.wav`, `note.wav`, `pickup.wav`,
`sand.wav`, `snow.wav`, `stone.wav`, `water.wav`, and `wood.wav`.

The exact existing notices are [Kenney-Voxel.txt](../clients/lapiscube/assets/licenses/Kenney-Voxel.txt)
and [LapisCube-Originals.txt](../clients/lapiscube/assets/licenses/LapisCube-Originals.txt).
They refer to [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)
([legal text](https://creativecommons.org/publicdomain/zero/1.0/legalcode)).
Attribution is not required by these CC0 notices; the credits here acknowledge
the creators and preserve provenance. The original generator/renderer code
remains BSD-3-Clause; the server remains under its existing GPL terms. Those
code licenses do not replace the separate asset notices.

### Source and integrity evidence

- [manifest.json](../clients/lapiscube/assets/manifest.json) records each of the
  22 media files, its creator, source, license, modifications and SHA-256.
  Every hash matched at the audited commit, and no PNG, SVG or WAV in the client
  asset directory was missing from the manifest.
- The [pinned Kenney archive](https://kenney.nl/media/pages/assets/voxel-pack/a3a73d0ff7-1677662501/kenney_voxel-pack.zip)
  has SHA-256 `667c05e3f6d95718aaef888c7fc06f7137ba5dede95f4574deb17d4436257958`,
  matching [source.json](../clients/lapiscube/assets/source.json). Its included
  `License.txt` exactly matches the retained Kenney notice.
- The five PNGs and sixteen WAVs were regenerated in an isolated directory using
  that archive and the checked-in generator. All 21 outputs matched byte-for-byte.
  The SVG was checked against its manifest and original CC0 notice, not generated.
- [atlas.json](../clients/lapiscube/assets/atlas.json) records texture placement;
  [sound-map.json](../clients/lapiscube/assets/sound-map.json) records audio routing.
  These checks establish correspondence with the documented sources, not visual
  quality or listening acceptance.

## Engine assets outside the CC0 manifest

The engine is pinned to ClassiCube
[`d41c3f7eef2038f59702b58bdb373483fb0d28f9`](https://github.com/ClassiCube/ClassiCube/tree/d41c3f7eef2038f59702b58bdb373483fb0d28f9).
Its [credits](https://github.com/ClassiCube/ClassiCube/blob/d41c3f7eef2038f59702b58bdb373483fb0d28f9/credits.txt)
identify **Goodlyay** as the icon and web-texture artist. Its
[license file](https://github.com/ClassiCube/ClassiCube/blob/d41c3f7eef2038f59702b58bdb373483fb0d28f9/license.txt)
contains BSD-3-Clause and dependency notices, not a blanket CC0 dedication.

| Asset / evidence | Audit finding | Public-domain/CC0 alternative |
| --- | --- | --- |
| `engine/misc/CCicon.ico`, embedded by `misc/windows/CCicon.rc` and the Windows makefile; `misc/x11/CCIcon_X11.h`, embedded by `src/Window_X11.c` | Native builds retain upstream icon artwork. No separate CC0 notice found; credit Goodlyay and retain upstream notices. The web icon does not replace these native resources. | Generate native icon resources from the existing CC0 `assets/web-icon.svg` and wire them into the staged builds. |
| `engine/src/SystemFonts.c`, `font_bitmap` | Embedded 8 × 8 fallback glyphs explicitly cite Goodly's ClassiCube texture pack. No separate CC0 notice found. Using host fonts does not remove this compiled fallback. | [Daniel Hepper's font8x8](https://github.com/dhepper/font8x8/tree/8e279d2d864e79128e96188a6b9526cfa3fbfef9), declared **Public Domain** in its [README](https://github.com/dhepper/font8x8/blob/8e279d2d864e79128e96188a6b9526cfa3fbfef9/README) and [basic font header](https://github.com/dhepper/font8x8/blob/8e279d2d864e79128e96188a6b9526cfa3fbfef9/font8x8_basic.h), with upstream credits to Marcel Sondaar and IBM. Adapt glyph indexing/bit order and verify rendering before replacing the table. |
| `engine/misc/cc_textures.zip`, `engine/misc/cc_audio.zip`, other platform artwork/archives | Present in the upstream checkout but not selected by the current LapisCube Linux/Windows/web package scripts. Outside the 22-file CC0 manifest; do not treat them as cleared CC0 sources. | Continue packaging the audited LapisCube asset bundle; review separately if adding other platform targets or using upstream distribution targets. |

Host/browser fonts are used without bundling their font files. User-selected
texture packs are not redistributed by the current package scripts. This audit
does not assign a CC0 license to either category. Existing engine license and
credit notices must still be retained; see [client NOTICE.md](../clients/lapiscube/NOTICE.md).

## Suggested texture sources reviewed

These are candidates, not credits for material currently shipped.

| Source and reviewed revision | License evidence | Suitability for a public-domain/CC0 asset set |
| --- | --- | --- |
| [OfficialPixelBrush / LibreProg](https://github.com/OfficialPixelBrush/LibreProg/tree/1121a9595593800bf3967ba4273a4272ce51717e) | [LICENSE](https://github.com/OfficialPixelBrush/LibreProg/blob/1121a9595593800bf3967ba4273a4272ce51717e/LICENSE): **BSD-3-Clause**, copyright 2026 the LibreProg artists and developers. | **Not CC0/public domain.** A permissive option only if accepting BSD assets: preserve copyright, conditions and disclaimer in source and binary distributions, and respect the non-endorsement clause. Keep Kenney or use the AFCMS CC0 subset below for a strict CC0 collection. |
| [AFCMS / mc-like-textures](https://github.com/AFCMS/mc-like-textures/tree/45f167320f99632549be0672416b9caf52b83180) | [LICENSE.txt](https://github.com/AFCMS/mc-like-textures/blob/45f167320f99632549be0672416b9caf52b83180/LICENSE.txt): **CC0-1.0**. [README](https://github.com/AFCMS/mc-like-textures/blob/45f167320f99632549be0672416b9caf52b83180/README.md) credits **TechDudie** for diamond/emerald artwork and **AFCMS** for the other material. | **CC0 candidate.** Only 36 PNGs: selected items, swords, TNT, character, HUD and particles. Useful supplementation, not a complete terrain atlas replacement. Four XCF source files are also present. |
| [Kenney Voxel Pack](https://kenney.nl/assets/voxel-pack) | Author's page and the verified archive notice both say **CC0**. | Retain as the existing broad texture base. The original archive also contains media beyond the tile/item images currently packed. |

Examples to evaluate from AFCMS are `ITEMS/default_diamond.png`,
`ITEMS/mcl_core_emerald.png`, `TOOLS/default_tool_woodsword.png`,
`NODES/default_tnt_side.png`, and `particles/mcl_particles_smoke.png`.
Any adoption needs explicit atlas/item mappings and updated provenance; copying
those filenames into the asset directory alone will not connect them to the game.

## Suggested sound sources reviewed

### Minetest Game / Luanti: select individual CC0 files

Reviewed `luanti-org/minetest_game` at
[`c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f`](https://github.com/luanti-org/minetest_game/tree/c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f).
The [`default` media license](https://github.com/luanti-org/minetest_game/blob/c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f/mods/default/license.txt)
and [per-file sound credits](https://github.com/luanti-org/minetest_game/blob/c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f/mods/default/README.md#sounds)
show **mixed licenses**, including CC BY-SA 3.0, CC BY 3.0 and CC0.
The code's LGPL license is not the sounds' license.

The following **26 of 77 OGG files** are explicitly identified as CC0 by the
upstream sound credits. Paths are relative to
[`mods/default/sounds/`](https://github.com/luanti-org/minetest_game/tree/c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f/mods/default/sounds).
Number lists identify exact variants, not permission to import an entire folder.
Suggested uses are candidates for auditioning; none have been integrated or
accepted in a listening review.

| Files (all CC0 per upstream credits) | Creator and original source | Possible use |
| --- | --- | --- |
| `default_dug_metal.1.ogg`, `.2.ogg` | [Iwan Gabovitch / qubodup](https://opengameart.org/users/qubodup) | Metal breaking |
| `default_metal_footstep.1.ogg`, `.2.ogg`, `.3.ogg` | [mypantsfelldown](https://freesound.org/people/mypantsfelldown/sounds/398937/) | Metal steps |
| `default_place_node_metal.1.ogg`, `.2.ogg` | [Ogrebane](https://opengameart.org/content/wood-and-metal-sound-effects-volume-2) | Metal placement |
| `default_dig_snappy.ogg` | [blukotek](https://freesound.org/people/blukotek/sounds/251660/) | Plant/snapping effects |
| `default_snow_footstep.1.ogg` through `.5.ogg` | [Ryding](https://freesound.org/people/Ryding/sounds/94337/) | Snow steps |
| `default_item_smoke.ogg` | Ferk, based on [bart](https://opengameart.org/users/bart) | Short smoke effect |
| `default_dig_choppy.1.ogg`, `.2.ogg`, `.3.ogg` | [Sheyvan](https://freesound.org/people/Sheyvan/sounds/476113/) | Wood chopping |
| `default_gravel_dig.1.ogg`, `.2.ogg`; `default_gravel_dug.1.ogg`, `.2.ogg`, `.3.ogg` | [lolamadeus](https://freesound.org/people/lolamadeus/sounds/179341/) | Gravel digging/breaking |
| `default_sand_footstep.1.ogg`, `.2.ogg`, `.3.ogg` | [worthahep88](https://freesound.org/people/worthahep88/sounds/319224/) | Sand steps |
| `default_furnace_active.ogg` | [iankath](https://freesound.org/people/iankath/sounds/173991/) | Furnace ambience |

The per-file classifications above come from the pinned Minetest Game credits;
the original-source links preserve its provenance references. In particular,
CC0 metal breaking is not the same as `default_dig_metal.ogg` (CC BY 3.0), and
CC0 gravel digging is not the same as `default_gravel_footstep.*.ogg` (CC BY-SA 3.0).

Do **not** import the whole sound directory for a CC0-only collection:

- Mito551's grass, dirt, wood, gravel/glass footsteps and other listed placement/
  digging sounds are **CC BY-SA 3.0**.
- Glass breaking, metal digging, tool breaking, water footsteps, player damage,
  cracky digging, hard footsteps and the listed ice sounds are **CC BY 3.0**.
- Chest open/close effects mix CC0 and **CC BY 3.0** recordings; the resulting
  mixes are not CC0-only.
- `default_cool_lava.1.ogg` through `.3.ogg` have no explicit CC0 entry in the
  inspected sound credits. Do not include them in the CC0 selection.

### Kenney Impact Sounds: wholly CC0 alternative

[Kenney Impact Sounds](https://kenney.nl/assets/impact-sounds) contains **130 OGG
effects** and is explicitly CC0 on the author's page and in the archive's
`License.txt`. Creator: **Kenney**. The reviewed
[version 1.0 archive](https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip)
has SHA-256 `029d734af1582474edf3a694d1b0cebc97c1c152f2f39fa34d4c2bafc5de77f8`.

Candidates include `Audio/footstep_grass_000.ogg`, `footstep_wood_000.ogg`,
`footstep_concrete_000.ogg`, `footstep_carpet_000.ogg`, `footstep_snow_000.ogg`,
`impactGlass_light_000.ogg`, `impactMetal_light_000.ogg`, `impactMining_000.ogg`,
and `impactWood_light_000.ogg`, all under `Audio/`, with numbered variants.
This offers CC0 alternatives to several of Minetest's attribution/share-alike
sound families. It is an audition shortlist, not a claim of complete event coverage.

## Recommended next asset work

1. Keep the verified Kenney atlas and original CC0 artwork/audio as the baseline.
   Address the native icons and embedded fallback font before claiming all
   distributed audiovisual assets are public domain/CC0.
2. Evaluate AFCMS for specific missing item/HUD/particle textures; retain its
   license and author mapping if importing. Exclude LibreProg under a strict
   CC0-only policy unless its authors offer an applicable CC0 release.
3. Audition Kenney Impact Sounds and the explicit Minetest CC0 selection for
   material steps/digging. Preserve each imported file's source revision,
   author, license evidence, source/output hashes and conversion details.
4. Update the asset generator, manifest and packaging together. Current packaging
   maps nine material WAVs to step/dig names; web variants currently duplicate
   the same WAV. Preserve compatible PCM output when converting OGG recordings.
   Protocol interaction, mob and note events instead use `LapisAudio.c`'s runtime
   synthesis: replacing `door.wav` or `hurt.wav` alone will not improve those events.
   Verify native/web playback, variant selection, levels and loops before shipping.
