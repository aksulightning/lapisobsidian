# Browser build and optional server hosting

LapisCube compiles its C engine to WebAssembly with WebGL 1. Desktop uses the
native keyboard/mouse controls. Touch devices use multi-pointer controls around
the same C game, require landscape fullscreen, and release all held inputs when
rotated, hidden, interrupted, or leaving fullscreen. Inventory stays authoritative
on the server. No Minecraft runtime, textures, audio or external CDN is required.

## Build

Initialize the pinned ClassiCube submodule, install Emscripten (tested with
emsdk 4.0.23), and activate its environment so `emcc` is on PATH. Python 3 and make
are also required. From the repository root:

```sh
git submodule update --init --recursive
LAPIS_ENABLE_WEBCLIENT=1 ./build.sh
./lapis-obsidian
```

Open `http://localhost:8080/`, or the server's LAN address on your phone. The build
places `webclient/` next to the server. Keep that directory when distributing the
binary, and launch from that directory's parent (or set `LAPIS_WEB_ROOT`). The
standalone browser archive is `clients/lapiscube/build/LapisCube-web.zip`.

`make -C clients/lapiscube web` builds only the browser archive. To bundle a
previously built, extracted package without Emscripten:

```sh
LAPIS_ENABLE_WEBCLIENT=1 LAPIS_WEBCLIENT_PREBUILT=/path/to/webclient ./build.sh
```

The installer verifies the package file set, hashes, WASM magic and licenses.
`./build.sh` without the exact flag value `1` produces the original TCP-only
server. A runtime environment variable cannot enable HTTP/WebSocket in that
binary. The game TCP port remains unchanged for native/Java clients.

## Hosting

Web-enabled binaries read these runtime settings:

| Variable | Default | Purpose |
| --- | --- | --- |
| `LAPIS_WEB_PORT` | `8080` | Separate HTTP/WebSocket port, 1–65535 |
| `LAPIS_WEB_ROOT` | `webclient` | Trusted, read-only bundle directory |
| `LAPIS_WEB_ORIGIN` | Request's `http://Host` | Exact allowed browser origin |

For public hosting, terminate HTTPS at your existing reverse proxy and forward
`/` and `/ws` to the web port, preserving WebSocket upgrades. Set
`LAPIS_WEB_ORIGIN=https://your-host.example` (include a non-default port if used).
The browser chooses same-origin WSS automatically on HTTPS. Serve at the origin
root. No arbitrary TCP destinations, CORS or authentication bypass is provided.
Offline usernames and all game permission checks are the same as TCP clients.

The host supports eight simultaneous HTTP/WS connections, bounded headers,
masked binary frames and fragmentation, ping/pong, 64 KiB inbound streams and
4 MiB outbound queues per upgraded connection. Stalled peers expire; overflow
closes a connection. Static downloads are streamed. Never put secrets or symlinks
in the web root; it is exclusively for the validated public bundle.

## Desktop and touch

Desktop: WASD and mouse, Space jump, left click mine/hit, right click use/place,
B inventory, G drop, T chat, 1–9 hotbar, Ctrl sneak, Shift sprint. Flight where
permitted uses Z, Q and E. Click the canvas to capture the mouse; Esc releases it.

Touch: hold direction/action buttons and drag the world to look; several fingers
can be used together. Tap hotbar slots. In Bag, choose Stack, One / split or
Transfer before tapping an inventory slot. Close exits menus. Enter sends chat
or respawns. Flying controls still obey the server's mode/abilities.

The browser requires a tap to enter fullscreen or unlock sound. Orientation lock
is attempted when supported; otherwise rotate manually. If the browser cannot
fullscreen a page, open it as a home-screen app in landscape. Unsupported modes
stay at the gate; CSS viewport filling is not presented as real fullscreen.
The server continues simulation while the client is gated, so move to safety
before switching apps. This version does not cache the game for offline play.

## License and provenance

The browser package retains ClassiCube, bundled dependency, Emscripten and asset
notices. `asset-sources/manifest.json` records asset provenance; `release.json`
records source revisions and file hashes. The shell's cube icon is original CC0 SVG
with its source, hash and dedication recorded in the asset manifest. PCM sound synthesis is shared with the native
client; footsteps load the local CC0 replacement WAVs.

## Validation

See the web transport and browser acceptance tests in `tests/`. Browser emulation
is a regression check, not certification of every physical phone/browser.

The first web acceptance run passed on Chromium with software WebGL in CI:
real server login/world rendering, desktop keyboard controls, simultaneous touch
movement/jump, server inventory click requests, hotbar selection, chat, teleport
synchronization, portrait blocking and actual fullscreen exit/resume. Both clients
had no JavaScript exceptions and requested only same-origin resources. The run
also passed 4,022 host bounds/malformed-frame checks under ASan/UBSan and the
existing 10,733 client protocol/gameplay assertions. The byte-bridge integration
test separately passed mining, pickup, dropping and placement over WebSocket.
Physical Android/iOS devices, Safari, Firefox, sound listening and a full human
survival playthrough have not yet been validated.

To repeat the gates after building both the default binary at `lapis-obsidian`
and optional binary at `clients/lapiscube/build/web-server`:

```sh
make -C clients/lapiscube test web-test
python3 -m pip install playwright==1.51.0
python3 -m playwright install --with-deps chromium
python3 clients/lapiscube/tests/web_browser.py
```

`LAPIS_WEB_TEST_SERVER` can select a different optional server binary;
`LAPIS_BROWSER_EXECUTABLE` selects an installed Chromium executable. The default
binary gate deliberately sets the runtime flag and verifies there is no listener.
