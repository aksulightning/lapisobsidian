# Lapis Obsidian Client

HTML5/JavaScript/WebGL2 client using the native **lapisclient v1** WebSocket
endpoint in the `testing` server. No local bridge or generic gateway is required.

## Run

Start the matching server using `./build.sh && ./lapis-obsidian` from a separate
checkout. On Linux, install a C compiler and OpenSSL development headers first.
Then, from this directory with Node.js 22.12 or later:

```sh
npm ci
npm run dev
```

Open http://127.0.0.1:8080. Enter `ws://127.0.0.1:25566/lapisclient`, a player name,
and the optional server access token. The UI shows connecting, negotiation,
authentication, world loading, connected, disconnected and failure states.
The server uses offline identities; names are not authenticated accounts.

`npm run build` creates **dist/**: deploy its contents to any static HTTPS host.
`npm start` serves an existing build for development. `HOST` and `PORT` configure
the static development server. Public HTTPS pages require a `wss://` game server.
See [the complete protocol and deployment guide](../docs/lapisclient-protocol.md)
for allowed origins, shared access tokens, LAN development and TLS termination.

## Controls

| Desktop | Action |
| --- | --- |
| WASD / mouse | Move / look (click canvas for pointer lock) |
| Space / Shift | Jump / sneak |
| Left / right mouse | Break or attack / place or use |
| 1–9 / wheel | Hotbar selection |
| E / Q | Inventory / drop item |
| T or Enter / Escape | Chat / menu |
| F | Fullscreen |

Touch mode provides independent multitouch joystick, look region, jump, sneak,
break/use buttons, hotbar, inventory, chat and menu controls. Settings can force
touch mode on a desktop; layout supports portrait/landscape and safe areas.

## Verification

```sh
npm test
npx playwright install --with-deps chromium
npm run test:web
```

The tests build and launch the real C server in isolated temporary worlds.
`ws` and Playwright are development dependencies only; the production build has
no npm runtime dependency. Integration tests cover session negotiation, binary
world decoding/meshing, two-player updates, actions, inventory, chat, reconnect,
access tokens and malformed inputs. Browser tests cover rendered geometry,
desktop pointer lock and movement, touch multitouch, chat, resize and reconnect.

## Supported behavior and limitations

The authoritative server owns world generation, edits, players, mobs, inventory
and gameplay. Local motion/collision is prediction; server movement bounds,
rate limits, existing border/plate checks and action validation still apply.
This is not a full anti-cheat physics implementation. Models, lighting, mining
and entity visuals remain simplified. Sound, sign text editing, advanced item
components, server commands and native mobile packaging are not implemented.
Client movement in experimental negative-Y plate voids is outside v1's scope.

## Contributing and license

See [architecture](../docs/client-architecture.md) and
[protocol](../docs/lapisclient-protocol.md). Preserve versioned semantic schemas;
never expose arbitrary TCP packets. Application code is GPL-3.0-only. Procedural
textures and geometry are original; see the root LICENSE and NOTICE.md.
