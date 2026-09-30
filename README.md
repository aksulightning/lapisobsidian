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

## Configuration and scope

Configuration is currently compile-time, in `include/globals.h`. The server uses
port 25565 and stores bounded world edits and player records in `world.bin`.
The low-level network core, inventory, ticks, and edit storage come from bareiron.
This is an experimental server, not a plugin platform or a complete survival
implementation. A full audit of inherited packet handling is still required.

Milestones 0 and 1 are implemented: self-contained protocol data and the terrain
foundation. Terrain currently contains stone, water, ice and air from Y=0..127;
the compatibility floor below Y=0 remains bedrock. Biome surfaces, caves, ores,
trees, decorations, commands and persistent signs are not implemented yet.
Inherited gameplay, including sprinting and hunger, is retained during these
milestones; the Beta gameplay pass is still pending.

Create a new world with `./lapis-obsidian --seed -12345`, using a signed 64-bit
decimal seed. Later runs load the seed from `world.meta`. Supplying a different
seed for the same world fails. Keep `world.meta` and `world.bin` together. Run the
binary from a fresh directory to start another world. Legacy saves without
metadata are rejected rather than overlaid onto a different terrain generator.
The inherited compact coordinate range is X/Z=-32768..32767, with player/edit
Y=0..255. Network view-distance requests beyond that boundary are not generated.

See [the milestone report](docs/milestones.md) for changes, validation and limits.

See [registry maintenance](docs/registries.md) for data provenance and protocol
constraints, and [notices](NOTICE.md) for upstream attribution.
