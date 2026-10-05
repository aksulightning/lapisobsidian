# Lapis Obsidian Client local bridge boundary

## Listener and authority

The bridge binds a single IPv4 TCP listener to `127.0.0.1:0`. It never binds an
unspecified, LAN or public address and offers no bind-address option. The OS
selects an unused port, so instances do not share a fixed port. Both HTTP assets
and `/bridge` WebSocket upgrade use this listener. The peer must be loopback and
the HTTP Host must exactly equal `127.0.0.1:<selected port>` (DNS-rebinding defense).
The upgrade Origin must exactly equal `http://127.0.0.1:<selected port>`; missing,
null, alternate-port and foreign origins are rejected. No CORS permission exists.

HTTP serves only compile-time embedded assets, without directory listings,
dynamic file reads, uploads, redirects or proxy routes. CSP permits bundled
scripts/styles/images and the exact local WebSocket endpoint; it disallows
frames, plugins, forms, inline script and evaluation. Native navigation is
restricted to the application root and new windows are denied. The shell grants
no Tauri IPC capabilities or shell/filesystem plugins. Source links are displayed
and can be copied without giving untrusted pages access to the app window.

## Authentication and ownership

Each launch generates 32 bytes from the OS random source. Failure to obtain
entropy aborts startup. The token is injected before page code runs, guarded by
an exact origin and top-frame check. It is not embedded in assets, placed in a
query/fragment, logged, persisted or used as a fixed source secret. The frontend
removes the bootstrap global and clears its reference after sending AUTH.
The native launch retains the token in memory for same-window reloads.

An upgrade has three seconds to supply AUTH, with bounded JSON and constant-time
comparison of fixed-length token bytes. Only one authenticated session is allowed
per launch; up to four unauthenticated upgrades can await authentication. Once the
active session closes, its slot is released. A page knowing only the public local
asset address cannot authenticate. Origins are not authentication by themselves.
Local malware running as the same OS user is outside this protection: it can
inspect process memory or spoof browser headers if it also steals the token.

## Internal protocol v1

All control messages are UTF-8 JSON text frames, limited to 1 KiB. Unknown fields,
unknown types, wrong types, malformed JSON and invalid states are rejected. WS
frames/messages are bounded to 8 KiB and outgoing buffering to 64 KiB. Controls
are limited to 30 messages per second per authenticated session; exceeding the
limit closes it. A peer silent for 35 seconds expires; the frontend sends PING
every ten seconds. Writes have a two-second deadline. No error includes a token,
raw server data, filesystem path or stack trace.

| Direction | Message | M1 behavior |
| --- | --- | --- |
| Client → bridge | `{"type":"AUTH","token":"<per-launch secret>"}` | Must be first; authenticate within 3 seconds. |
| Bridge → client | `{"type":"AUTH_OK","version":1}` | Bridge ready; does not mean connected to a remote server. |
| Client → bridge | `{"type":"SETTINGS","value":{…}}` | Strict bounded preference schema; caches values for the native launcher to save on exit. No paths accepted. Replies `SETTINGS_OK`. |
| Bridge → client | `{"type":"PREFERENCES","value":{…}}` | After AUTH_OK, optional previously saved preferences. |
| Client → bridge | `{"type":"PING"}` | Heartbeat; replies `{"type":"PONG"}`. |
| Client → bridge | `{"type":"CONNECT","host":"example.org","port":25565}` | Validate ASCII host/IP (max 253 bytes, bounded labels), u16 port 1–65535. Production responds `PROTOCOL_NOT_READY`; it performs no DNS lookup or dial. |
| Bridge → client | `{"type":"CONNECTED"}` | Only the restricted TCP echo build can produce this in M1. |
| Either direction | Binary frame | Raw transport bytes, no Base64 or JSON byte arrays. Production rejects because no connected game session exists. |
| Client → bridge | `{"type":"DISCONNECT"}` | Close any test TCP stream and reply `DISCONNECTED`; authenticated WS may remain. |
| Bridge → client | `{"type":"DISCONNECTED"}` | TCP session ended. |
| Bridge → client | `{"type":"ERROR","code":"…"}` | Finite code mapped to understandable frontend text. Invalid sessions terminate. |

The development-only Cargo feature `dev-echo` creates an internal echo server on
another OS-selected loopback port. CONNECT only accepts the exact literal address
and internally assigned port, and permits one TCP stream. It cannot select an
external target. Tests exercise actual WebSocket binary → TCP → WebSocket binary
flow and teardown. The Tauri dependency does not enable this feature, and release
CI builds the application without it. Never ship feature-unified all-features
workspace builds as user releases.

## Preferences

Dynamic HTTP ports cannot provide a stable local-storage origin. The native
launcher therefore reads at most 4096 bytes from its fixed application-config
`preferences.json`, validates every field and supplies the preferences after
AUTH_OK. Authenticated SETTINGS commands update only that bounded schema; the
frontend debounces slider updates. The native launcher saves the latest values
on clean exit using a private temporary file (0600 on Unix) and rename. Windows
replacement removes the old preferences file immediately before renaming. A
crash can lose unsaved changes; concurrent instances use last-clean-exit wins.
No path, token, script or arbitrary object can be stored through this interface.
Developer browser mode uses session-origin local storage, not native persistence.

## Shutdown

Window/application exit cancels the bridge. Active sessions leave their select
loops, drop test TCP streams and close WS sessions. The HTTP listener stops and
tracked tasks drain, with bounded waits so idle HTTP peers cannot hold exit.
Normal OS process termination also closes owned sockets; no detached bridge
process survives. The headless developer harness exits when its private stdin
pipe closes or on Ctrl-C. It emits credentials only into the private bootstrap
pipe and refuses terminal stdout. Do not log or archive that pipe.

## Requirements before Milestone 2 remote dialing

- Restrict admission to the inspected Lapis protocol and session state. A token
  plus arbitrary host and raw byte forwarding is still a generic TCP proxy and
  is not an acceptable release implementation.
- Validate incremental packet lengths (including across binary-frame boundaries),
  handshake target/version, string bounds and allowed initial intent. Test
  malformed, fragmented and coalesced frames using server fixtures.
- Specify allowed destination IP classes, DNS resolution/rebinding handling,
  connect and session deadlines, reconnect policy and user-visible failure states.
- Add directional backpressure and bounded queues for chunks larger than one
  WebSocket frame, slow peers, abrupt disconnects and half-closed TCP sessions.
- Preserve one selected game endpoint per authenticated session. No arbitrary
  inbound TCP listeners, SOCKS, HTTP CONNECT, forwarding or shell/file commands.
- Keep bridge authentication separate from protocol player identity; preserve the
  server's documented lack of encryption/offline UUID verification honestly.

Tests cover exact origins and Host checks, token freshness, failed/missing auth,
connection slots, auth expiry, malformed/oversized commands, rate limits, target
validation, binary state checks, absence of credentials in assets, local address,
shutdown and port reuse, plus restricted binary echo when explicitly enabled.
