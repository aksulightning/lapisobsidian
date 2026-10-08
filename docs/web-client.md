# Optional HTML5 client

The HTML5/WebGL client is **excluded by default**. Enable it when compiling:

```sh
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build.sh
./lapis-obsidian
```

On Alpine (including the existing cross-build and static options):

```sh
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build-alpine.sh --static
```

Only the exact value `1` enables the feature. Setting the variable when launching
an already-built server cannot enable or disable it. Rebuild without the variable
(or with `0`) to remove the HTTP/WebSocket implementation and all embedded assets.
The default C-only build and its restricted-PATH/no-Java test remain unchanged.
Opt-in builds also use the standard shell tools `mktemp`, `od`, `awk`, and `tr`
(available in Alpine's BusyBox). No Node, npm, Java, Emscripten, downloaded assets,
or extra link libraries are required to build or run either binary.

Open **http://localhost:8080/**, or use your server address and the configured
`web-port`. The web client has a dedicated HTTP/WebSocket listener, configured in
`server.txt`:

```ini
port=25565
web-address=0.0.0.0
web-port=8080
```

`web-address` is a dotted IPv4 bind address: `0.0.0.0` listens on all IPv4
interfaces; `127.0.0.1` restricts access to the local machine. `web-port` must be
1–65535 and differ from the native Minecraft `port`. Hostnames and IPv6 are not
supported. A bind failure stops startup with a clear error. Existing files that
omit the keys use the defaults above; restart after changing either setting.
These settings do not enable a web listener in a binary compiled without the
feature. See [configuration rules](server-config.md#web-listener).

No proxy process is required. Assets are embedded in the executable, so it can
run from a different world directory without a `client/` folder. The build embeds a
catalog from the existing C registry.

## Playing

Use a WebGL browser with touch controls or a keyboard and mouse. Enter a player
name; desktop players then click to capture the mouse. Touch controls are enabled
automatically on touch-capable devices and can be toggled on the join screen.
The browser remembers a random player UUID per
name in local storage so reconnects reuse the server's player record. As with the
native server, this is an offline identity, not account authentication. Do not
connect multiple tabs using the same saved identity.

| Control | Action |
| --- | --- |
| W/A/S/D, mouse | Walk and look |
| Space | Jump; swim upward in water |
| Hold left mouse | Mine a targeted block |
| Left mouse on an entity | Attack |
| Right mouse | Place/use selected item; hold to eat |
| 1–9 | Select hotbar slot |
| Q / Ctrl-Q | Drop one item / stack |
| E | Inventory, basic crafting and creative block picker |
| Click / right-click an inventory slot | Move a stack / split or place one |
| Shift-click a slot | Quick move |
| T | Chat and slash commands |
| R after death | Respawn |
| Escape | Release mouse or close a dialog |

On phones and tablets, drag the left stick to move and drag the world to look.
Use separate fingers to move/look/jump together. Hold **Mine** to mine or attack,
tap **Use** to place/use, and hold **Jump** to jump or swim. Tap hotbar slots to
select items; the bar scrolls horizontally on narrow screens. **Inventory**,
**Chat**, **Drop** and **Respawn** provide the remaining basic actions without
pointer lock. In inventory, enable **Split stack / place one item** to perform
the secondary slot action. The layout adapts to portrait and landscape; landscape
offers more space. Input is released when touches are cancelled or the page loses
focus. Native mobile devices still need WebGL support.

Terrain streams while the server generates it, with byte/chunk progress shown
during loading. The client enters when the spawn position and its chunk are both
available, without waiting for a redundant final teleport. Mesh generation yields
between layers to keep mobile input responsive. Loading fails with a visible
message after 60 seconds without data or five minutes without a usable spawn.

Gameplay uses the normal protocol-772 login/configuration and game handlers,
including server-owned inventories, game modes, command permissions, world edits,
Plates, player limits and saves. Native and browser players share the same world.
Creative blocks are available only when the server grants creative mode; the
client does not grant itself permissions. The default server mode is survival.

The client is named **Lapis Obsidian Client**. It now uses an approved CC0 texture
atlas, original multipart entity art and original synthesized audio, with settings,
particles, biome tint, day/night lighting, transparency and item icons. Sprint
(Ctrl), sneak (Shift) and pause/settings supplement the existing controls.
See [architecture](client-architecture.md), [development](client-development.md),
[protocol limits](client-protocol.md), [asset records](client-asset-sources.md),
[code provenance](client-third-party-code.md) and [compliance](client-license-compliance.md).
This remains a practical server-specific client with simplified special-block
geometry, collision, mining timing and lighting; skins, full entity metadata,
resource packs and sign editing are not implemented.

## Transport and deployment

The `/ws` endpoint accepts same-origin browser connections and carries the
existing framed Minecraft protocol as a binary stream. HTTP requests are limited
to 4 KiB; binary messages to 8,195 bytes (an 8,192-byte inbound game frame plus its
length prefix). WebSocket fragmentation, masking, ping/pong and closing are
handled. Partial requests/frames and stalled output have 15-second deadlines.
Output is nonblocking with an 8 MiB per-connection cap; slow clients disconnect
instead of growing buffers indefinitely. HTTP requests use the existing bounded
connection slots. There is no filesystem serving or arbitrary TCP destination.

Like the existing native listener, direct HTTP/WS is unencrypted. For HTTPS/WSS,
use a TLS reverse proxy targeting `web-address:web-port`, with WebSocket
upgrades on `/ws`, preserving the public
`Host` and browser `Origin` (including the port). The client chooses WSS on an
HTTPS page. Use the existing trusted-network/tunnel guidance for admin commands.

## Tests

```sh
./tests/run.sh
./tests/web-client.sh
SANITIZE=1 ./tests/web-client.sh
node tests/web_protocol.mjs

# Node 22+ integration tests, temporary worlds, no npm packages:
LAPIS_OBSIDIAN_WEB_CLIENT=0 ./build.sh
node tests/web_integration.mjs ./lapis-obsidian --disabled
LAPIS_OBSIDIAN_WEB_CLIENT=1 ./build.sh
node tests/web_integration.mjs ./lapis-obsidian
```

The disabled-binary test deliberately sets the runtime variable to `1`; the
web-binary test sets it to `0`. Native status requests are tested in both modes.
The web test covers configured binds, listener isolation, startup failures,
embedded assets, two clients, chunk decoding, inventory,
commands, mining, placement, shared edits and reconnects. The C transport tests
include fragmented input, the RFC handshake vector, origin rejection, malformed
frames, deadlines and the output bound, also under ASan/UBSan. A regression test
simulates a minute of generation before sending queued output; a writable peer
must survive, while a peer that actually stops reading still times out.

The optional browser smoke test installs development-only dependencies:

```sh
npm install --prefix .tests/browser --no-save --no-package-lock playwright@1.51.1
node .tests/browser/node_modules/playwright/cli.js install --with-deps chromium
node tests/web_browser.cjs
```

CI runs the Chromium smoke test and retains desktop and phone screenshots. It
checks terrain pixels, login without the redundant teleport, pointer lock,
reconnects, and phone multitouch movement/look/jump, cancellation, mining/placement,
chat, inventory and portrait/landscape layouts. The Alpine web jobs also run the
full live-server integration on both AMD64 and RISC-V (QEMU), including 25 chunks,
two players and reconnects. Emulation does not establish physical device speed
or compatibility with every mobile browser.
