# Lapis Obsidian Client: alternative implementation

`LAPIS_OBSIDIAN_WEB_CLIENT=2` selects a separately licensed adaptation of
LabyStudio/js-minecraft. Mode `1` remains the original optional client; unset/zero
retains the native-only server. This is a separate browser program, not a
replacement server. Both clients speak the existing protocol 772 over the same
bounded, same-origin binary WebSocket endpoint `/ws`.

The upstream Three.js renderer, section mesher, ambient occlusion, transparent
render phase, fog, sky/day-night renderer, camera, collision/swimming physics,
player models, arm/item rendering, particles, Canvas GUI widgets, menus,
settings, chat and hotbar are retained. `adapter/connection.mjs` connects their
multiplayer controller/world objects to the actual C server. `adapter/wire.mjs`
is a fresh, bounded binary codec; mode 1 GPL browser modules are not imported.
The server remains authoritative for terrain, entity positions, block edits,
inventory, health, food and commands. No raw TCP proxy or arbitrary destination
forwarding is added.

`adapter/registry.mjs` maps factual server registry IDs to renderer block IDs.
`catalog.mjs` is generated from the checked-in server registries; regenerate it
using `sh tools/client-catalog.sh > client2/catalog.mjs` after registry changes.
Sections cover server Y=0..319; below-zero sections are decoded and discarded.
The initial position and its actual chunk must arrive before player ticks and
movement start. Replacing a chunk removes its previous Three.js group.

All resource keys resolve to original Canvas artwork; no image or sound URLs
are requested. The 16×16 pixel atlas, UI sheets, font bitmap from the browser's
system monospace font, abstract panorama, character and celestial art are
generated in memory. Texture objects are cached. The upstream positional audio
pool creates original synthesized buffers instead of downloading recordings.

CC BY-NC code is deliberately not embedded in the GPL server executable. Mode 2
compiles only a static URL/file whitelist. The server serves bounded files from
`client2/dist`, or the trusted directory named by `LAPIS_OBSIDIAN_CLIENT2_DIR`.
URLs cannot select arbitrary files. Missing build files return HTTP 503 with
setup instructions; unknown paths return 404. Rebuild the server whenever the
client route list changes. Keep the directory read-only to untrusted users.
