# Alternative client development

Requires Node.js 22+ for the separate client build; the C compiler does not need
Node.js. From the repository root:

```sh
npm --prefix client2 run build
npm --prefix client2 test
node client2/scripts/audit.mjs
LAPIS_OBSIDIAN_WEB_CLIENT=2 ./build.sh
LAPIS_OBSIDIAN_CLIENT2_DIR="$PWD/client2/dist" ./lapis-obsidian
```

Set `web-address=127.0.0.1` and `web-port=8080` in `server.txt` for local use.
Open `http://127.0.0.1:8080/`, choose Multiplayer, enter that server's **web**
address (not its native TCP port) and a 1–15 character alphanumeric/underscore
name, then Connect. For another origin, the client navigates to its web page
before opening a same-origin WebSocket. Browser security prevents raw TCP.

WASD moves; mouse looks; Space jumps; Ctrl sprints; Shift sneaks; 1–9/wheel
selects a slot; left click mines; right click uses/places; E opens server
inventory (creative mode offers blocks for the selected hotbar slot); T opens
chat/commands; Escape pauses; R requests respawn after death; F3 shows debug;
F5 changes perspective. Local practice world retains upstream sandbox behavior;
it is separate from server play. Settings remain in browser cookies.

Production files are in `client2/dist` and are not committed. Run the same build
command after editing modules, then reload the page; no bundler/dev server or npm
runtime dependency is required. `routes.list` is committed metadata and must
match the build. Static musl/RISC-V builds use
`LAPIS_OBSIDIAN_WEB_CLIENT=2 ./build-alpine.sh --arch riscv64 --static` on the
appropriate toolchain; ship `client2/dist` separately with all its notices.

Verification:

```sh
LAPIS_OBSIDIAN_WEB_CLIENT=2 ./tests/web-client.sh
SANITIZE=1 LAPIS_OBSIDIAN_WEB_CLIENT=2 ./tests/web-client.sh
node tests/web_integration.mjs ./lapis-obsidian --client2
npm install --prefix .tests/browser --no-save --no-package-lock playwright@1.51.1
node .tests/browser/node_modules/playwright/cli.js install --with-deps chromium
node tests/web2_browser.cjs
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

Browser tests use the real C executable with an isolated temporary world and
check configuration, chunks, rendering, movement, edits, inventory, chat and
clean disconnect. CI also checks mode 1 and native builds. No server or world
from a normal installation is touched. `WEB_TEST_TIMEOUT_MS=180000` permits
slower RISC-V emulation for protocol tests.

HTTP 503 means the separate client has not been built or its directory is
wrong. Pass an absolute `LAPIS_OBSIDIAN_CLIENT2_DIR` when launching from a world
directory. A startup error generally means missing WebGL support or files. A
terrain timeout means no supported spawn chunk arrived; inspect server logs.
This client supports this server's offline login only, not authenticated online
servers. Use a trusted network by default; for remote use put the listener behind
a TLS reverse proxy preserving Host/Origin and `/ws`, with access control.
The C listener has no TLS and offline names do not prove identity.
