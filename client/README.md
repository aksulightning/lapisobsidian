# Lapis Obsidian Client — HTML5 / Standalone

Milestone 1 development preview, based on `testing` commit
`e96af88`. New code is GPL-3.0-only; the repository license and upstream notices
are preserved. No proprietary game assets are used.

## What works

- Tauri 2 application shell with an embedded Rust bridge and frontend assets.
- HTML5/TypeScript/CSS UI with desktop, phone, tablet and landscape layouts.
- WebGL test island, fog, resize/DPR handling, frame limit and quality presets.
- Keyboard/mouse free-camera exploration and independent multi-touch movement,
  look and elevation controls. This is a test camera, not a simulated player.
- Saved server/player preferences, graphics quality, frame limit, sensitivities,
  UI scale, and a render-distance preference reserved for world integration.
  The native shell reads a bounded, typed `preferences.json` in its application
  configuration directory and saves it on clean exit, independent of the random
  bridge port. Concurrent instances use last-clean-exit wins; crash recovery of
  unsaved preferences is not implemented. Browser-development preferences last
  for that launch/origin.
- Automatic loopback binding on an OS-selected port, 256-bit per-launch token,
  exact origin/Host checks, bounded control messages and managed shutdown.
- Optional compile-time TCP echo harness; production builds cannot dial arbitrary
  destinations. Remote Connect is visibly unavailable, rather than simulated.

**No remote login, live world, chunk decoder, collision physics, entities,
block actions, hotbar, inventory, chat, sound or gamepad support yet.**

## Architecture

The existing C code is a server, not a native client. It remains unchanged.
The application consists of a small TypeScript frontend, `lapis-bridge` Rust
library, and Tauri desktop launcher. The bridge runs inside the application's
process; users never launch a helper or configure a WebSocket URL.

The runtime first binds `127.0.0.1:0`, generates a fresh random token, and then
loads the embedded frontend from that selected HTTP origin. It injects the
bootstrap into that WebView before frontend code runs. The frontend deletes the
bootstrap global, authenticates via WebSocket, and clears its token reference.
Credentials do not appear in bundled files, URLs, logs or saved preferences.
Closing the application cancels sessions and listeners and releases the port.

The intended transport is frontend → local WebSocket → bundled bridge → remote
TCP endpoint. **Milestone 1 only enables authenticated controls in normal builds.**
The TCP echo feature tests both binary directions against one internally created
loopback target. Milestone 2 will enable protocol-constrained remote transport.
See [architecture](../docs/client-architecture.md) and
[security/protocol specification](../docs/client-bridge.md).

## Dependencies

Install Node.js 22.12+ (or a supported newer LTS), npm, a current stable Rust
compiler/Cargo, and the [Tauri 2 platform prerequisites](https://v2.tauri.app/start/prerequisites/).
The lockfiles pin the resolved dependency graph. On Debian/Ubuntu, Tauri needs
WebKitGTK 4.1 and GTK development packages, a C/C++ toolchain and pkg-config.
Windows needs the MSVC build tools and WebView2. macOS needs Xcode command-line
tools. Tauri uses system WebViews instead of bundling Chromium. No Java, game
installation or external game assets are needed to build this preview.

```sh
cd client
npm ci
npm run dev
```

`dev` builds the frontend and starts the native shell with its own bridge.
Restart after frontend edits: hot reload is intentionally not enabled, to keep
the exact-origin policy and embedded-asset path identical to production.

For a browser development window on the **same machine**:

```sh
npx playwright install chromium
npm run dev:browser
# Optional, after installing the corresponding Playwright browser:
BROWSER_ENGINE=firefox npm run dev:browser
BROWSER_ENGINE=webkit npm run dev:browser
```

This developer launcher also manages the bridge automatically. It uses a private
pipe and pre-document injection rather than printing a credential-bearing URL.
Playwright is a development dependency only, never a packaged runtime dependency.
Firefox/WebKit are development options requiring separate validation. An ordinary
static hosting preview can render the scene but cannot connect to a remote TCP
server or authenticate itself with a standalone instance.

## Builds and tests

Run these from `client/`:

```sh
npm run build                     # TypeScript check + static HTML5 assets in dist/
npm run build:bridge              # release headless verification executable
npm test                          # normal boundary tests + restricted TCP echo
npm run test:web                   # Chromium desktop/touch smoke checks
npm run build:standalone           # native application and platform bundles
```

The frontend must be built before a direct Cargo command because the bridge
embeds `dist/` at compile time. If rebuilding only assets, run Cargo again; the
bridge build script tracks the embedded asset directory.

`cargo build --locked -p lapis-obsidian-client --release` compiles the native
application without creating installers. `npm run build:standalone` runs the
frontend build and Tauri packaging. Build on each target OS; do not assume a
Linux build produces Windows/macOS installers. Outputs are under
`target/release/` and `target/release/bundle/`. Code signing and notarization
require maintainer credentials; no signed release is claimed by this milestone.
Use the `Client checks` workflow for platform checks and draft build artifacts.

The `lapis-bridge --bootstrap-stdio` binary is a **developer harness**, not the
user application. It requires a private stdout pipe, exits on stdin close or
Ctrl-C, and refuses to write credentials to a terminal. Do not redirect its
bootstrap output into logs. The normal app has no sidecar, process-spawning API,
arbitrary filesystem API, or bridge command for running shell commands.

Run the existing repository gates from the repository root:

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
node build_registries.js --check
```

## Controls

| Context | Controls |
| --- | --- |
| Desktop test scene | Click Explore; WASD moves, mouse looks under pointer lock; Space raises camera, Shift lowers it; Escape or Menu returns to the menu. |
| Touch test scene | Left joystick moves, right region looks, ↑ raises camera. Each region tracks its own pointer so they work simultaneously. Menu exits. |
| Settings | Graphics quality, FPS cap, mouse/touch sensitivity, UI scale, fullscreen toggle. Antialiasing changes apply after restart. |
| Endpoint settings | Save host, TCP port and a 1–15 character ASCII player name. This does not connect or authenticate a player. |

Touch controls respect display safe areas and phone/tablet layout changes.
Rotate to landscape for more scene space; orientation is not forcibly locked.
Keyboard/touch input feeds an abstract input frame, separate from the renderer.
Gameplay bindings, jump/primary/use actions, hotbar, inventory and chat are later
milestones. All current controls are labelled for their actual test-scene action.

## Compatibility and limitations

The source-of-truth server is protocol **772 / Java 1.21.8**, from
`include/protocol.h`. The build derives the frontend compatibility label from
that header. This is a planned target, not a claim of client interoperability.
The server currently uses unencrypted offline login. Its configuration exchange
assumes a matching built-in core registry pack and omits some registry NBT;
client integration must supply legally redistributable semantic data or an
explicit compatible server extension, not pretend it owns that asset pack.

The test renderer uses WebGL 1 for broad compatibility. Recent Chromium,
WebView2, WebKitGTK and WKWebView are intended environments. Pointer lock and
fullscreen depend on runtime support and fail with understandable messages.
A lost graphics context reports an error and requires restarting this preview.
No production frame-rate or mobile battery claim has been established.

Android/iOS packaging is **not implemented or validated**. Tauri's mobile support
makes it a candidate, but SDK builds, local cleartext WebSocket policies, token
injection behavior, lifecycle suspension, safe areas, WebView capabilities and
on-device testing need separate work. iOS signing and distribution restrictions
must be evaluated. Desktop bundles do not automatically work on phones.
The same TypeScript frontend and input model are intended to remain shared.

## Contributing

Base client changes on `testing-client`; preserve the server's source-only build.
Keep product labels as **Lapis Obsidian Client**. Avoid proprietary assets.
Add protocol fixtures from the inspected C implementation before adding client
packet handlers. Do not duplicate or replace the server packet implementation
without a reviewed transport-neutral extraction plan. Keep bridge policy, wire
codec, renderer and input separate. Run the existing C checks and client checks.
Do not enable remote dialing without the Milestone 2 security gates described in
`docs/client-bridge.md`. Include source and dependency notices with distributed
binaries under [LICENSE](../LICENSE) and [NOTICE.md](../NOTICE.md).
