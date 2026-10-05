# Lapis Obsidian Client verification

Reference server: unchanged `testing` e96af88797b0718851ae8422415467cb94253982.

Checks performed during replacement:

- Clean npm dependency installation and HTML5 registry/module build.
- Actual C reference server compilation and full original server test suite.
- Portable Linux build and packaged runtime startup/authentication/shutdown.
- CI portable builds passed on Ubuntu, Windows and macOS.
- Live TCP login, configuration, 25 received/decoded initial chunks, movement,
  input flags, hotbar, chat echo, creative slots, mining, placement, inventory
  pickup and same-session reconnect.
- Split/coalesced frame integrity and invalid VarInt/frame rejection.
- Bridge random ports/tokens, missing/bad authentication, foreign Origin/Host,
  malformed controls, target validation, protocol admission and binary integrity.
- Connection refusal with surviving local session and subsequent PING.
- Launcher automatic bridge startup/authentication, last-tab shutdown, remote
  TCP close, WebSocket close, listener close and released port.

Local browser execution was attempted with Playwright and two Chromium builds.
The standard browser download failed and locally extracted binaries exited
with SIGTRAP before a page could open. Local execution therefore could not verify
WebGL, pointer lock, desktop input or multitouch. The same checks subsequently **passed in GitHub Actions** on Ubuntu with
Chromium, against the actual C server: live chunk rendering, pointer lock,
keyboard movement, mouse look, hotbar/inventory, chat, simultaneous touch
movement/look/jump, portrait layout, resize and reconnect.

[Verified client CI run](https://github.com/aksulightning/lapisobsidian/actions/runs/37301710117)
also passed packaged runtime startup/authentication/shutdown on Linux, Windows
and macOS. Screenshots and browser-result JSON are attached to that run. No physical phone, Android/iOS package or signed installer was
validated here.

Additional build/CI results are recorded in the replacement commit’s checks.
