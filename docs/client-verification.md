# Lapis Obsidian Client verification

Reference server: unchanged `testing` e96af88797b0718851ae8422415467cb94253982.

Checks performed during replacement:

- Clean npm dependency installation and HTML5 registry/module build.
- Actual C reference server compilation.
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
with SIGTRAP before a page could open. This environment therefore did not verify
WebGL, pointer lock, desktop input or actual multitouch. The repository includes
`npm run test:web` and CI to run those checks; do not treat their presence as a
passing result. No physical phone, Android/iOS package or signed installer was
validated here.

Additional build/CI results are recorded in the replacement commit’s checks.
