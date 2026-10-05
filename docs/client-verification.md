# lapisclient milestone verification

The implementation is exercised with the real C server, not a mocked world or TCP
bridge. `npm test` builds the server and runs protocol tests covering:

- subprotocol/version negotiation and offline session startup;
- all 25 initial binary chunks, palette decoding and nonempty mesh geometry;
- two-player spawn and movement broadcasts, rejected teleport correction;
- hotbar selection, creative inventory, cursor pickup/place, block break/place;
- UTF-8 chat, disconnect removal, reconnect and duplicate identity rejection;
- optional shared token, wrong/missing origin and wrong path/subprotocol;
- malformed JSON, duplicate keys, fragmented messages, binary client input,
  oversized messages, invalid field ranges and invalid session transitions.

`npm run test:web` adds desktop/touch Chromium integration. It verifies actual
rendered vertices, pointer lock, WASD/look/jump, mining, hotbar/inventory, chat,
multitouch move/look/jump/use, responsive layout and reconnect. GitHub's Client
checks workflow installs Chromium and runs this test on Ubuntu.

Local protocol/meshing tests, the real Game/worker integration test and native
server build passed during implementation. All three real-server tests also
passed with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
The desktop and touch Chromium suite **passed** on 2026-10-05 in
[Client checks run 37355442344](https://github.com/aksulightning/lapisobsidian/actions/runs/37355442344),
including WebGL world rendering, controls, chat, resize and reconnect with no
browser errors. The run also passed real-server tests and the production build;
its artifacts include screenshots, browser-results.json and the static client.
Native server build, codec tests and C regression/sanitizer checks passed in
[server CI](https://github.com/aksulightning/lapisobsidian/actions/runs/37355129463).

The branches are published in draft PRs #2 (`testing`) and #3 (`testing-client`).
The local Chromium executable exits before page creation, so browser verification
used GitHub's Ubuntu runner. Touch emulation is not physical device verification.

The existing C regression suite and new `tests/lapisclient-codec.sh` cover the
shared gameplay and strict JSON decoder. Production WSS certificate configuration,
physical iOS/Android devices and native Windows/ESP WebSocket builds require
separate deployment/platform testing.
