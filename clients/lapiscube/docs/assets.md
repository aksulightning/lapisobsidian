# Independent assets

The committed bundle contains CC0 Kenney/AFCMS textures, original CC0 models and
icons, recorded CC0 gameplay audio, and a public-domain bitmap fallback font.
No official Minecraft files are bundled or fetched by the Lapis path. The
launcher asset downloader is not an entrypoint; Lapis skin downloads are disabled.
Host/browser font files are not bundled. See the repository
[asset credits and license audit](../../../docs/credits.md) for per-file evidence
and the scope of the Linux/Windows/web build audit.

## Texture and model sources

Kenney's [Voxel Pack](https://kenney.nl/assets/voxel-pack) author archive:
https://kenney.nl/media/pages/assets/voxel-pack/a3a73d0ff7-1677662501/kenney_voxel-pack.zip

SHA-256: `667c05e3f6d95718aaef888c7fc06f7137ba5dede95f4574deb17d4436257958`.
Its included License.txt identifies Kenney Vleugels and Creative Commons Zero.
The exact notice is retained in `assets/licenses/Kenney-Voxel.txt`.
141 tile/item PNGs are resized from 128 to 32 pixels and packed into a 512×1024
atlas; water/ice alpha is adjusted and Classic cloth colors are tinted.
AFCMS `mc-like-textures` revision `45f167320f99632549be0672416b9caf52b83180`
provides 12 additional atlas sprites: six sword materials, diamond, stick, TNT
side/top/bottom, and the enchanting-table top. These 16-pixel images scale to
32 pixels with nearest-neighbor sampling. Two AFCMS hotbar images replace the
original UI placeholders. The diamond is by TechDudie; the other imported art
is by AFCMS. `assets/texture-sources.json` records all 14 source hashes, authors,
and runtime destinations. The retained CC0 license and contributor README live
in `assets/licenses/AFCMS-*`. Unsupported book/emerald items are not imported.

The first 256 atlas cells preserve the Classic/CPE texture layout with independent
substitutes. `assets/atlas.json` records locations. The rest hold Lapis images.

`tools/build_assets.py` packs the textures and authors the crosshair geometry.
`LapisMobs.c` authors ten geometric shapes: eight mob species, players, and a shared item/projectile shape. These
original designs and the pitched note-block synthesis are CC0; generator/renderer
code remains BSD-3-Clause. See `assets/licenses/LapisCube-Originals.txt`.

## Recorded audio

The 16 placeholder WAVs and generic noise synthesis have been replaced by **75
recorded PCM clips**, converted from **69 distinct CC0 source files**. The sources
are Kenney Impact Sounds 1.0 and individually credited files in Minetest Game at
`c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f`. Minetest Game is mixed-license; only the
specific CC0 recordings in `assets/audio-sources.json` are admitted.

- Material steps/digging use 52 clips across nine materials. Each group has one
  to five actual recordings, including real metal sounds rather than the old
  stone substitution. The web loader and native ZIP use the same variant list.
- Door open/close uses matching CC0 glass-door recordings by SkeetMasterFunk69.
  Fence-gate opening retains its separate CC0 mhtaylor67 recording. Snow steps
  now use all five CC0 Ryding variants.
- Protocol effects use recorded impacts, door/gate foley, pickup bells, water,
  lava, clicks, eating/crunch foley, and percussion. Consecutive event variants
  do not repeat when a group has more than one clip.
- Harp, bass, and bell/chime note-block tones retain original pitched synthesis
  so server pitch and musicbox playback remain functional. Percussive note
  instruments use recordings. This is not a General MIDI bank.
- Unsupported species ambient calls and unknown events are silent. Hurt/death
  events share impact foley; species-specific voices are still missing. Other door
  materials and chest/gate closing share door foley; eating, explosions and
  percussion are substitutes, not exact recordings of each game event. Water/lava
  are short one-shots, not looping environmental ambience.

`assets/sound-map.json` maps material and event groups to exact filenames.
`tools/embed_audio.py` verifies the manifest and embeds only the protocol-event
PCM in the staged native/WebAssembly build, deduplicating samples shared between
events. Both platforms use the same recordings, pitch, distance attenuation and
volume; web protocol effects need no asynchronous file fetch. WebAudio caps
concurrent event sources at 16 and stops them on disconnect. The native audio
pool manages native concurrency. Step/dig sounds use the engine's material bank.

Every bundled PNG/SVG/WAV and embedded icon/font resource has source, author,
license, modifications, destination and SHA-256 in `assets/manifest.json`; recorded clips also carry source hashes,
source filenames, original-source URLs and retained license evidence. Packaging
verifies output hashes. The source selection and archive/revision pins live in
`assets/audio-sources.json`. License evidence is in `assets/licenses/`.

## Reproduction

Ordinary builds use committed WAVs and need only Python's standard library to
embed event PCM; no ffmpeg, network, or source archive is needed.

To regenerate audio, install ffmpeg, download the pinned
[Kenney Impact Sounds ZIP](https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip)
to `build/kenney-impact-sounds.zip`, and check out Minetest Game at the pinned
revision in `build/minetest-game`. From `clients/lapiscube/`, run:

```sh
python3 tools/build_audio.py --kenney build/kenney-impact-sounds.zip --minetest build/minetest-game
```

The importer verifies the archive, revision, retained notices and each selected
source file before replacing the generated WAVs. It converts to mono signed
16-bit PCM at 22,050 Hz, trims silence, caps duration/peak level, limits gain
boost to 6 dB, and applies short attack/release fades. It updates the manifest
and sound map. Rebuild the client after changing event recordings.

To regenerate textures, put the pinned Voxel Pack ZIP at `build/kenney-voxel.zip`,
install Pillow, and check out the pinned AFCMS revision at `build/mc-like-textures`.
Run `python3 tools/build_assets.py --afcms build/mc-like-textures`. This verifies
each source hash/license and regenerates the atlas, UI and numeric compatibility facts from
the pinned server. It preserves recorded-audio provenance and does not overwrite
WAVs with synthetic effects. Native icon/font entries are also preserved.

To regenerate the embedded artwork, check out `dhepper/font8x8` revision
`8e279d2d864e79128e96188a6b9526cfa3fbfef9` at `build/font8x8`, install Pillow and
CairoSVG, and run:

```sh
python3 tools/build_native_assets.py --font8x8 build/font8x8
```

The generator verifies the font revision, source header and license README
hashes. It selects 94 printable ASCII glyphs in the renderer's LSB-first bit
order, and rasterizes `assets/web-icon.svg` into Windows and X11 icons.
`prepare_engine.py` checks manifest coverage/hashes and replaces the upstream
font/icon resources in the staged engine for normal builds. The engine submodule
is unchanged. Windows icon edits also invalidate the compiled resource object.

Run `make test` to exercise protocol/gameplay, recorded event routing, variant
selection, pitch/volume/distance, allocation failure/cleanup, PCM integrity,
license/hash coverage, native/web packaging parity, texture mappings and staged
icon/font replacement.

## Remaining artwork and quality work

Several utility blocks/items still use material fallbacks. The eight mob species
have distinct original geometry, but remote armour, detailed animations, species
voices, particle art, full sign text and complete menu artwork remain incomplete.
Audio integrity and routing tests do not replace an in-game listening review on
speakers/headphones. See the central credits for imported sources and remaining
coverage. LibreProg is BSD-3-Clause and was excluded from this public-domain/CC0 selection.
