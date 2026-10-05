# Lapis Obsidian Client local bridge

Implementation: `client/runtime/bridge.mjs`, `gate.mjs`, `launcher.mjs`.

Startup selects `127.0.0.1:0`, creates a cryptographically random 32-byte token,
then opens the local HTTP origin in the system browser. The token is delivered
as a URL fragment and removed by the frontend. The first WebSocket message must
be AUTH within three seconds. The bridge compares the secret in constant time.
Exactly one authenticated frontend is admitted; up to four unauthenticated
handshakes may be pending. The bridge does not support public/LAN listening.

HTTP serves only built browser files and requires the exact loopback Host.
WebSocket upgrades require `/bridge`, that same Host, the exact HTTP Origin and
a loopback peer. Security headers include a restrictive CSP, no framing,
no-referrer, nosniff and no-store. Workers and modules are same-origin.

## Controls

UTF-8 JSON control objects have exactly the listed fields. Unknown fields/types
are rejected. Frontend control messages are limited to 4 KiB and 30 per second.

| Direction | Message | Fields |
|---|---|---|
| Client → bridge | AUTH | `type`, `token` (64 hex characters) |
| Bridge → client | AUTH_OK | `type`, `version: 1` |
| Client → bridge | CONNECT | `type`, `host`, `port` |
| Bridge → client | CONNECTED | `type` |
| Client → bridge | DISCONNECT | `type` |
| Bridge → client | DISCONNECTED | `type` |
| Bridge → client | ERROR | `type`, bounded symbolic `code` |
| Client → bridge | PING | `type` |
| Bridge → client | PONG | `type` |
| Client → bridge | SHUTDOWN | `type`; ends this application instance only |

After CONNECTED, binary frames carry the protocol byte stream. WebSocket
boundaries do not imply packet boundaries. No number-array JSON encoding is used.
The bridge caps WebSocket messages at 64 KiB, client protocol packets at 8 KiB,
client traffic at 256 KiB/s and queued client TCP writes at 256 KiB. Outbound
server traffic pauses TCP reads under WebSocket backpressure; stalled sessions
are terminated. Authentication, connect and idle timeouts are bounded.

Hostnames are ASCII DNS labels or IP literals, with ports 1–65535. An IPv6
literal is entered without URL brackets. URLs, userinfo, whitespace, paths,
invalid DNS labels and port zero are rejected. The game server can be remote
or on the same device. A direct loop back into the bridge is rejected.

Before forwarding any client data, the gate requires a complete protocol-772
login handshake containing the CONNECT target and login intent. It requires
valid player-name/UUID framing, login acknowledgement and appropriate packet IDs
in configuration/play. This is protocol admission, not gameplay simulation.
No SOCKS negotiation, HTTP CONNECT, raw arbitrary forwarding, file paths,
filesystem read/write operations or executable commands are exposed.

DISCONNECT destroys TCP while retaining the authenticated local session for
reconnect. WebSocket close destroys TCP. Shutdown closes all WebSockets and
HTTP peers, stops the listener and releases its port. Closing the last
client tab triggers shutdown after 2.5 seconds; starting without a browser
connection expires after 60 seconds. The developer-only `--bootstrap-stdio`
launcher mode requires a non-terminal private pipe and exits when stdin closes.
It uses the same bridge, not a test-only transport implementation.

The security boundary protects against unrelated web origins, accidental LAN
exposure and unauthenticated local WebSocket access. It does not protect a token
from a malicious browser extension or a process with the same OS-user access.
Supported server login is offline and unencrypted. The bridge does not add
remote server identity verification or account authentication.
