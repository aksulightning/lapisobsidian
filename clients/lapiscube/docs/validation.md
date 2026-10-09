# Milestone 1 validation — 2026-10-09

Reference server: testing `56300de744b9b859993f64d5c00d1637e42584c6`.
Engine: ClassiCube `d41c3f7eef2038f59702b58bdb373483fb0d28f9`.
Host: Linux, GCC; isolated loopback server with seed 42 and ordinary survival
configuration. No server source, security checks or gameplay settings were
changed to accept the client. No proprietary client or game assets were used.

## Executed checks

| Check | Actual result |
| --- | --- |
| Root `./build.sh` | Unmodified Lapis server built successfully |
| `make test` | 2,396 assertions passed with C89, warnings as errors, shadow/conversion warnings |
| `make sanitize SANITIZER_FLAGS=-DLAPIS_SANDBOX_SANITIZERS` | Same 2,396 assertions passed under AddressSanitizer and UndefinedBehaviorSanitizer |
| Core C++ compilation (`g++ -std=c++11 -Wall -Wextra -Werror`) | `LapisProtocol.c` compiled successfully |
| `make integration` | Actual TCP status/pong, offline login/configuration/Play and same-identity reconnect passed |
| Integrated native terminal/software-renderer build | Full engine executable linked successfully |
| `tests/native_smoke.py`, Lapis case | Actual integrated engine accepted Play login, received 25 chunk frames and synchronized both spawn teleports |
| `tests/native_smoke.py`, Classic case | Original Classic 131-byte login, version 7, username and mppass matched a synthetic Classic peer |
| Windowed Linux build | All objects compiled; linker blocked by unavailable `-lX11`, `-lXi`, `-lGL` development libraries |
| Windows | Not built or run; MinGW compiler not installed in this environment |

The first sanitizer invocation could not complete because LeakSanitizer cannot
inspect `/proc` in this sandbox. The explicitly named sandbox option disables
only leak detection. ASan/UBSan then completed without a detected memory/UB error;
symbolization warnings remain an environment limitation. The engine and existing
BearSSL objects were not sanitizer-instrumented in this targeted core test.

Actual headless login and reconnect summary (both runs):

```text
result=PASS state=4 registries=11 entries=68 tags=1 joined=1 loaded=1 chunks=25 teleports=2 keepalives=1 skipped=77 queued=0
```

Native integration output:

```text
native Lapis: Play login and both spawn teleports passed (25 chunk frames)
native Classic: original 131-byte Classic login passed
```

The probe's `loaded=1` is a test-only completion request after receiving the
server's final spawn teleport. The native M1 UI stays on the loading screen and
does not send that request. No world is rendered by either test. Skipped packets
include inventory, health, entities, chunk bodies and other future gameplay data.

Tests cover field VarInt edge values/overflow/truncation, every fragmentation
boundary of a status frame, byte-at-a-time and coalesced input, zero/oversized/
overlong lengths, EOF in header/body, deterministic offline UUID, wrong login
states, missing/duplicate registries, unsupported NBT, exact keepalive/negative
teleport acknowledgment bytes, invalid floating-point positions/flags, maximum
accepted frame size, partial writes, would-block retention, full output queue,
connection/frame deadlines, state reset, and 2,000 deterministic adversarial inputs.

Raw reproduction logs are generated under `build/`: `integration-*.log`,
`integration-results.json`, `native-lapis.log`, `native-classic-login.hex` and
the server logs. Local build logs are not shipped as game assets or binaries.

## Not validated / remaining acceptance gates

- No manual visual gameplay session: no chunk decoder, terrain display or player
  movement exists yet. Receipt of chunks does not establish world compatibility.
- No breaking/placing, hotbar/inventory consistency, pickup/drop/crafting/container
  gameplay, mobs, combat/damage/death, audio/particles or mode-specific play.
- No Classic world/CPE gameplay regression session, only unchanged source and the
  original login exercised through the real engine.
- No broad vanilla protocol-772 compatibility claim. Only the pinned Lapis
  connection profile is verified, on a locally built actual Lapis server.
- No Windows binary, cross-platform runtime matrix or distributable CC0 asset pack.
- Full existing server test suite was not rerun: no server source was modified.
  Client-specific automated and actual-server integration tests were run.

Before calling LapisCube playable, complete M2–M4 and manually validate a fresh
survival session. Before distribution, complete asset provenance, package licenses,
Linux/Windows builds and clean-install tests. Keep this file specific to evidence,
not the roadmap's intended capabilities.
