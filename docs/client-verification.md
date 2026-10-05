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
The local Chromium executable exits with SIGTRAP before page creation in the
restricted work environment; local rendering/control results must not be inferred
from protocol tests. Remote CI has not run: automatic approval review blocked pushing the commits.
The milestone cannot be called fully verified until those browser tests pass.

The existing C regression suite and new `tests/lapisclient-codec.sh` cover the
shared gameplay and strict JSON decoder. Production WSS certificate configuration,
physical iOS/Android devices and native Windows/ESP WebSocket builds require
separate deployment/platform testing.
