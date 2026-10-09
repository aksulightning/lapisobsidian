# Independent assets

The committed bundle contains a verified Kenney CC0 atlas and original geometric
UI/models and synthesized sounds. No official Minecraft files are bundled or
fetched by the Lapis path. The launcher asset downloader is not an entrypoint;
Lapis entity skin downloads are disabled. Fonts come from the host's installed
system fonts and are not bundled.

## Admitted sources

Kenney's [Voxel Pack](https://kenney.nl/assets/voxel-pack) author archive:
https://kenney.nl/media/pages/assets/voxel-pack/a3a73d0ff7-1677662501/kenney_voxel-pack.zip

SHA-256: `667c05e3f6d95718aaef888c7fc06f7137ba5dede95f4574deb17d4436257958`.
Its included License.txt explicitly identifies Kenney Vleugels and Creative
Commons Zero. The exact notice is retained in `assets/licenses/Kenney-Voxel.txt`.
141 tile/item PNGs were resized from 128 to 32 pixels and packed into a 512×1024
atlas; water/ice alpha was adjusted, and Classic cloth colors were tinted.
The first 256 atlas cells preserve the Classic/CPE texture layout with independent
substitutes. `assets/atlas.json` records locations. The rest hold Lapis images.

`tools/build_assets.py` authors the UI geometry and 16 deterministic PCM effects.
`LapisMobs.c` authors ten geometric shapes: eight distinct initial mob species,
players, and a shared item/projectile shape. `LapisAudio.c` creates eight runtime
synthesis banks for named protocol events. The audiovisual output and geometric
model designs are dedicated under CC0; generator/renderer code remains BSD-3-Clause.
See `assets/licenses/LapisCube-Originals.txt`.

Every bundled PNG/WAV has source URL, author, license, modifications, destination
and SHA-256 in `assets/manifest.json`. Packaging verifies those hashes. Sound
families/fallbacks are documented in `assets/sound-map.json`. Step/dig sounds reuse
the engine audio bank; protocol sounds use the native audio pool. Musicbox MIDI
is already interpreted by the server; the client plays its named note events.

To reproduce the assets, put the pinned author ZIP at `build/kenney-voxel.zip`,
install Pillow, and run `python3 tools/build_assets.py`. It verifies the source
hash/license and regenerates the atlas, WAVs, manifest and numeric compatibility
facts from the pinned server. Ordinary builds use the committed generated files.

## Coverage and quality limits

The collection is an initial cohesive resource set, not complete original art for
every item. Several utility blocks/items use material fallbacks. Cow horns, a squat pig/snouted head, a removable sheep coat, chicken bill/comb,
eight spider legs, an upright creeper, an outstretched zombie and a ribbed skeleton
now give the eight species distinct geometry. The skeleton has original bow bars
and string. Drops/projectiles remain simple cuboids. Remote armour, detailed
animations and species voices need further work. Animal events currently use
synthesis fallbacks. Note timbres are simple tones/noise, not a General MIDI bank.
Particle art, full sign text, armor visualization and complete menu art are pending.
Sounds have parser coverage but no listening acceptance session yet.

## Investigated but not imported

- Minetest `voxelpack_by_kenney`: prefer the author's archive because conversion
  README alone does not establish every derivative file's provenance.
- Kenney Impact Sounds: official CC0 candidate, no recordings imported here.
- Freesound and OpenGameArt: each hosts multiple licenses. No site-wide license
  assumption was made and no recordings were admitted without individual review.

Any future non-CC0 asset requires its own author/license/attribution and
redistribution documentation. Identifier strings are interoperability keys,
not a license to distribute another game's corresponding resources.
