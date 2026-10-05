# Client architecture

Lapis Obsidian Client → WebSocket (`lapisclient`) → native testing server.

- `web/transport.js`: direct browser WebSocket, negotiation, timeout, heartbeat,
  error handling, backpressure and reconnect cleanup.
- `web/protocol/client.js`: named application messages; no TCP packet parsing.
- `web/game.js`: local motion prediction, authoritative state/corrections and
  coordination of world, renderer and input.
- `web/world/world.js`: validates binary chunk headers and converts palette
  indices into render-state arrays; worker meshing remains off the main thread.
- `web/input.js`, `renderer.js`, `style.css`: shared desktop/touch input and WebGL2
  rendering. No separate mobile network path and no unnecessary WASM layer.
- `scripts/serve.mjs`: static development HTTP server only.
- `src/lapisclient_transport.c`: native bounded RFC 6455 listener and origin checks.
- `src/lapisclient_codec.c`: bounded strict flat JSON decoder.
- `src/lapisclient_session.c`: lifecycle, schema validation and semantic adapter.
  Its private bounded argument builder reuses existing controller validation;
  raw game packet IDs/bytes never appear in the public WebSocket protocol.
- Existing server output helpers publish semantic JSON or binary chunks for
  WebSocket peers. Unsupported TCP cosmetics are suppressed for those peers.

World generation, inventory, interaction, player persistence, entity simulation,
world-border and plate rules are shared with the existing server. There is no
second world simulation or forwarding socket. Both branch implementations share
identical server source. See [lapisclient v1](lapisclient-protocol.md) for every
schema, the binary chunk layout, configuration and deployment commands.
