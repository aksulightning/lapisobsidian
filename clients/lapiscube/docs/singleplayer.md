# Standalone browser singleplayer

`make -C clients/lapiscube singleplayer` builds **LapisCube-singleplayer.html**
separately from the native and multiplayer clients. Use Emscripten **4.0.23**,
Make, Python 3 and the pinned engine submodule. The dedicated GitHub Actions
workflow is `.github/workflows/lapiscube-singleplayer.yml`.

The complete game is one file at
`clients/lapiscube/build/singleplayer/LapisCube-singleplayer.html`. Download the
`LapisCube-singleplayer` Actions artifact and extract that HTML file. It embeds
the client, server, WebAssembly modules, textures, sounds, styles, licenses and
asset provenance. No companion files or network downloads are needed during play.

Open the HTML directly in a current desktop Chromium browser, including while
offline. You can also serve the same single file over HTTPS or localhost, for
example `python3 -m http.server 8080` from its directory, then open
`http://localhost:8080/LapisCube-singleplayer.html`. Browsers that restrict storage
for local files require this hosted option. No native game server, account,
WebSocket endpoint, proxy, cross-origin isolation or multiplayer service is needed.

The client and the existing C server compile to separate WebAssembly modules.
Both modules are embedded in the HTML; the server runs in a Blob-backed dedicated
worker and exchanges bounded binary protocol
streams with exactly one local client. The singleplayer client has no network
socket constructor and the server has no listening socket. Gameplay, terrain,
crafting, mining and inventory still use the shared authoritative server code.
The multiplayer build and default TCP server keep their existing build paths.

## Saves

One world is stored in IndexedDB for this site/browser profile. Keep the same
player name to resume the same inventory. Saves run every 15 seconds and when
you press **Save world**. **Save & quit** waits for the database write before
returning to the title screen. Use it before closing the tab; browsers cannot
guarantee asynchronous saves during tab/process termination. Clearing site data
removes the world. Different origins, ports or browser profiles have separate
saves. Local-file storage rules vary by browser; use the same file location and
profile to resume, and do not expect saves to follow the HTML to another device.
This initial standalone release does not have world export/import.

A Web Lock prevents two tabs from writing the same world. Storage failures are
displayed; failed loads never silently start a replacement world. Touch controls
retain the landscape/fullscreen requirements of the multiplayer browser client.
World simulation runs in the worker and is subject to background-tab throttling.

## Licenses and validation

The embedded server is GPL-3.0. Open **Open source & asset credits** on the title
screen to read the bundled server/client/engine licenses and corresponding-source
revision/build instructions. The same panel downloads all notices and
CC0/public-domain asset evidence as a ZIP generated from bytes inside the HTML.
Run `python3 clients/lapiscube/tests/singleplayer_browser.py` after building.
It copies only the HTML to an otherwise empty directory and checks both HTTP
and direct `file://` launch. Gameplay runs with networking disabled. It checks
real WASM world loading and input, embedded sound decoding, offline notices,
absence of companion-file requests or WebSockets, save/reload and the second-tab
lock. The separate YAML workflow uploads the HTML itself as the game artifact.

## Optional persistent drops

Build with `LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 make -C clients/lapiscube singleplayer`
to include drop registries in the browser save. Use Save & quit; durability is
the last completed IndexedDB save. Offline singleplayer rejects the experimental
multiplayer authentication flag. See [experimental systems](../../../docs/experimental-systems.md).
