# lapisclient v1

Lapis Obsidian Client connects directly to the `testing` server. This is an
application protocol, not a TCP tunnel. The native TCP endpoint remains available
to its existing clients. No browser bridge, remote target selection, packet
injection, or administrative command API exists in this protocol.

## Transport and deployment

- URL: `ws://127.0.0.1:25566/lapisclient` by default. Exact path; no query string.
- Required WebSocket subprotocol: **`lapisclient`** (case sensitive).
- RFC 6455 version 13; client frames must be masked. Text messages can be
  fragmented; each complete message is one JSON object. Ping/pong/close control
  frames are supported. No compression extension is negotiated.
- Application version is **1**, negotiated in `hello`, separately from the
  WebSocket subprotocol. Other versions are rejected; never assume compatibility.
- Client messages: UTF-8 JSON, at most 4096 bytes per complete message, flat objects
  with string, finite number and boolean values. Duplicate or unknown fields,
  nested objects, arrays, nulls, invalid UTF-8, unknown types and invalid fields
  close the session. Omitted optional fields are explicitly listed below.
- Server messages: JSON text or a complete binary chunk message. Ordering follows
  WebSocket ordering. There is no TCP packet framing inside any message.
- Binary client messages are not defined and are rejected.

Build on Linux/POSIX with a C compiler and OpenSSL development headers/library
(`libssl-dev` on Debian/Ubuntu). OpenSSL supplies SHA-1 for the HTTP upgrade and
SHA-256 for offline identity; TLS is terminated by the deployment proxy.

```sh
git checkout testing
./build.sh
./lapis-obsidian
```

The server reads `server.txt` and the current directory's world saves as before.
The default native TCP listener is 25565. The WebSocket listener starts in the
same process after game state initialization; bind/configuration errors fail
startup. `LAPISCLIENT=0 ./build.sh` builds the legacy server without this feature
or OpenSSL. `build-alpine.sh` also remains a legacy TCP build; use `build.sh`
with Alpine openssl-dev for WebSocket support. Native Windows/ESP builds currently use the legacy path; run the
WebSocket server under Linux/WSL for this milestone.

| Environment variable | Default | Meaning |
| --- | --- | --- |
| `LAPIS_WS_BIND` | `127.0.0.1` | IPv4 bind address; use `0.0.0.0` deliberately for LAN access |
| `LAPIS_WS_PORT` | `25566` | Port, or `0` to disable the listener |
| `LAPIS_WS_ORIGINS` | `http://localhost:8080,http://127.0.0.1:8080` | Comma-separated exact origins, including scheme and nondefault port; no wildcard |
| `LAPIS_WS_TOKEN` | unset | Optional shared access token, 32–128 URL-safe ASCII characters (`A-Z a-z 0-9 _ - . ~`) |

Origin is required, including for non-browser implementations. Missing,
unauthorized or duplicate Origin headers fail the upgrade with HTTP 403, as do
wrong path, version, key or subprotocol. Origin restrictions protect browser
access; they are not identity authentication and non-browser clients can forge
an Origin. A token is separate from `LAPIS_ADMIN_TOKEN` and grants no admin power.
Tokens are never stored by the browser in local/session storage or URLs.

For public deployments use HTTPS for static client files and WSS for gameplay.
Configure the exact HTTPS client origin on the server. Example nginx location
inside an existing TLS-enabled server block:

```nginx
location = /lapisclient {
    proxy_pass http://127.0.0.1:25566;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_set_header Origin $http_origin;
    proxy_read_timeout 45s;
}
```

Use your normal certificate/key configuration on that TLS virtual host. Preserve
the original Origin and subprotocol headers. Do not expose the loopback backend
unnecessarily. For example set `LAPIS_WS_ORIGINS=https://play.example.org` and enter
`wss://game.example.org/lapisclient` in the client. Browsers reject `ws://` from
HTTPS pages; the client reports that before opening a socket.

## Session lifecycle

```json
{"type":"hello","protocol":"lapisclient","version":1}
{"type":"welcome","protocol":"lapisclient","version":1,"authentication":"offline","tokenRequired":false}
{"type":"login","username":"Explorer"}
{"type":"login_ok","entity":5,"username":"Explorer","authentication":"offline"}
```

The first two lines are client then server; the last two are client then server.
`login.token` is optional when no access token is configured and required when
one is configured. Username matches `[A-Za-z0-9_]{1,15}`. Identity follows the
existing offline model: first 16 bytes of SHA-256 of UTF-8 `LapisObsidian:` plus
username, then byte 6 becomes `(byte & 15) | 64` and byte 8 becomes
`(byte & 63) | 128`. The server derives this identity, not the browser. Existing
player persistence, inventory, spawn and gameplay state are reused. Duplicate
live UUIDs or names are rejected across both transports. Reconnect reuses the
persisted offline identity; this does **not** prove ownership of a username.
The optional shared token limits server access, not impersonation among holders.

The server sends `world_info`, initial `player_position`, inventory/hotbar,
health/abilities/time, `chunk_center`, the center chunk followed by the remaining
view, another authoritative `player_position`, and existing player entities.
When the client has installed its spawn chunk it sends `{"type":"ready"}`.
The server uses its existing join handler to synchronize mobs/items and notify
other players, then replies `{"type":"ready"}`. Only then does normal input
begin. After respawn the client sends `ready` again; this does not join twice.

Handshake/hello/login deadlines are 10 seconds per phase; initial world readiness
has a 20-second deadline. Idle and incomplete-message limits are 30 and 10 seconds
respectively. Send `{"type":"ping"}` every 10 seconds; the server returns
`{"type":"pong"}`. The server caps all transports together at 16 live clients
and retains the inherited limit of 16 stored player identities.

## Client-to-server schemas

Every row includes the required string `type`. Fields are required unless stated
otherwise. Coordinates use world blocks; yaw/pitch use degrees. All integers must
be integral JSON numbers. Nonfinite numbers are always rejected.

| Type | Fields and constraints | Behavior |
| --- | --- | --- |
| `hello` | `protocol:"lapisclient"`, `version:1` | First application message |
| `login` | `username:string`, optional `token:string` | Only after welcome |
| `ready` | none | World installed; begins gameplay |
| `player_move` | `x,z:number [-32767,32767]`, `y:number [0,256)`, `yaw:number [-360,360]`, `pitch:number [-90,90]`, `onGround:boolean` | Validated predicted position |
| `player_input` | `flags:integer [0,63]` | Bits: forward 1, back 2, left 4, right 8, jump 16, sneak 32; existing server uses sneak |
| `selected_slot` | `slot:integer [0,8]` | Select authoritative hotbar slot |
| `action` | `action:string`; for break actions also integer `x,z [-32767,32767]`, `y [0,255]`, `face [0,5]` | `break_start`, `break_cancel`, `break_finish`, `drop_stack`, `drop_item`, `release_use` |
| `interact` | integer `x,z [-32767,32767]`, `y [0,255]`, `face [0,5]` | Place/use held item on block |
| `item_use` | `yaw [-360,360]`, `pitch [-90,90]` | Use held item in air; bucket ray direction |
| `entity_interact` | `entity:integer [-1000000,1000000]`, `action:"attack"` or `"use"`, `sneak:boolean` | Existing entity range/health/cooldown checks |
| `inventory_click` | `window:integer [0,14]`, `slot:integer [0,62]`, `button:0 or 1`, `shift:boolean` | Server validates actual open window and slot; no client stacks accepted |
| `inventory_close` | `window:integer [0,14]` | Close matching container |
| `creative_slot` | `slot:integer [5,45]`, `item:valid item ID [1,65535]` | 64-item stack only when server game mode allows creative inventory |
| `swing` | none | Arm action; cosmetic rendering optional |
| `respawn` | none | Honored only when dead |
| `chat_send` | `text:string`, 1–224 UTF-8 bytes, no control characters | Server chat broadcast; slash commands are unavailable |
| `ping` | none | Heartbeat after login |
| `disconnect` | none | Valid in any negotiated application state |

Block faces: 0 bottom, 1 top, 2 north (-Z), 3 south (+Z), 4 west (-X), 5 east (+X).
The server drives chunk delivery; there is no arbitrary chunk-request interface.
No client can upload world state or supply an arbitrary controller packet ID.

## Server-to-client schemas

Every message contains `type`. Fields below are in addition to that field.

| Type | Fields |
| --- | --- |
| `welcome` | `protocol`, `version`, `authentication:"offline"`, `tokenRequired:boolean` |
| `login_ok` | `entity:integer`, `username:string`, `authentication:"offline"` |
| `world_info` | `entity:integer`, `distance:integer`, `mode:0..3`, `dimension:string`, `minY:-64`, `height:384`, `registry:"lapis-v1"` |
| `player_position` | `x,y,z,yaw,pitch:number`; authoritative absolute correction/spawn; discard predicted vertical velocity |
| `player_update` | `x,y,z:number`; accepted movement acknowledgement, no correction needed |
| `chunk_center`, `chunk_unload` | `x,z:integer` chunk coordinates |
| `block_update` | `x,y,z:integer` block coordinates, `state:u16` render-state ID |
| `inventory_slot` | `window,slot,item,count:integer` (count zero means empty) |
| `inventory_cursor` | `item,count:integer` |
| `inventory_open` | `window:integer` |
| `selected_slot` | `slot:0..8` |
| `health` | `health,food,saturation:integer` (saturation is the server's internal hunger timer units, not hunger points) |
| `abilities` | `flags:integer` inherited ability flags: invulnerable 1, flying 2, flight allowed 4, creative 8 |
| `world_time` | `ticks:integer [0,23999]` |
| `entity_spawn` | `id,kind:integer`, `x,y,z,yaw,pitch:number` |
| `entity_move` | `id:integer`, `x,y,z,yaw,pitch:number` absolute |
| `entity_delta` | `id:integer`, `dx,dy,dz,yaw,pitch:number`; deltas in blocks |
| `entity_look` | `id:integer`, `yaw,pitch:number` |
| `entity_update` | `id:integer`, one or more optional `flags:integer`, `pose:integer`, `sheared:boolean`, `item:integer`, `count:integer` |
| `entity_remove` | `id:integer` |
| `chat_message` | `text:string`, literal UTF-8; render as text, never HTML |
| `respawn` | `dimension:string`, `mode:0..3`; discard world/entities, wait for new initial view |
| `ready`, `pong` | no fields |
| `error`, `login_error` | `code:string`, `message:string`, followed by close |
| `disconnect` | `reason:string`, followed by close |

Inventory window IDs/layouts: 0 player (0 crafting output, 1–4 crafting grid,
5–8 armor, 9–35 storage, 36–44 hotbar, 45 offhand); 2 chest (0–26 chest,
27–53 storage, 54–62 hotbar); 12 crafting table (0 output, 1–9 grid,
10–36 storage, 37–45 hotbar); 14 furnace (0–2 furnace, 3–29 storage, 30–38 hotbar).
Only implemented game operations are honored by the existing controller.

Render-state IDs, block palette and item IDs in v1 are frozen by the checked-in
`generated/registry_snapshot.json` (`palette`, `mapping`, `mappingToBlock`, `blockRegistry`, `items`) and generated
`include/registries.h`; they are semantic identifiers, not TCP packet formats.
Entity kind IDs used by this server include player 149 and item 69; the full
current mob mapping is in `include/mobs.h`. Unrecognized render states/kinds may
be displayed with a generic fallback. Do not change existing v1 ID meanings.

## Binary chunk framing

Each binary WebSocket message is exactly **98,830 bytes**. No JSON number arrays,
Base64, compression or nested TCP framing. All multi-byte integers are little
endian. Browser implementations use ArrayBuffer/DataView/Uint8Array.

| Offset | Length | Value |
| --- | --- | --- |
| 0 | 1 | kind `1` = `chunk_data`; 0 and 2–255 reserved |
| 1 | 1 | format version `1` |
| 2 | 4 | signed chunk X (world X divided by 16, rounded down) |
| 6 | 4 | signed chunk Z |
| 10 | 2 | signed minimum Y, `-64` |
| 12 | 2 | unsigned height, `384` |
| 14 | 512 | 256 unsigned 16-bit render-state IDs |
| 526 | 98,304 | unsigned 8-bit palette index for each block |

Index = `(y + 64) * 256 + localZ * 16 + localX`; local X/Z are 0–15. Negative
Y sections follow the existing server's bedrock floor; upper sections come from
the same world generator. Chunk messages replace the entire column atomically.
Install them in order before applying subsequent `block_update` overlays (torches,
chests, door/circuit/farm state). Worker implementations must preserve this order.
The server sends changed edges when the player crosses chunk boundaries, explicit
unloads for columns leaving the view, and incremental authoritative block/entity
updates. Clients clear all world state on reconnect/respawn, bound their chunk
cache, and do not generate a second world simulation.

## Movement, validation and errors

The browser predicts local motion/collision for responsiveness and sends positions
at 20 Hz. The server reuses its plate/world-border guards, coordinate checks,
fall-damage/hunger handling and existing player broadcasts. It adds time-based
movement budgets (horizontal 6 blocks/s, upward 10, downward 55, bounded burst
allowances). Rejected movement gets `player_position`; accepted movement gets
`player_update` and updates other clients. The inherited integer player storage
is preserved; WebSocket movement acknowledgements retain accepted precision.
This is not a full server physics/anti-cheat rewrite: the inherited client-side
collision/on-ground model and simplified mining timing remain limitations.

Shared gameplay code validates reach, health, game mode, item IDs/stacks, active
inventory window and permissions. There are at most 100 application messages/s,
30 nonmovement actions/s, and 2 chat messages/s per connection. Output queues are
bounded at 4 MiB; stalled peers are closed instead of blocking the game loop.
All malformed/unknown messages are fatal rather than leaving an ambiguous state.
`protocol_mismatch`, `invalid_state`, `invalid_message`, `unknown_type`,
`authentication_failed`, `already_online`, `server_full`, `rate_limit`, `timeout`,
`invalid_frame`, `message_too_large` and `session_error` are v1 error codes.
Policy failures send close 1008; ordinary disconnect sends close 1000. Broken
sockets/backpressure may terminate without an application error. All closures
release the shared player session and broadcast player removal. Reconnect starts
a new WebSocket/hello/login sequence with no replay of pending input.

## Browser commands and scope

```sh
git checkout testing-client
cd client
npm ci
npm run dev             # http://127.0.0.1:8080; static files only
npm run build           # deploy client/dist/ to any static HTTPS host
npm test                # builds server; codecs and real-server protocol tests
npx playwright install --with-deps chromium
npm run test:web        # real server + desktop/touch browser tests
```

Use `HOST=0.0.0.0 PORT=8080 npm start` for LAN development after building, allow
that exact LAN origin on the server, and bind the WebSocket server appropriately.
Both desktop and touch input use this same protocol. JavaScript orchestrates UI,
networking and worker meshing; no WASM is needed for this format. Render models,
lighting and entity cosmetics remain simplified. Sounds, sign text editing,
advanced item components and server commands are outside v1's browser scope.
A future incompatible revision must negotiate a new integer version; reserved
binary IDs are not permission to assume compatibility with unimplemented types.
