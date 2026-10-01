# Built-in commands (Milestone 3)

Commands use the Java 1.21.8/protocol 772 slash-command packets. The server sends
a small command tree on entering play, so modern clients recognize the names.
No plugin, scripting runtime, database or permission framework is involved.

| Command | Access | Behavior |
| --- | --- | --- |
| `/help` | Everyone | Lists command syntax |
| `/seed` | Everyone | Shows the signed 64-bit world seed |
| `/worldinfo` | Everyone | Generator/protocol versions, mirroring, coordinate bounds, time |
| `/spawn` | Everyone | Teleports the caller to the existing world spawn at X/Z=8/8 |
| `/tp <player>` | Administrator | Teleports the caller to an online player's stored position |
| `/tp <x> <y> <z>` | Administrator | Teleports the caller to integer block coordinates |
| `/time` or `/time query` | Everyone | Shows the current day time |
| `/time set <day\|night\|ticks>` | Administrator | Sets day time; day=1000, night=13000, ticks=0..23999 |
| `/gamemode <mode> [player]` | Administrator | Changes the caller's or named online player's mode |
| `/music <number\|stop>` | Nearby players | Controls the jukebox selected by right-clicking |
| `/music reload` | Administrator | Stops playback, rescans `songs/`, clears selections |
| `/spawnmob <type> [x y z]` | Administrator | Spawns one supported mob on safe ground; see [mob details](mobs.md) |
| `/admin <token>` | Everyone | Authenticates administrator access for this connection |

Modes accept survival/creative/adventure/spectator or 0/1/2/3. Coordinates must
be X/Z=-32768..32767 and Y=0..255 for teleporting (Y=1..253 for mob spawning). Fractions, relative coordinates, selectors,
quoted arguments and teleporting another player are not supported.
A teleport destination reaching or exceeding X/Z +/-4068 redirects to spawn;
the wider integer range is only the storage/parser limit. Names must
match an online player's stored name exactly; the core's existing name-length
limit remains. Teleports have a two-second per-connection cooldown and cannot
be used while dead. They update stored position, fall-distance tracking, chunk
center/view, the caller's position and the entity position seen by other players.
They preserve inventory. Spawn retains the generator's existing fixed location;
it can be underwater, on a canopy, or obstructed by edits.

## Administrator setup

Before starting the server, set the environment variable `LAPIS_ADMIN_TOKEN` to
a private, randomly generated token of 32..128 printable non-space ASCII bytes.
An absent variable disables administrator login. An invalid configured value
fails startup without echoing it. In the client, enter `/admin <your-token>`.
The server does not echo, broadcast, save or log the supplied token. Three failed
or successful authentication attempts are permitted per connection. Reconnecting
resets the attempt count; this is not a global brute-force defense.

The inherited core does not authenticate client UUIDs with Mojang and does not
encrypt these connections. A token sent in a command is visible to someone who
can observe the network traffic and can remain in the client's command history.
Use admin commands on a trusted network or protected tunnel. Usernames and UUIDs
alone do not grant administrator permission. Permissions are cleared on disconnect
and when a player slot is reserved again, including when a client claims an
existing UUID. This does not add online-mode authentication or prevent identity
spoofing/denial of service elsewhere in the inherited core.

## Game modes and persistence

Mode state is per connection, defaults to the existing `GAMEMODE` setting, and is
reset on reconnect. Login, respawn, game-event, abilities and player-info packets
use that state. Creative enables flight and instant mining, prevents damage and
ordinary block/item consumption, and accepts bounded plain inventory stacks.
Adventure blocks direct mining/placement while retaining existing interactions.
Spectator enables flight, prevents damage, and rejects mining, item use, attacks
and inventory clicks. Creative/spectator movement does not consume hunger.
Sprinting remains enabled. Camera spectating and complete vanilla game-mode
semantics are not implemented.

Creative slot packets only support component-free stacks of known protocol item
IDs, count 0..64, and inventory slots 5..45. Custom-component stacks are ignored
and the slot is resynchronized. Oversized/malformed packets or invalid item IDs
are rejected. Accepting a valid item ID does not implement all modern item behavior.
The project's compact palette and existing gameplay subset remain in force.

Mode and permission state are separate fixed arrays; the on-disk `PlayerData`
layout and world-generator version are unchanged. Existing worlds need no
migration. Time changes affect the current runtime only and retain the core's
restart behavior. Position changes use the existing periodic player persistence.
The world seed and horizontal-mirroring startup behavior remain unchanged.

## Parsing, wire compatibility and tests

`commands.c` owns tokenization, validation, permissions, dispatch and bounded
chat/whisper formatting. Commands accept at most 256 bytes and six tokens including
the command name; printable ASCII is required for this fixed English command set.
There is no evaluation, expansion or allocation. Normal chat remains UTF-8 and
retains the core's 224-byte limit. Outgoing system-chat NBT encodes supplementary
characters as Java modified UTF-8 surrogate pairs; system messages are capped at
512 input bytes. Legacy `!help` and `!msg` continue to work.

`command_packets.c` reads complete bounded packets before dispatching any command
or forwarding chat. Its cursor checks VarInts, declared strings, remaining bytes,
UTF-8 chat and trailing fields. Unsigned command packet 0x06 and signed packet
0x07 are supported; the latter requires zero argument signatures because the
advertised tree uses unsigned `brigadier:string` arguments, not signed message
arguments. Timestamps and acknowledgements are consumed, not authenticated.
The main frame reader uses the number of packet-ID bytes actually consumed.
Malformed command/chat/creative packets disconnect the client; malformed command
syntax, unknown names and permission failures receive a message with no action.

Tests exercise valid commands, unknown/missing/excessive arguments, oversized
input, integer limits, NUL/control/non-ASCII rejection, authorization failures,
authentication limits, session reset, spawn/teleport/time/mode effects, exact
player-name matching and legacy whispers. Socket tests cover every truncated
prefix of signed commands and chat, oversized and invalid VarInts, invalid
signature counts, permission enforcement, UTF-8, the advertised command graph,
and game-mode/ability/player-info bytes. All earlier registry, terrain, save and
no-JAR build tests continue to run.

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

A live TCP smoke test also covers login/configuration/play, all seven requested
commands, administrator authentication, creative movement, teleport destinations,
spawn, malformed-command disconnection and permission reset after reconnect.
ASan/UBSan pass on the tested paths; LeakSanitizer is disabled in this sandbox.
A graphical Minecraft client session remains untested. This work does not claim
a complete adversarial audit of inherited inventory, networking or gameplay code.

See [musicbox controls and limits](redstone-and-music.md) for `/music` setup.
