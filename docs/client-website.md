# Browser client on GitHub Pages

The main Pages URL now opens `client/web/`, the actual HTML5 game client, using
an immediate relative redirect from the repository-root `index.html`.

- Play: https://aksulightning.github.io/lapisobsidian/
- Direct client: https://aksulightning.github.io/lapisobsidian/client/web/
- Optional project information: https://aksulightning.github.io/lapisobsidian/website/

## TCP multiplayer

The browser mode has no local-launch token requirement. It accepts a secure
WebSocket gateway, server host/port, and player name. The protocol, renderer,
world processing, and TCP game server are retained.

A deployable Cloudflare Worker gateway is in `client/gateway/`. It forwards
only protocol-772 traffic to its operator-configured TCP destination. It must
be deployed to a hosting account and pointed at a running game server before
players can connect. No public gateway/server is configured by default. Follow
[the gateway instructions](../client/gateway/README.md), then set public defaults
in `client/web/config.js`. Do not put secrets in browser files.

The original authenticated loopback launcher continues to work for local and
LAN servers. The hosted gateway does not expose or weaken that listener.

## Publishing

The current configuration is **Deploy from a branch → testing-client / (root)**.
GitHub's `pages build and deployment` workflow publishes changes automatically.
The root redirect and all client modules use relative URLs under `/lapisobsidian/`.

The `Browser client` workflow also validates the client/gateway and packages
`.pages/` with the same root redirect and directory structure. If Pages is later
switched to **GitHub Actions**, this workflow deploys that artifact directly.
Its deployment job intentionally skips while branch publishing is selected.

## Verification

Run `node --test client/tests/hosted.test.mjs` from the repository root. These
checks cover secure gateway URLs, the fixed destination, protocol admission,
real TCP byte forwarding, reconnects, and the hosted browser transport.
They do not replace a live deployed-worker/game-server test.
