# lapisclient implementation report

## Delivery status

Implementation is in local branches `codex/lapisclient-server-v1` (based on
`testing`) and `codex/lapisclient-client-v1` (based on `testing-client`). Both have
the same server source. Remote publication and draft PR creation have not been
completed: automatic approval review rejected the push as an external repository
mutation/publication requiring explicit authorization.

The server/client path is implemented and passes real-server gameplay and
controller/worker tests. The complete milestone is **not yet verified**: Chromium
exits with SIGTRAP before creating a page in this workspace, and GitHub CI cannot
run until the branches can be pushed. Actual WebGL rendering, desktop browser
input and mobile multitouch therefore still require the prepared browser suite.

## Architecture

Lapis Obsidian Client (HTML5/JavaScript/WebGL2 + meshing worker)
→ direct WebSocket (`lapisclient`) → native Lapis Obsidian testing server
→ existing player/world/gameplay state. No standalone TCP bridge.

The initial inspection found identical server code in the two branches and a
browser implementation that parsed protocol-772 TCP packets behind local and
hosted bridges. Rendering, block materials, worker meshing and desktop/touch
controls were reusable. Transport, TCP codecs, gateways and packaging assumptions
were replaced. The server's output helpers and bounded controller dispatch are
the integration points; world and gameplay simulation are shared.

## Server files

New:

- `include/lapisclient.h`, `include/lapisclient_internal.h`
- `src/lapisclient_transport.c` — HTTP upgrade, RFC 6455 framing, origin checks,
  bounded input/output, deadlines, fair nonblocking polling and cleanup
- `src/lapisclient_codec.c` — strict bounded JSON/UTF-8 decoder
- `src/lapisclient_session.c` — version/session state machine, schemas, movement
  budgets, application-to-controller adapter and binary chunk serialization
- `tests/lapisclient_codec.c`, `tests/lapisclient-codec.sh`
- `docs/lapisclient-protocol.md`, this report

Modified:

- `src/main.c`: native listener lifecycle/polling and shared connection cap
- `include/packet_input.h`, `src/packet_input.c`: private bounded in-memory
  controller arguments; not an exposed packet-injection interface
- `src/packets.c`, `src/mob_packets.c`, `src/items.c`, `src/world_border.c`:
  application events, authoritative positions, chunks/unloads and state updates
- `src/procedures.c`: duplicate live identity rejection across both transports
- `src/tools.c`: suppress legacy TCP-only output for WebSocket peers
- `build.sh`, `.github/workflows/build.yml`, `README.md`: build/dependency/startup
  instructions and codec test gate

## Client files

Replaced/modified:

- `client/web/transport.js`: `LapisClientTransport`, direct endpoint negotiation,
  heartbeat, limits, status/errors, mixed-content check and reconnection cleanup
- `client/web/protocol/client.js`: named v1 messages and event mapping
- `client/web/world/world.js`: binary chunk decoding with DataView/Uint8Array
- `client/web/game.js`: direct session lifecycle, server-ready gate, movement
  acknowledgements, worker cleanup and reconnect reset
- `client/web/main.js`, `client/web/index.html`: WebSocket URL/access-token UI
- `client/web/protocol/registry.js`: explicit shared semantic registry generation
- `client/scripts/build.mjs`, new `client/scripts/serve.mjs`: static build/server
- `client/package.json`, `client/package-lock.json`: no production dependencies
- `client/tests/helpers.mjs`, `protocol.test.mjs`, `browser.mjs`, new
  `game.test.mjs` and `world-worker.mjs`: real server and actual worker tests
- `.github/workflows/client.yml`, `.github/workflows/pages.yml`, `.gitignore`,
  `README.md`, `NOTICE.md`, `client/README.md`, client architecture/verification/
  hosting docs and `website/index.html`

The existing `input.js`, renderer, worker, mesh generation and CSS are reused.
Desktop and mobile send the same application protocol.

Removed:

- Entire `client/runtime/` local TCP bridge/gate/launcher/bundled-runtime notice
- Entire `client/gateway/` Cloudflare TCP gateway and configuration
- `client/web/hosted-transport.js`, `client/web/config.js`
- `client/web/protocol/binary.js` TCP packet reader/writer/framer
- Bridge, hosted-gateway, launcher and packaged-runtime tests
- Obsolete `docs/client-bridge.md` and runtime packaging workflow steps

## Protocol and synchronization

- Name/subprotocol: **lapisclient**; application version **1**
- Default endpoint: **ws://127.0.0.1:25566/lapisclient**
- Production: **wss://hostname/lapisclient** via TLS termination and exact allowed
  HTTPS origins; configuration/examples are in the protocol document
- JSON client types: `hello`, `login`, `ready`, `player_move`, `player_input`,
  `selected_slot`, `action`, `interact`, `item_use`, `entity_interact`,
  `inventory_click`, `inventory_close`, `creative_slot`, `swing`, `respawn`,
  `chat_send`, `ping`, `disconnect`
- JSON server types: `welcome`, `login_ok`, `world_info`, `player_position`,
  `player_update`, `chunk_center`, `chunk_unload`, `block_update`, `inventory_slot`,
  `inventory_cursor`, `inventory_open`, `selected_slot`, `health`, `abilities`,
  `world_time`, `entity_spawn`, `entity_move`, `entity_delta`, `entity_look`,
  `entity_update`, `entity_remove`, `chat_message`, `respawn`, `ready`, `pong`,
  `error`, `login_error`, `disconnect`
- Binary type: **1 = chunk_data**, format version 1, 98,830 bytes per column:
  coordinates/height header, 256-entry u16 state palette, 98,304 u8 block indices.
  No Base64, JSON block arrays, client binary uploads or encapsulated TCP packets.

Login reuses the offline player model and deterministic name-derived identity,
with optional shared server access token and duplicate-active-identity rejection.
It is not an online-account authentication system. Initial delivery includes
player, inventory/hotbar, health, abilities, time, spawn position and 25 chunks.
The ready acknowledgement starts gameplay and existing entity synchronization.
Crossing chunk edges produces additional chunks and unloads. Block, inventory,
entity and player updates come from the authoritative server systems.

Local movement/collision prediction remains responsive. The server validates
coordinates and rate-based displacement, reuses its border/plate/fall/hunger
logic, broadcasts accepted movement and corrects rejected movement. No second
world simulation is introduced.

## Validation and security

Exact origins/subprotocol/path/version, strict JSON types/ranges/UTF-8/duplicates,
message/frame limits, masked frames, continuation order, close validation,
connection/session/partial-message timeouts, per-second message/action/chat limits,
4 MiB output cap, username validation, optional constant-time token check and
state sequencing. Shared game handlers retain reach, health, game-mode, entity,
item and inventory-window checks. No filesystem access, arbitrary commands,
server administration, TCP forwarding or generic proxy interface is exposed.

## Commands

Server (`testing` implementation; C compiler + OpenSSL development library):

```sh
./build.sh
./lapis-obsidian
./tests/lapisclient-codec.sh
./tests/run.sh
```

Client (`testing-client` implementation; Node.js 22.12+):

```sh
cd client
npm ci
npm run dev              # http://127.0.0.1:8080
npm run build            # static dist/ production output
npm test
npx playwright install --with-deps chromium
npm run test:web
```

Server configuration: `LAPIS_WS_BIND`, `LAPIS_WS_PORT`, `LAPIS_WS_ORIGINS`, optional
`LAPIS_WS_TOKEN`. `LAPIS_WS_PORT=0` disables the endpoint;
`LAPISCLIENT=0 ./build.sh` builds the legacy server without OpenSSL support.

## Verified and remaining limits

Passed locally:

- native endpoint startup and static client production build;
- all three real-server integration tests, including the actual browser game
  controller and meshing worker, under normal and ASan/UBSan server builds;
- strict codec tests and shared C regression suite;
- registry consistency and website reference checks;
- negotiation, login/token checks, 25 chunks, nonempty terrain geometry,
  two-player movement, server corrections, edge streaming/unloads, block mining/
  placement, hotbar/inventory, UTF-8 chat, disconnect/reconnect, malformed inputs,
  wrong origin/subprotocol/version/path, rate limits and timeout closure.

Still unverified: actual WebGL display, desktop/touch input in a running browser,
physical mobile devices and deployed TLS certificates. The automated browser
suite is ready but cannot run locally; remote CI awaits push authorization.
Native WebSocket support is currently POSIX; Windows/ESP and the separate Alpine
packaging script retain their legacy TCP builds. Run under Linux/WSL, or use
`build.sh` with Alpine openssl-dev. Rendering/lighting/entity cosmetics are
simplified; sound, sign editing, advanced components and server commands are
outside browser v1. The inherited collision/on-ground model is not a full server
physics/anti-cheat system, and experimental negative-Y plate movement is not
supported by the v1 movement schema.
