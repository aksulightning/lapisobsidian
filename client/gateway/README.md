# Serverless HTTPS-to-TCP translator

The normal browser client uses `fetch()` over HTTPS. A Cloudflare Worker routes
its requests to a dedicated Durable Object, which owns one connection to the
configured TCP server. Browser game packets remain raw bytes in both directions.
There are no WebSocket connections or upgrades in this hosted connection path,
and players do not install anything. The TCP game server must still be running.

## Deploy

1. Set `SERVER_HOST` and `SERVER_PORT` in `wrangler.jsonc` to the public TCP
   server. Set `ALLOWED_ORIGIN` to the HTTPS website origin, without a path or
   trailing slash (for Pages: `https://aksulightning.github.io`).
2. From this directory, run `npx wrangler@4 deploy` using your Cloudflare account.
   The included `TCP_SESSIONS` binding and SQLite Durable Object migration create
   the session namespace. TCP sessions are transient; no game data is persisted.
3. Set the public `gateway` value in `../web/config.js` to
   `https://<worker-address>/bridge`, with the matching TCP host and port.
   Players may also enter these values in the connection screen.
4. Publish the browser files and open the site to play.

This replaces the previous hosted WebSocket gateway. Existing hosted `wss://`
URLs must change to `https://`; deploy the translator and updated client together.
The optional, pre-existing local launcher is separate and retains its local
transport. It is not needed by players using the website.

No live deployment is included in this change. A blank `SERVER_HOST` returns
503. Do not put Cloudflare credentials or other secrets in browser configuration.

## HTTPS protocol

All requests require the configured `Origin`. CORS preflight is supported.

| Request | Purpose | Response |
| --- | --- | --- |
| `POST /bridge` | JSON `{ "host": "…", "port": 25565 }`, matching the configured target | `201` with `{ "session": "…" }` after TCP connects |
| `GET /bridge/read` | Wait for bytes from that TCP connection | `200` binary bytes, `204` after 20 seconds without data, or `205` at TCP EOF |
| `POST /bridge/write` | Raw `application/octet-stream` body; `X-Sequence` starts at `0` and increases after each successful write | `204` after TCP accepts the bytes |
| `DELETE /bridge` | Close the TCP session | `204` |

After creation, send `Authorization: Bearer <session>` on every request. This
unguessable session capability stays in browser memory and request headers;
it is not placed in URLs, local storage, or application logs. Each new browser
connection receives a new session and TCP socket. Unknown/expired sessions
return `410`; TCP failures return `502`. Invalid or overlapping writes return
`409`, unsupported write content types `415`, and oversized bodies `413`.

One read and one write may be in flight simultaneously. Reads use long polling:
they return as soon as TCP bytes arrive, then the browser immediately issues
another read. This works with ordinary browser fetch, without relying on
full-duplex streaming uploads. The browser batches queued writes (up to 64 KiB
per POST), sends them in order, and waits for TCP write completion. It never
retries an uncertain write: doing so could duplicate bytes on the TCP stream.
Network failure ends the session; reconnect creates a new TCP connection.

TCP is a byte stream, so response boundaries need not match write boundaries.
The translator does not inspect or transform Minecraft packets. The game client
continues to implement protocol 772.

## Limits and lifecycle

- Only the operator-configured destination can be dialled.
- TCP connection setup and each write have a 10-second timeout.
- The browser permits 256 KiB of pending outbound bytes and batches at 64 KiB.
- The translator pulls TCP data on demand instead of accumulating an unbounded
  application read queue. Only one pending TCP read is retained across polls.
- Disconnect sends DELETE. Abandoned sessions expire after 60 seconds without
  requests. Closing the socket cancels its readers and writers.
- Sessions cannot survive an object restart or deployment. The client must
  reconnect. TCP half-closes are not exposed as an application feature.
- Long polling adds HTTP round trips and request volume. Durable Objects and
  active TCP connections consume provider resources; this is managed serverless
  hosting, not a promise of free operation or a replacement for the game server.

Origin checking is not player authentication; non-browser clients can forge it.
This is a public gateway to the configured server. Use provider access controls
and rate limits if the server is intended to be private.

Cloudflare blocks private/loopback TCP destinations and Cloudflare IP ranges in
production. Use a reachable DNS-only TCP hostname. Local runtime tests can use
loopback, but that does not prove production reachability. See the official
[TCP sockets documentation](https://developers.cloudflare.com/workers/runtime-apis/tcp-sockets/).

## Verification

From `client/`, run `npm test` for byte-integrity, session isolation, failures,
real TCP gameplay, and the actual Workers/Durable Objects runtime tests. Run
`npm run test:web` for browser verification. `npx wrangler@4 deploy --dry-run`
from this directory checks deployment packaging without publishing.
