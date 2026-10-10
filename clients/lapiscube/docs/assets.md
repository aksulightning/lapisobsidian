# Independent assets

The committed bundle contains a verified Kenney CC0 atlas, original CC0 geometric
UI/models, and recorded CC0 gameplay audio. No official Minecraft files are
bundled or fetched by the Lapis path. The launcher asset downloader is not an
entrypoint; Lapis entity skin downloads are disabled. Host/browser font files
are not bundled, but the engine includes a bitmap fallback font and native icon
artwork outside the asset manifest. See the repository
[asset credits and license audit](../../../docs/credits.md) for these exceptions
and replacement candidates.

## Texture and model sources

Kenney's [Voxel Pack](https://kenney.nl/assets/voxel-pack) author archive:
https://kenney.nl/media/pages/assets/voxel-pack/a3a73d0ff7-1677662501/kenney_voxel-pack.zip

SHA-256: `667c05e3f6d95718aaef888c7fc06f7137ba5dede95f4574deb17d4436257958`.
Its included License.txt identifies Kenney Vleugels and Creative Commons Zero.
The exact notice is retained in `assets/licenses/Kenney-Voxel.txt`.
141 tile/item PNGs are resized from 128 to 32 pixels and packed into a 512×1024
atlas; water/ice alpha is adjusted and Classic cloth colors are tinted.
The first 256 atlas cells preserve the Classic/CPE texture layout with independent
substitutes. `assets/atlas.json` records locations. The rest hold Lapis images.

`tools/build_assets.py` authors the UI geometry. `LapisMobs.c` authors ten geometric
shapes: eight mob species, players, and a shared item/projectile shape. These
original designs and the pitched note-block synthesis are CC0; generator/renderer
code remains BSD-3-Clause. See `assets/licenses/LapisCube-Originals.txt`.

## Recorded audio

The 16 placeholder WAVs and generic noise synthesis have been replaced by **74
recorded PCM clips**, converted from **68 distinct CC0 source files**. The sources
are Kenney Impact Sounds 1.0 and individually credited files in Minetest Game at
`c42e4d0c0ff9d27ff7b9b308c3cfc14098dd3a0f`. Minetest Game is mixed-license; only the
specific CC0 recordings in `assets/audio-sources.json` are admitted.

- Material steps/digging use 50 clips across nine materials. Each group has one
  to three actual recordings, including real metal sounds rather than the old
  stone substitution. The web loader and native ZIP use the same variant list.
- Protocol effects use recorded impacts, door/gate foley, pickup bells, water,
  lava, clicks, eating/crunch foley, and percussion. Consecutive event variants
  do not repeat when a group has more than one clip.
- Harp, bass, and bell/chime note-block tones retain original pitched synthesis
  so server pitch and musicbox playback remain functional. Percussive note
  instruments use recordings. This is not a General MIDI bank.
- Unsupported species ambient calls and unknown events are silent. Hurt/death
  events share impact foley; species-specific voices are still missing. Door
  closing, eating, explosions, and percussion are foley substitutes, not exact
  recordings of each game event. Water/lava are short one-shots, not looping
  environmental ambience.

`assets/sound-map.json` maps material and event groups to exact filenames.
`tools/embed_audio.py` verifies the manifest and embeds only the protocol-event
PCM in the staged native/WebAssembly build, deduplicating samples shared between
events. Both platforms use the same recordings, pitch, distance attenuation and
volume; web protocol effects need no asynchronous file fetch. WebAudio caps
concurrent event sources at 16 and stops them on disconnect. The native audio
pool manages native concurrency. Step/dig sounds use the engine's material bank.

Every bundled PNG/SVG/WAV has source, author, license, modifications, destination
and SHA-256 in `assets/manifest.json`; recorded clips also carry source hashes,
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
install Pillow, and run `python3 tools/build_assets.py`. This verifies its source
hash/license and regenerates the atlas, UI and numeric compatibility facts from
the pinned server. It preserves recorded-audio provenance and does not overwrite
WAVs with synthetic effects.

Run `make test` to exercise protocol/gameplay, recorded event routing, variant
selection, pitch/volume/distance, allocation failure/cleanup, PCM integrity,
license/hash coverage, and native/web packaging parity.

## Remaining artwork and quality work

Several utility blocks/items still use material fallbacks. The eight mob species
have distinct original geometry, but remote armour, detailed animations, species
voices, particle art, full sign text and complete menu artwork remain incomplete.
Audio integrity and routing tests do not replace an in-game listening review on
speakers/headphones. See the central credits for AFCMS texture candidates and
remaining engine icon/font licensing work.
