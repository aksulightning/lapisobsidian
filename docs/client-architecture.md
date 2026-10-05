# Lapis Obsidian Client architecture and protocol provenance

The single replacement implementation lives in `client/`. The previous Tauri /
Rust preview, TypeScript free camera, static island, echo-only bridge, placeholder
Connect control, and their tests/build configs have been removed. The C server
under `src/` and `include/` is unchanged and remains the authoritative reference.

Inspected reference: `testing` commit
`e96af88797b0718851ae8422415467cb94253982`. `testing-client` had identical server
sources, but explicitly returned `PROTOCOL_NOT_READY` for every remote CONNECT.
It contained no client-side login, world decoder or gameplay. Enabling that one
control alone could not make it a game client.

| Reference source | Reused behavior / client location |
|---|---|
| `include/protocol.h`, `generated/registry_snapshot.json` | Build-checked protocol number, core-pack version, block-state/item identities; `web/protocol/registry.js` is generated automatically. |
| `src/main.c` | Login/configuration/play transition order and serverbound packet IDs; `protocol/client.js`. |
| `src/packets.c` | Login, client information, known packs, position sync, slots, keepalive, chunks, entity spawning, health and chat. |
| `src/worldgen.c` | Section indexing and packed long byte order (`index ^ 7` in the 8-bit server palette); decoded in `world/world.js`. The client renders transmitted terrain instead of generating its own world. |
| `src/sign_packets.c` | Fully framed action/use packets, sequence counters, position and cursor fields. |
| `src/inventory_packets.c` | Hashed-slot click framing, container IDs and authoritative inventory replies. The client submits zero prediction changes and uses server corrections. |
| `src/command_packets.c` | Unsigned chat/commands and creative slot format. |
| `src/mob_packets.c`, `src/packets.c` | Relative/absolute entity movement and removal; simple geometry represents received entities. |
| `src/doors.c`, `src/farming.c`, `src/circuits.c` | State variants for basic material selection; specialized rendering remains limited. |

The server serializer is not a reusable client codec. Compiling the entire C
server to WASM would retain server world generation and socket dependencies,
while still needing inverse decoders and browser gameplay. JavaScript typed
buffers are used for the inverse codec. The semantic registry is reused without
copying proprietary textures or a game installation. The known-pack reply means
compatibility with this server’s minimal semantic snapshot, not an asset pack.

The packet framer handles arbitrary TCP splits/coalescing with bounded buffering.
Malformed lengths, unsupported login modes and invalid palettes abort the session.
Chunks use 24 sections starting at Y=-64; protocol 772 containers do not carry
the old long-array-length VarInt. The actual server emits a zero heightmap count.
The world worker rejects other heightmap layouts instead of guessing.

The main thread owns the player, protocol, input and UI. A generation counter
invalidates worker messages on disconnect/reconnect or dimension change. The
worker owns a second chunk cache and coalesced dirty-mesh queue. Transferable
buffers carry meshes and collision arrays. Neighbor arrival/block changes mark
adjacent meshes dirty. Distance-based cache eviction releases both CPU and GPU
resources. Client loading pauses physics until the spawn chunk and position
arrive; server corrections replace player position and are acknowledged.

WebGL2 renders only meshes decoded from server packets. It uses a deterministic
original texture atlas, depth testing, per-face shading, fog, frustum/distance
culling and a separate transparent pass. Entity boxes reflect received positions;
they do not pretend to be full models. Collision and jump/swim/sneak behavior are
client-side approximations synchronized to server movement packets at 20 Hz.

The standalone build copies Node’s runtime rather than compiling a second
native stack. It hosts assets, starts its restricted loopback bridge and opens
the system browser. A tab close triggers remote socket cleanup, listener close
and process exit; a short grace allows reload. Development and portable packages
use exactly the same runtime/frontend implementation. No external service is
needed. Native mobile wrappers and signed desktop installers are not supplied.
