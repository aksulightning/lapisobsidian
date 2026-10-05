# Lapis Obsidian Client

An HTML5 game client for the offline Lapis Obsidian server in this repository’s
`testing` branch, protocol **772**. The browser draws live server chunks and
runs the player; a bundled local runtime handles the browser-to-TCP boundary.
The local launcher needs no external proxy, Electron, Java installation, or proprietary asset pack.

## Play in a browser

Open https://aksulightning.github.io/lapisobsidian/ to load the actual game client.
Enter the secure gateway URL supplied by your server owner, the matching TCP
server address/port, and your player name, then choose **Play / Connect**.
No local Node installation is needed for this mode.

The hosted gateway still needs to be deployed and configured before live play
is possible. See [gateway deployment](gateway/README.md). Set public defaults
in `web/config.js` after deploying it. The gateway translates WebSocket traffic
to the existing TCP protocol; it does not replace or host the game server.
An empty configuration does not imply that a public game server is available.

## Develop and run

Requires Node.js 22.12 or later and an installed WebGL2 browser. Linux needs a
working desktop browser association (`xdg-open`); macOS uses `open`; Windows uses
the default browser association.

```sh
git clone https://github.com/aksulightning/lapisobsidian.git
cd lapisobsidian
git checkout testing-client
cd client
npm ci
npm run dev
```

The launcher selects an unused **127.0.0.1** port, generates a 256-bit secret,
starts its own bridge, and opens the HTML5 app in the system browser. Enter your
server address, TCP port and a 1–15 character ASCII player name, then Play / Connect.
Closing the client tab shuts down the runtime after a short reload grace period;
Quit closes it immediately. Disconnect leaves the local runtime ready to reconnect.
Do not start a second copy in the same tab. Each launch has a separate port/token.

For a local supported server, in another terminal at the repository root:

```sh
./build.sh
./lapis-obsidian
```

This is the game server, not a proxy. The client can also connect to a remote
instance of that server. The client does not start or distribute a game server
process. Server config, permissions, saves, game modes and gameplay rules remain
owned by the unchanged server implementation.

## Build a standalone distribution

```sh
npm run build
```

This builds the frontend, derives its semantic registry from the server snapshot,
and produces `release/lapis-obsidian-client-<platform>-<arch>/` containing:

- HTML/CSS/JavaScript modules and the worker;
- the local bridge and launcher;
- a copy of the build machine’s Node executable and the `ws` dependency;
- shell/Windows launchers, licenses, and corresponding project source.

Launch `Lapis-Obsidian-Client.sh` on Linux/macOS or
`Lapis-Obsidian-Client.cmd` on Windows. Recipients need an installed WebGL2 browser,
but **do not need Node/npm or a separate bridge installation**. Build separately
on each target OS/architecture. Linux runtime compatibility follows the copied
Node binary’s libc requirements. This is a portable directory, not a signed
installer. `npm run build:standalone` is an alias for the complete build.
`npm run build:web` builds only browser assets; these connect through a configured hosted gateway or the bundled local runtime. `npm start` runs previously built assets.

## Architecture

```text
HTML5 UI + input → player/world logic → protocol codec → transport
                                                          ↓ binary WebSocket
                                           authenticated loopback bridge
                                                          ↓ TCP
                                             Lapis Obsidian server
```

`web/main.js` owns menus and connection presentation. `input.js` supplies input
intentions independently of gameplay. `game.js` owns movement, collision,
interaction and synchronization. `protocol/` owns packet framing, encoding and
server packet decoding. `transport.js` owns WebSocket lifecycle only.
`world/worker.js` decodes chunks and builds meshes off the UI thread.
`renderer.js` uses WebGL2, depth testing, a procedural atlas, transparency,
distance/frustum culling, fog, and bounded display resolution.

No WASM is used: the inspected C implementation is a server whose serializers
write directly to file descriptors. A WASM build would bring server simulation
and sockets without providing a client decoder. The client instead reuses the
checked-in semantic registry directly and traces packet layouts to the C sources.
The actual C server is the interoperability test fixture, not a simulated server.
See [protocol provenance](../docs/client-architecture.md).

Tauri was evaluated from the previous implementation. Its native toolchain adds
platform prerequisites and could not be built in the implementation environment.
A bundled Node runtime with the installed browser provides a testable portable
HTML5 launcher without bundling a browser engine. Node is used for transport and
launching; the frontend has no Node APIs or Node-only execution requirement.

## Local bridge security

The listener binds only to `127.0.0.1:0`, never to a LAN or public interface.
IPv6 **remote targets** are accepted; an IPv6 local listener is unnecessary.
The bridge validates the HTTP Host, WebSocket Origin and loopback peer, requires
a per-launch random token, admits only one authenticated client, bounds control
and binary messages, applies traffic/backpressure limits and cleans up sessions.

The launcher passes its token in a URL fragment, which is not sent in HTTP
requests. The frontend immediately removes the fragment from visible history
and keeps the token only in that tab’s session storage for reload. Browser
extensions/history capture and other processes running as the same user are
outside this boundary. Tokens are not logged or saved in configuration files.

The bridge accepts a validated target only after authentication. Before any
client bytes reach TCP, it requires a protocol-772 login handshake naming that
same target. It frames subsequent client packets and permits only supported
state-specific packet IDs. It is not a SOCKS/HTTP proxy or unrestricted raw TCP
forwarder. It has no filesystem, shell or arbitrary process execution command.
See [bridge contract](../docs/client-bridge.md).

## Controls

| Action | Desktop | Touch |
|---|---|---|
| Move | WASD | Left joystick |
| Look | Click world for pointer lock, move mouse | Drag right look region |
| Jump / swim up | Space | Jump |
| Sneak | Shift | Hold Sneak |
| Mine / attack | Hold left mouse | Hold Mine / Hit |
| Place / use | Right mouse | Use / Place |
| Hotbar | 1–9, mouse wheel | Tap slot |
| Drop item | Q | Inventory interactions |
| Chat / commands | T or Enter | Chat |
| Inventory | E | Inventory |
| Pause UI / release pointer | Escape | Menu |
| Fullscreen | F or Settings | Settings → Fullscreen |

Movement, look and action touches have independent pointer capture. Hybrid
hardware can force touch controls in Settings. Layout handles phone/tablet
sizes, portrait and landscape, safe areas and fullscreen where the browser
permits it. Inventory supports click/tap pickup and placement, right-click
splitting and Shift-click quick movement; a creative palette is available when
the server grants creative mode. The server remains authoritative for actions.

## Tests

A C compiler and Bash are needed to compile the unchanged reference server for
integration checks. These are test tools, not client runtime dependencies.

```sh
npm test                         # builds C fixture; codec, bridge, real-server and lifecycle tests
npx playwright install chromium
npm run test:web                 # actual live-server desktop/touch browser checks
npm run build                    # complete portable package
```

`LAPIS_TEST_BROWSER=/path/to/chromium` selects an existing Chromium executable.
Tests create disposable worlds in `test-results/`; never use a production save.
Browser checks exercise live world geometry, pointer lock, keyboard/mouse input,
simultaneous touch movement/look/jump, chat, hotbar, inventory, resize and reconnect.
The CI workflow runs the same suite. See [verification](../docs/client-verification.md)
for actual results and environment limitations, rather than assuming a test’s
presence means it has passed.

The original server gates remain available: `./tests/run.sh`,
`SANITIZE=1 ./tests/run.sh`, and `node build_registries.js --check` from the root.

## Supported behavior and limitations

- Supported target: this repository’s **unencrypted, uncompressed offline server**
  at `testing` commit `e96af88797b0718851ae8422415467cb94253982`. This is not a
  general-purpose client for arbitrary servers of the same protocol number.
  Encrypted login/compression requests fail visibly; no online account login.
- Login, streamed chunks, block updates, position corrections, basic player
  physics, hotbar, chat/commands, container interactions, health/respawn and
  basic entity movement are implemented. Server permission checks still apply.
- Original block textures and simple entity boxes replace proprietary assets.
  Specialized block models, animation, lighting, exact survival mining timing,
  sounds, signs/text editing, advanced item components and full fluid physics
  are not equivalent to a full commercial game client. Transparency is sorted
  by chunk; intersecting transparent surfaces can have visual artifacts.
- Render distance cannot exceed the data the server sends. Meshing runs in a
  worker; no measured mid-range phone frame-rate or battery claim is made.
- Settings are stored in the current browser origin. Because the port changes
  per launch, preferences are not currently restored across new launches.
  Offline player UUIDs are deterministically derived from the entered name;
  names are not authenticated accounts.
- The same frontend includes functional touch controls. Native Android/iOS
  packaging is **not included**. A phone cannot connect to a desktop’s loopback
  bridge; deploying a same-device mobile wrapper is required for native mobile
  distribution. Browser mode can use the hosted gateway to reach its configured public server; the loopback bridge remains private.
- Browser pointer lock/fullscreen depend on browser/OS support. Graphics context
  loss displays an error and requires reloading. Windows/macOS packaging must
  be built and tested on those systems; signing is not configured.

## Contributing and license

Work against `testing-client`. Keep protocol behavior tied to `testing` and add
real-server tests when changing packets. Keep UI/input, protocol, transport and
world processing separate. Run the checks above and include screenshots for UI
changes. Keep the product name **Lapis Obsidian Client** and do not add proprietary
assets. New client source is GPL-3.0-only; existing source retains its notices.
See [LICENSE](../LICENSE) and [NOTICE.md](../NOTICE.md). Node and `ws` retain their
own licenses, included in the portable distribution. Distributors should use
the license/source correspondence of the Node version they package.

