# Lapis Obsidian

A lightweight C Minecraft Java server, based on bareiron, targeting a Beta
1.7.3-style world with selected modern conveniences. Not affiliated with Mojang
Studios or Microsoft.

Supported client: **Minecraft Java 1.21.8, protocol 772**, with the built-in
`minecraft:core` pack. Other protocol versions are rejected for login.

## Build and run

```sh
./build.sh
./lapis-obsidian
```

Requires a C compiler (GCC by default), a shell, and the system C/math libraries.
The build finishes without launching the server. It downloads nothing and needs
no Java, Minecraft installation, game assets, or data extraction. The checked-in
`src/registries.c` and `include/registries.h` are the compatibility snapshot.
Set `CC` to select a compiler. `DEBUG=1 ./build.sh` enables ASan/UBSan and additional
conversion/shadow diagnostics. The existing MinGW `--9x` build option is retained.
Embedded ESP-IDF support is inherited and has not been validated by this fork.

## Tests

```sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

The tests include a source-only build with a restricted PATH that excludes Java
and JavaScript runtimes. Maintainers can additionally run
`node build_registries.js` and check that the generated C files have no diff.
Node is only used for optional snapshot maintenance.

`node build_registries.js --check` verifies deterministic generation without
rewriting files. New C modules and unit tests compile with `-Wall -Wextra
-Wconversion -Wshadow -Werror`. Integration tests retain the core's warning level.
Test sanitizer builds disable LeakSanitizer because restricted environments may
not expose `/proc`; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## Commands

`/help`, `/seed`, `/worldinfo`, `/spawn`, and `/time query` work for everyone.
`/tp`, `/time set`, and `/gamemode` require administrator access. Configure a
private random `LAPIS_ADMIN_TOKEN` environment value (32..128 non-space ASCII
bytes), then use `/admin <token>` in-game. Admin login is disabled by default.
Permissions and game modes reset on reconnect; no world-file migration is needed.

The inherited connection is unencrypted and does not verify player UUIDs; use
administrator commands on a trusted network or protected tunnel. See
[command syntax, permissions and limits](docs/commands.md) for details.

## Signs

Place an oak sign on the top or side of a solid block, then enter text in the
client editor. Right-click either side to edit it again. Up to 128 signs persist
in `signs.bin`, with four lines per side (96 UTF-8 bytes per line). Existing worlds
remain compatible. See [sign behavior, storage and limits](docs/signs.md).

## Configuration and scope

Configuration is currently compile-time, in `include/globals.h`. The server uses
port 25565 and stores bounded world edits and player records in `world.bin`.
The low-level network core, inventory, ticks, and edit storage come from bareiron.
This is an experimental server, not a plugin platform or a complete survival
implementation. A full audit of inherited packet handling is still required.

Milestones 0–4 are implemented: self-contained protocol data, deterministic
Beta-style terrain, biome surfaces, caves, ores, ordinary oaks and basic ground
cover. Trees and decoration are an initial subset; see [world generation](docs/worldgen.md)
for deliberate differences and deferred variants. Built-in slash commands are
available, and oak signs support persistent front/back text. No scripting runtime, plugin system or database is included.
Inherited gameplay, including sprinting and hunger, remains; the Beta gameplay
pass is still pending.

Create a new world with `./lapis-obsidian --seed -12345`, using a signed 64-bit
decimal seed. Later runs load the seed from `world.meta`. Supplying a different
seed for the same world fails. Keep `world.meta`, `world.bin`, and `signs.bin` together. Run the
binary from a fresh directory to start another world. Legacy saves without
metadata are rejected rather than overlaid onto a different terrain generator.
Generator version 2 also rejects Milestone 1/version 1 saves. Start a fresh world
in another directory; no migration is provided.
Enable horizontal world mirroring with a startup argument, optionally with a seed:

```sh
./lapis-obsidian --mirror-horizontal
./lapis-obsidian --seed -12345 --mirror-horizontal
```

Arguments can appear in either order. Mirroring defaults to off; no rebuild is
needed. Repeat `--mirror-horizontal` whenever starting a mirrored world.
This flips X using `x -> -x - 1`; Y and Z stay unchanged. Surfaces, caves, ores,
trees, decoration and biome columns are reflected together. Player edits remain
at their placed coordinates. The setting is stored in `world.meta`; an existing
world rejects a different setting. Older version-2 metadata remains compatible
with mirroring off. Select the setting before creating a fresh world directory.

The inherited compact coordinate range is X/Z=-32768..32767, with player/edit
Y=0..255. Network view-distance requests beyond that boundary are not generated.

See [the milestone report](docs/milestones.md) for changes, validation and limits.

See [registry maintenance](docs/registries.md) for data provenance and protocol
constraints, and [notices](NOTICE.md) for upstream attribution.
