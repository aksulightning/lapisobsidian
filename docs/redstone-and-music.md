# Basic redstone, note blocks and MIDI musicboxes

These are small C systems for the custom server, with Alpha-style circuit
building as their starting point. They do not implement the complete vanilla
redstone simulator or a general-purpose MIDI synthesizer.

## Building circuits

- Place redstone dust on the top of solid blocks. Dust connects north, south,
  east and west on the same level. A neighboring torch, pressed plate or enabled lever supplies
  level 15; each subsequent dust block loses one level. Loops lose power when
  their source is removed.
- Redstone torches attach to the top or horizontal sides of solid blocks. A torch switches off when
  a circuit component adjacent to its supporting block supplies power, and
  switches on when that input disappears. This allows simple NOT gates.
- Stone plates activate for living players/mobs; oak plates also detect dropped
  items. Both release when empty. Oak and iron trapdoors accept adjacent power;
  oak also opens by hand. See [controls and limits](farming-and-controls.md).
- Levers are floor-mounted. Right-click to toggle them. Their setting persists.
- Oak doors open when either half has an adjacent powered component and close
  when that input disappears. A powered door cannot be manually closed.
- Note blocks play on an unpowered-to-powered transition. They do not repeatedly
  play while held powered.

Circuits update at most once per 100 ms. Dust propagation is limited to 15
passes through a fixed 256-component table. Torch feedback settles one step per
update; oscillators cannot recurse indefinitely or allocate more work. There is
no torch burnout model. Flat dust, floor/wall torches, floor levers, plates and
direct adjacency are the supported subset. Staircase wiring, wall levers,
general solid-block conduction, repeaters, comparators, pistons, buttons and redstone lamps are not implemented by this addition. The torch's
support-input rule is the one special solid-block rule. Dust visual connections
and power states are sent to modern clients.

Craft a torch with redstone over a stick, or a lever with a stick over
cobblestone. Redstone ore now drops dust when mined with an iron-or-better
pickaxe. Mining placed dust returns dust; removing its support removes it and
attempts one item drop, subject to the existing bounded item pool.

## Note blocks

Craft eight oak planks around one redstone dust. Right-click to tune through
25 pitches and preview the note. Left-click previews in survival; completing
mining still breaks the block. Creative left-click breaks it normally. Redstone
plays the selected note only on a rising edge. An occupied block above mutes it.
Pitch is saved; powered state is recalculated. Supported instruments are:

| Block below | Sound |
| --- | --- |
| Oak planks, oak log, bookshelf | Bass |
| Stone, cobblestone, bricks, obsidian | Bass drum |
| Sand, gravel | Snare |
| Glass | Hi-hat |
| Other blocks | Harp |

Sounds use the client's existing note-block resources and its jukebox/note-block
volume category. No audio assets or sound library are bundled. The initial
implementation sends sounds and block states, without floating note particles.

## Musicboxes

The jukebox block acts as a musicbox. Craft eight oak planks around a diamond,
place the box, and right-click it to select it and list songs in chat:

```text
/music 1
/music stop
```

Selections expire after 60 seconds and require the player to remain within six
blocks on each axis of the same box. Spectators cannot control it. Any eligible
nearby player can replace or stop playback; there is no ownership framework.
The menu uses numbered commands, not clickable chat components. There is a
one-second menu/start rate limit. Administrators can run `/music reload` to
rescan `songs/` under the server working directory; this stops current playback and clears selections. Player
uploads and arbitrary path arguments are not supported.

Two boxes can play simultaneously, each sharing one song with nearby players.
Playback continues if the initiating player leaves, ends naturally, and stops
when the box is removed. Playback position is not saved across restarts. The
placed box persists through normal world edits. See [songs/README.md](../songs/README.md)
for filenames and an optional C tool that creates an original four-second demo.

The reader accepts Standard MIDI File formats 0 and 1 with a positive PPQN time
division, at most 16 tracks, 64 KiB per file, 2048 note-on events, ten minutes of
timeline and 32 notes per 50-ms bucket. It merges tracks, honors tempo changes,
supports channel running status and skips bounded meta/SysEx payloads. It rejects
format 2, SMPTE time division, malformed/truncated data, missing end-of-track
events and excessive limits before replacing active playback.

MIDI note-ons become short note-block sounds. Notes outside the supported
two-octave pitch range are folded by octaves. Bass programs use the bass sound;
other melodic programs use harp. Channel 10 maps kick, snare and other percussion
to the three percussion sounds above. Note-offs, sustain, pitch bend and other
controllers do not change the short sound's natural decay. This is a lightweight
arrangement of a MIDI file, not faithful General MIDI playback.

The main loop services playback approximately every 20 ms. After a stall, notes
more than 100 ms late are skipped, with at most 32 note packets per box per
service. Slow chunk generation can therefore drop musical notes rather than
queue an unbounded audio burst. Already emitted sounds finish their short decay
when stopped. A graphical/audio playtest is still needed to judge musical quality.

## Storage and implementation

`src/circuits.c` keeps 256 compact nodes and a fixed lookup table (about 3 KiB),
and uses `circuits.bin` for kinds, attachment direction, trapdoor settings, lever settings and note tuning.
Version 2 keeps seven-byte records and reads older version-1 saves. Keep this file
with the other world files. The existing 256-entry block palette is unchanged:
dust uses the passable redstone-torch carrier plus its distinct side-table kind.
Chunk/block updates replace that carrier with the correct dust state. Removing
`circuits.bin` loses the distinction and saved tuning. Previously decorative
torches, levers and note blocks in old worlds are adopted on startup; a world
exceeding the component limit fails startup clearly. Invalid files are rejected,
stale records are removed, and failed placement/tuning saves roll back.

`src/notes.c` handles named sound packets. `src/midi.c` is a bounded parser;
`src/musicbox.c` handles the catalog, selection and playback. Each playing song
uses about 16 KiB, allocated only on a manual play request and freed on stop.
File parsing temporarily allocates another 64 KiB; replacing a song may briefly
retain the old and new event lists. Idle musicboxes allocate no song buffers.
The runtime has no Java, scripting, synthesis, database or new library dependency.

Packet/state constants are pinned to protocol 772 and were checked against the
existing minecraft-data reference revision documented in [registries.md](registries.md).
The primary SMF reference is the MIDI Association's
[Standard MIDI Files specification](https://midi.org/standard-midi-files-specification).

## Validation

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

New tests cover power decay and source removal, inversion, note rising edges,
instrument/pitch state IDs, powered doors, placement limits, support removal,
recipes, persistence and save failure, malformed circuit records, song menus,
permissions, distance/expiry, playback/removal and simultaneous playback limits.
MIDI tests cover formats 0/1, merged tempo changes, running status, truncation,
mutations, file/event/duration/burst limits and the generated demo. Existing
command-tree, chunk-packet and no-JAR build checks remain part of the suite.

The normal build and regression suite, complete ASan/UBSan suite, strict new-module
warning checks and no-JAR build passed. A live protocol-772 TCP test verified
creative placement, dust state updates, a redstone-triggered note sound, the
right-click MIDI menu, playback sound packets, stop and administrator reload.
Actual rendered appearance and audible playback have not been tested here.
