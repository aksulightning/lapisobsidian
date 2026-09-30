# Gameplay roadmap

Lapis Obsidian is a lightweight custom server with a Beta-inspired foundation.
Exact vanilla behavior is not the target. Keep each addition bounded, written
in C, and separate from the low-level networking and world generator.

1. **Item drops — implemented.** World entities, gravity, pickup, merging,
   lifetime, block/mob loot and player drops. See [behavior and limits](items.md).
2. **Passive mobs — inherited baseline to refine.** Chickens, cows, pigs and
   sheep already have basic wandering, health and loot. Improve the small
   existing implementation instead of adding a second entity framework.
3. **Hostile and neutral mobs — next.** Retain melee zombies; add skeletons
   with arrows, spiders that retaliate only when attacked, and creepers with
   a hiss and firecracker-style sound/particle burst. Creepers destroy no
   blocks; the proposed default also does no player damage.
4. **Basic redstone — planned.** Dust, power-providing/inverting torches and
   switches for simple circuits, doors and note blocks. Bound update work and
   document intentional differences from vanilla.
5. **Note blocks — planned.** Tuning, instrument selection and hand/redstone
   activation, providing the sound foundation for the musicbox.
6. **MIDI musicbox — planned.** Server-owned `.mid` files in `songs/`, a
   right-click chat selection menu and play/stop controls. One shared song
   per box, audible nearby, through in-game instrument sounds. A bounded C
   MIDI reader, no scripting runtime, arbitrary file paths or player uploads.
   Distribute only original or explicitly redistribution-licensed songs.

The later entries are a plan, not a claim that those features are implemented.
