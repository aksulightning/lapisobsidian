# Standalone browser singleplayer

`make -C clients/lapiscube singleplayer` builds **LapisCube-singleplayer.zip**
separately from the native and multiplayer clients. Use Emscripten **4.0.23**,
Make, Python 3 and the pinned engine submodule. The dedicated GitHub Actions
workflow is `.github/workflows/lapiscube-singleplayer.yml`.

Extract the ZIP and serve its `singleplayer/` directory as static files over
HTTPS or localhost, for example `python3 -m http.server 8080`. Open
`http://localhost:8080/` in a current WebAssembly/WebGL browser. Opening
`index.html` with `file://` is not supported. No native game server, account,
WebSocket endpoint, proxy, cross-origin isolation or multiplayer service is needed.

The client and the existing C server compile to separate WebAssembly modules.
The server runs in a dedicated worker and exchanges bounded binary protocol
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
saves. This initial standalone release does not have world export/import.

A Web Lock prevents two tabs from writing the same world. Storage failures are
displayed; failed loads never silently start a replacement world. Touch controls
retain the landscape/fullscreen requirements of the multiplayer browser client.
World simulation runs in the worker and is subject to background-tab throttling.

## Licenses and validation

The embedded server is GPL-3.0; see `LICENSE-server.txt` and `SOURCE-server.txt`.
Client/engine licenses and CC0/public-domain asset evidence are bundled as well.
Run `python3 clients/lapiscube/tests/singleplayer_browser.py` after building.
It serves only static files, checks real WASM world loading and input, verifies
that no WebSocket connects, saves/reloads, and checks the second-tab lock.
