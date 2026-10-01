# Gameplay roadmap

Lapis Obsidian is a lightweight custom server with a Beta-inspired foundation.
Exact vanilla behavior is not the target. Keep each addition bounded, written
in C, and separate from the low-level networking and world generator.

1. **Item drops — implemented.** World entities, gravity, pickup, merging,
   lifetime, block/mob loot and player drops. See [behavior and limits](items.md).
2. **Passive mobs — inherited baseline to refine.** Chickens, cows, pigs and
   sheep already have basic wandering, health and loot. Improve the small
   existing implementation instead of adding a second entity framework.
3. **Hostile and neutral mobs — implemented.** Melee zombies, skeletons with
   arrows, neutral spiders, and creepers with a hiss and harmless firecracker
   burst. Admin spawning and grass/flower fixes are included. See
   [behavior, implementation and tests](mobs.md).
4. **Basic redstone — implemented.** Flat dust, floor torches and levers,
   simple inversion and powered oak doors, with bounded updates and persistent
   settings. Staircase wiring and full vanilla circuitry remain outside this subset.
5. **Note blocks — implemented.** Tuning, five support-based instruments and
   hand/redstone activation.
6. **MIDI musicbox — implemented.** Server-owned `.mid` files in `songs/`,
   a numbered chat menu and `/music` play/stop controls. Two concurrent boxes,
   a bounded C format-0/1 reader and nearby note sounds; no player uploads or
   bundled Minecraft music.

See [redstone and music behavior, limits and tests](redstone-and-music.md).
