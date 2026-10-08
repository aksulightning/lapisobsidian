# Alternative client asset provenance

**No bundled image, recording, font, favicon, logo or screenshot from
LabyStudio/js-minecraft is retained, downloaded or used. No additional asset
repository is introduced.** The root CC BY-NC code license is not sufficient
proof of rights in individual media. Upstream README acknowledges original-game
textures and links to a general sound-credit page without file-level licenses.
No adequate individual rights evidence was available; every media file below
is excluded. Source review is pinned to commit
`468f942c4984578a03e472c49646198680489723` of
https://github.com/LabyStudio/js-minecraft. The Git tree inventory was read
without retrieving media blobs.

| Excluded upstream files (under `src/resources/`) | Review result |
| --- | --- |
| `char.png`, `favicon.ico` | No independently verified permission/provenance |
| `gui/background.png`, `gui/font.png`, `gui/gui.png`, `gui/icons.png`, `gui/container/creative.png` | No independently verified asset license; code license not treated as asset permission |
| `gui/title/minecraft.png`, `gui/title/splash.png` | Proprietary/confusing branding; no reuse |
| `gui/title/background/panorama_0.png` through `panorama_5.png` | No independently verified image license |
| `misc/grasscolor.png`, `terrain/terrain.png`, `terrain/sun.png`, `terrain/moon.png` | No independently verified asset license |
| `sound/random/glass1.ogg` through `glass3.ogg` | General sound-credit link does not establish each recording's rights |
| `sound/step/cloth1.ogg` through `cloth4.ogg` | Same; all four excluded |
| `sound/step/grass1.ogg` through `grass4.ogg` | Same; all four excluded |
| `sound/step/gravel1.ogg` through `gravel4.ogg` | Same; all four excluded |
| `sound/step/sand1.ogg` through `sand4.ogg` | Same; all four excluded |
| `sound/step/snow1.ogg` through `snow4.ogg` | Same; all four excluded |
| `sound/step/stone1.ogg` through `stone4.ogg` | Same; all four excluded |
| `sound/step/wood1.ogg` through `wood4.ogg` | Same; all four excluded |

Screenshots under upstream `.github/assets/` are also excluded. This exclusion
record does not assert ownership of each file; unclear provenance alone is
sufficient to reject inclusion under the requested policy.

| Included resource group | Creator/source/license | Modifications/use |
| --- | --- | --- |
| `client2/adapter/resources.mjs`: in-memory terrain atlas | Original Lapis Obsidian contributor artwork, CC0-1.0 | Fresh deterministic 16×16 mineral/vegetation/liquid/wood patterns; no external image input |
| Same: UI sheets, abstract panorama, independent title, sun/moon, character | Original artwork, CC0-1.0 | Fresh shapes/colors; upstream layout/render code uses these buffers |
| Same: glyph bitmap | Browser-provided system monospace font rasterized locally | No font file shipped or downloaded; font rights remain with the installed font's author |
| `client2/adapter/audio.mjs`: generated audio buffers | Original synthesis design/output, CC0-1.0 | Original differentiated tone/noise envelopes replace recordings; upstream SoundManager class remains CC BY-NC |

| `client2/adapter/entities.mjs`: procedural models | Original Lapis Obsidian contributor designs, CC0-1.0 | Animal, humanoid, spider, creeper, ghast, item and projectile geometry made from fresh boxes; no imported models |
| `client2/adapter/item-icons.mjs`: item glyphs | Original Lapis Obsidian contributor designs, CC0-1.0 | Canvas shapes for tools, food, buckets, blocks and labeled fallback; no input media |

The generated artwork/audio dedication is in
`client2/licenses/ORIGINAL-ASSETS-CC0.txt`. `client2/provenance.json` records these
groups and all retained code hashes. Only code files, notices and route metadata
are committed; resources are created in memory. A source audit and manual review
verify the resource generator has no input asset or remote loader. A matching
file extension or keyword alone is not proof of copyright compliance.
