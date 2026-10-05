# Browser-to-TCP gateway

This Cloudflare Worker connects the browser client to one operator-configured
Lapis Obsidian TCP server. No runtime is installed on the player's device.
The game server and protocol 772 remain unchanged. The Worker is a transport
gateway, not a game server; the TCP game server must still be running.

## Deploy

1. Set `SERVER_HOST` and `SERVER_PORT` in `wrangler.jsonc` to the public game
   server's address. Leave `ALLOWED_ORIGIN` set to the Pages origin above.
2. With a Cloudflare account, run `npx wrangler@4 deploy` from this directory.
3. Put the returned `wss://<worker-address>/bridge` URL and the same host/port
   in `../web/config.js`, then push `testing-client`. These are public values.
   Players can also enter them in the connection screen without rebuilding.
4. Open https://aksulightning.github.io/lapisobsidian/ and choose Play / Connect.

No gateway has been deployed by adding this code. A blank `SERVER_HOST` returns
503, and a blank client gateway leaves the connection form for the server owner
to configure. Do not put account credentials or API tokens in client config.

## Scope and limits

The gateway rejects foreign browser origins and connections to any host/port
other than the configured target. It checks the protocol handshake before
forwarding client bytes, enforces message/rate/queue limits, and closes idle
connections. It is a public game gateway: Origin checking is not user
authentication and non-browser clients can forge Origin. Use provider access
controls/rate limits for a private server; monitor usage and provider quotas.

Cloudflare blocks some TCP destinations, including private/loopback addresses
and Cloudflare IP ranges. Use a reachable DNS-only game hostname. See
https://developers.cloudflare.com/workers/runtime-apis/tcp-sockets/ .
The existing local launcher remains available for private/LAN TCP servers.

Run `node --test tests/hosted.test.mjs` from `client/` for the transport and
gateway session checks. Live Cloudflare connectivity needs deployment and a
reachable protocol-772 server; unit checks do not prove live gameplay.
