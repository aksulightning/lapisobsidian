# Server configuration

Lapis Obsidian reads `server.txt` from its working directory before loading the
world or opening its listening socket. A missing file is created with defaults.
Edit it and restart to apply changes. To prepare settings before the first run,
copy `server.txt.example` to `server.txt`.

```ini
port=25565
motd=Lapis Obsidian
gamemode=survival
seed=
mirror-horizontal=false
wheat-growth-seconds=30
experimental_enable_plates=false
```

| Setting | Values | Default |
| --- | --- | --- |
| `port` | TCP port, 1–65535 | 25565 |
| `motd` | Server-list message, up to 120 UTF-8 bytes | Lapis Obsidian |
| `gamemode` | survival, creative, adventure, spectator; or 0–3 | survival |
| `seed` | Empty, or a signed 64-bit decimal integer | Saved seed; otherwise the built-in seed |
| `mirror-horizontal` | true or false | false |
| `wheat-growth-seconds` | Full hydrated growth time, 1–600 seconds | 30 |
| `experimental_enable_plates` | Independent experimental worlds; true or false | false |

Use one `key=value` per line. Blank lines and lines starting with `#` are ignored.
Spaces around keys and values are trimmed. Values are literal: do not add wrapping
quotes, escape sequences or inline comments. Quotes, backslashes, `#` and `=`
inside the MOTD are supported; the server escapes JSON when answering a status
request. LF and CRLF line endings work. Control characters and malformed UTF-8 in
the MOTD are rejected. Empty MOTDs are allowed.

Unknown or duplicate keys, invalid values, lines longer than 255 bytes, embedded
NULs and files over 8 KiB stop startup with a line-numbered error. Invalid files
are never partly applied or overwritten. No heap-backed configuration tables,
scripting runtime or additional libraries are used.

## World options and precedence

Defaults are followed by file settings, then command-line world overrides:

```sh
./lapis-obsidian --seed -123 --mirror-horizontal
./lapis-obsidian --no-mirror-horizontal
```

An empty `seed` lets the existing `world.meta` supply the seed. An explicit seed
from the file or command line must match an existing world. Mirroring must also
match that world's metadata. A mismatch stops startup; use a fresh world directory
for different generation settings. CLI overrides are not written back to the file.
Repeated or conflicting CLI options are rejected.

The default game mode applies when a player connects or reconnects. Administrator
`/gamemode` changes remain session-only. Configure administrator access through
`LAPIS_ADMIN_TOKEN`, as described in [commands.md](commands.md).

Growth timing still requires hydrated farmland and is rounded to the 100 ms farm
tick. Water changes can take up to 6.4 seconds to be noticed. Growth pauses while
the server is stopped, and an existing crop keeps its saved age/partial step when
the configured speed changes. World and farm save formats are unchanged.

The fixed player/mob capacities and view distance remain compile-time settings in
`include/globals.h`. There is no live reload. This file-loading path targets desktop
and Alpine builds; the inherited, unvalidated ESP-IDF startup retains defaults and
command-line world options because its filesystem is mounted later.

## Validation

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

Tests cover default-file creation, parsing limits, invalid UTF-8, numeric bounds,
atomic rejection, CLI precedence, status-packet escaping/framing, default game-mode
abilities and hydrated growth at 1, 30 and 600 seconds.

When Plates is enabled, its saved catalog selects each Plate’s seed; the configured
seed initializes a new default hub. `/plate create <name> <seed> <type>` sets other seeds.
See [Plates setup, travel, saves and limits](plates.md).
