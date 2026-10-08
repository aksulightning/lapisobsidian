# Lapis Obsidian Client architecture

The optional browser client uses the existing authoritative C server. It is an
independent implementation in ES modules, with no production npm dependencies.
Plain JavaScript keeps the embedded build independent of a TypeScript compiler.
The server's GPL license covers client code; artwork is separately licensed.

## Transport and ownership

Compile with `LAPIS_OBSIDIAN_WEB_CLIENT=1`. The dedicated C HTTP/WebSocket listener
serves a checked-in whitelist (`client/embed.list`) embedded by `tools/embed-web.sh`.
`/ws` forwards the existing framed protocol-772 byte stream into the same login,
configuration and gameplay handlers used by native TCP clients. It is not an
arbitrary TCP gateway. The native listener, saves, world generation, inventories,
permissions and block validation are unchanged. No Node runtime, asset download,
Java installation or external proxy is needed by the shipped server.

WebSocket framing is handled separately from game packet framing. Fragmented
messages and packets spanning messages are supported. Queues, packet sizes,
connection slots and deadlines are bounded. Generation flushes queued output
between chunk sections; readiness requires the spawn position and its chunk,
not the redundant second teleport. This preserves slow RISC-V loading behavior.

The server owns chunks, edits, entity positions, health, food and inventory.
The client simulates local movement/collision and sends positions, input flags,
teleport confirmations and gameplay actions. Inventory clicks send intentions;
server updates determine the visible stacks. Block changes are rendered only
when received. See [protocol coverage](client-protocol.md).

## Modules and rendering

- `src/network`: bounded packet codec, stream framing, chunk/configuration decoding.
- `src/world`: received chunk storage, updates, ray selection and collision.
- `src/rendering`: original WebGL 1 renderer, materials and state-shape lookup.
- `src/assets`: explicit texture/item/entity mappings and cached atlas loader.
- `src/audio`: original Web Audio synthesis and named sound-event mapping.
- `src/ui`: settings validation, direct connection and timeout rules.
- `src/core`: session lifecycle, authoritative updates, HUD and desktop/touch input.

Meshes contain only visible faces. Work yields between vertical layers under a
five-millisecond frame budget; nearest chunks build first. Opaque/cutout geometry
writes depth; water/glass geometry blends afterward with depth writes disabled,
ordered by chunk distance. Frustum checks use a chunk bounding sphere. The server
streams a five-by-five view; rendering distance is adjustable from one to three
chunks and does not expand the server's view. Biome IDs tint vegetation. Time
packets control ambient brightness, sky and fog. Water shortens visibility.

All textures are nearest-filtered 16x16 tiles in one cached 128x128 canvas atlas.
Per-face grass/log mappings are explicit. Missing images use an original violet
checker; failures are reported in chat. No fallback fetches external resources.
Item icons use the same approved tile files. Entities use distinct original
simple multipart models, with dropped-item metadata selecting an item tile.
Confirmed block removal emits bounded original particles. Special-block shape
and lighting simplifications are documented rather than hidden.

Audio loads original waveform definitions once. Browser audio starts only after
an input gesture. Events create short oscillators or deterministic noise buffers,
with envelopes, distance attenuation, a volume setting and a 24-voice cap.
Unknown events, failed definitions and unavailable Web Audio remain silent.

## Development server

`npm --prefix client run build` creates an independent static distribution.
`npm --prefix client run dev` serves it on loopback port 8081 and forwards only
`/ws` to one explicitly configured HTTP/WebSocket server (default loopback 8080).
It never routes arbitrary addresses or native TCP. Production normally uses the
embedded C listener or a TLS reverse proxy, preserving Host and Origin.
