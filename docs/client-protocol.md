# Client protocol compatibility

Verified against `include/protocol.h`, `src/main.c`, `src/packets.c`, generated
registries, item/mob/circuit/sign/door helpers on the PR branch based on `testing`.
The supported version is **772 / Java 1.21.8**, with exactly
`minecraft:core:1.21.8`. These are compatibility identifiers, not asset sources.

## Login and configuration

Handshake 0 selects login and carries version 772; Login Start 0 carries a bounded
name and browser-generated UUID. The server rejects different protocol versions.
Login Success 2 is acknowledged with 3. Configuration Client Information 0 sends
locale and preferences. Known Packs 0x0e is parsed and validated before reply 7;
Registry Data 7 is parsed into named identifier lists (the C snapshot sends
unnamespaced registry keys such as `worldgen/biome`). The built-in snapshot omits
NBT payloads; unexpected registry NBT or a different pack is rejected clearly.
Finish Configuration 3 is acknowledged with 3. Tags are compatibility-only.

The server does not issue an encryption or compression negotiation, authenticate
accounts, or verify UUID ownership. Local-storage identities allow reconnection,
but are not secure accounts. This client is for Lapis Obsidian's compact subset,
not a general client for arbitrary protocol-772 servers.

## Implemented play packets

| Direction | IDs | Handling |
| --- | --- | --- |
| Server → client | 0x2b, 0x41, 0x57, 0x4b | Login, absolute teleport, center chunk, dimension respawn |
| Server → client | 0x27, 0x08 | Chunk/light packet subset, authoritative single-block updates |
| Server → client | 0x01, 0x1f, 0x2e, 0x2f, 0x46 | Entity spawn, absolute/delta positions, removal |
| Server → client | 0x5c, 0x75 | Supported byte/bool/VarInt/Slot metadata; dropped items and pickup |
| Server → client | 0x14, 0x59, 0x62, 0x34 | Slot, cursor, hotbar and container updates |
| Server → client | 0x61, 0x22, 0x72 | Health/hunger, game mode, plain NBT-string system chat |
| Server → client | 0x26, 0x6a, 0x6e | Keepalive, world time, inline named sounds |
| Client → server | 0, 0x1b, 0x2b | Teleport acknowledgement, keepalive reply, Player Loaded |
| Client → server | 0x1e, 0x29, 0x2a | Position/look/ground; sprint command; input including sneak |
| Client → server | 0x28, 0x19, 0x3c, 0x3f, 0x40 | Mining/drop/release-use, attack, swing, block/item interaction |
| Client → server | 0x34, 0x37, 0x11, 0x12 | Held slot, server-authorized creative slot, container click/close |
| Client → server | 6, 8, 0x0b | Commands, unsigned chat, respawn request |

Chunk sections range -4..19 (minimum Y=-64, dimension height 384). Negative
sections are the server's bedrock padding; client storage/rendering covers
Y=0..319/0..255 respectively. The server emits zero-bit uniform containers or
an eight-bit indirect palette of 256 states, packed eight entries per big-endian
long; `i ^ 7` reverses bytes inside each long. Each section has a single biome ID.
Heightmaps must be empty in this server subset. Light arrays are currently unused
by rendering; ambient/face lighting is original client approximation.

PacketStream bounds buffered data to 8 MiB and individual inbound packets to
2 MiB. C limits browser input game frames to 8192 bytes and message framing is
separate. Movement is sent at most 20 times/second, with idle heartbeats.

## Security and limitations

HTTP/WS and native TCP are unencrypted and offline. Use a trusted network or TLS
reverse proxy/protected tunnel, especially for `/admin`. Same-origin checks prevent
an unrelated browser origin from opening the web endpoint; they do not authenticate
players or protect native TCP. The development proxy listens only on loopback and
has one fixed web backend. No unauthenticated arbitrary TCP bridge is introduced.
See [existing hardening](security-hardening.md) for inherited server limitations.

Unsupported: online authentication/encryption/compression, arbitrary palette widths,
registry NBT, item components, signed player chat, skins/resource packs, recipe
books, full metadata serializers, sign editing/block-entity text, entity equipment
and full fluid/special-block geometry. Unknown entity types receive original
fallback geometry; unknown sounds are silent. Doors/trapdoors/slabs have simplified
shape approximations. Mining timing and client collision are approximations; the
server still validates interactions and owns the inventory/world.

`node tests/web_integration.mjs ./lapis-obsidian` tests the actual executable:
configuration registry parsing, 25 chunks, native coexistence, two browser
identities, commands, movement, creative inventory, mining, placement, shared
updates and reconnection. `tests/web_browser.cjs` verifies terrain pixels and
interactive desktop/mobile behavior with Chromium; CI repeats live protocol tests
on the shipped AMD64 and RISC-V static binaries.

The separately licensed mode 2 client is documented in [client2-protocol.md](client2-protocol.md). Mode 1 behavior remains unchanged.
