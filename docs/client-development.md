# Client development

## Embedded production server

```sh
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build.sh
./lapis-obsidian
```

Open http://localhost:8080/ in a WebGL browser. For Alpine, including RISC-V:

```sh
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build-alpine.sh --static
./lapis-obsidian
```

The exact compile-time value `1` enables HTTP/WS and assets. Node/npm are **not**
required by either C build. Native builds and their tests remain independent.
`server.txt` accepts `port=25565`, `web-address=0.0.0.0`, `web-port=8080`.
For local development prefer `web-address=127.0.0.1`. Ports must differ.
Set a restrictive bind/proxy policy for your deployment; restart after edits.

The menu's server field accepts an HTTP(S) origin using the server's **web port**,
not its native port. Another origin opens that server's embedded client, keeping
WebSocket traffic same-origin. Enter a 1–15-character player name and join.

## Separate production assets and development

Node 22+ is needed only for these optional commands; there are no dependencies
or installation/download steps:

```sh
npm --prefix client run build
npm --prefix client test
npm --prefix client run audit
npm --prefix client run dev
```

`client/dist/` is generated and ignored. The dev server serves this build on
http://127.0.0.1:8081/ and forwards `/ws` to the already-running web-enabled C server
at http://127.0.0.1:8080. After changing sources, build again and refresh. To select
one other fixed backend/port:

```sh
CLIENT_SERVER_ORIGIN=http://127.0.0.1:9000 CLIENT_DEV_PORT=8082 npm --prefix client run dev
```

Production uses embedded assets, or the static distribution behind the same-origin
TLS reverse proxy as `/ws`. Preserve public Host and browser Origin on upgrades.
Do not expose the development server publicly.

## Controls

WASD/mouse walk/look; Space jumps/swims; Ctrl sprints; Shift sneaks. Hold left mouse
to mine/attack, right mouse to place/use; 1–9 selects hotbar; E inventory/crafting;
T chat/commands; Q/Ctrl-Q drops one/stack; R respawns after death; Escape opens
pause. Settings control render distance, sensitivity, volume and ambient tones.
The multiplayer world continues while the pause menu is open.

Touch controls are automatic on touch devices, with a saved checkbox override.
Left stick moves; drag the world to look; Mine/Use/Jump allow multiple fingers.
Inventory, Chat, Drop and Respawn buttons and selectable hotbar remain available.
Inventory's split-stack checkbox substitutes for right-click. Sprint/sneak are
currently keyboard controls. Portrait/landscape are covered by Chromium CI.
Other WebGL 1 browsers are expected to work but are not independently certified.

## Maintenance and validation

```sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
./tests/web-client.sh
SANITIZE=1 ./tests/web-client.sh
node tests/web_protocol.mjs
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build.sh
node tests/web_integration.mjs ./lapis-obsidian
npm --prefix client run build
npm --prefix client test
node tests/web_integration.mjs ./lapis-obsidian --dev-proxy
```

Browser verification (optional test dependency, not shipped in production):

```sh
npm install --prefix .tests/browser --no-save --no-package-lock playwright@1.51.1
node .tests/browser/node_modules/playwright/cli.js install --with-deps chromium
node tests/web_browser.cjs
```

CI performs desktop and phone interaction, render-pixel checks and screenshots.
It also validates static AMD64/RISC-V binaries and source-only builds.
Do not claim a browser run passed if its executable cannot be installed locally.

After adding/removing served files run `node client/scripts/routes.mjs`; the build
checks this whitelist. Original artwork regenerates with
`node client/scripts/generate-originals.mjs`. After manual source/license/artwork
review, `node client/scripts/manifest.mjs` updates exact asset hashes. Never use
manifest regeneration to approve unknown assets automatically.

## Troubleshooting

Loading displays received bytes and chunks. A spawn position and its matching
chunk unlock play; 60 seconds without data or five minutes without a usable spawn
produces an error. Inspect server logs and configured web port on slow machines.
No Java/resource extraction is needed. Missing textures visibly use original
checker art; failed audio definitions are silent. Audio requires a join/input
gesture and a nonzero volume setting. WebGL errors explain hardware requirements.
Use [protocol limitations](client-protocol.md) to distinguish unsupported native
interfaces from connection errors. This implementation does not provide full
native-client parity.
