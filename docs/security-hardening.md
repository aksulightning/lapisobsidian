# Inventory and inbound packet hardening

This change addresses additional weaknesses found while reviewing the public
bareiron vulnerability reports against Lapis Obsidian. It does not assign new
CVE identifiers or certify the whole server as secure.

## Server-owned inventory

Click Container predictions are bounded and parsed for framing, but never supply
items or counts to the inventory. Left/right clicks, shift transfers, hotbar and
offhand swaps, drag distribution and double-click collection operate on existing
server stacks. Crafting derives the recipe result, verifies ingredients and
capacity, and consumes the ingredients before granting output. Normal and shift
crafting also work for the server's custom grass-to-seed recipe.

Only the currently opened window accepts clicks or close requests. Opening a
container returns the previous cursor/grid contents first. Chest storage pointers
are checked against live chest allocations before access and are never treated
as crafting ingredients. Container opening is limited to six blocks per axis.
Furnace output stops at its stack limit, and furnace item access avoids unaligned
pointers into the packed save structure. Held-slot packets validate the entire
16-bit value against 0..8 before conversion.

Session metadata is a fixed array separate from the packed player save format.
There is no save migration, dynamic inventory allocation or extra dependency.
Existing gameplay simplifications remain: no recipe book, no creative middle-click
cloning through Click Container, and simplified furnace shift-transfer placement.

## Bounded inbound frames

`packet_input.c` polls nonblocking sockets for a complete packet before dispatch.
It uses one shared 8 KiB frame buffer and a small deadline record per connection.
A frame length must fit three VarInt bytes and be between 1 and 8192 bytes.
Incomplete frames expire 15 seconds after the first byte is observed; subsequent
bytes do not extend that deadline. A partial sender cannot hold the dispatcher
inside a receive loop.

While dispatch is active, `recv_all` reads exclusively from that frame. An
attempt to read beyond it sets a sticky failure and disconnects the peer after
dispatch. Bytes from the following frame cannot satisfy a truncated field.
Movement, held-slot and close-window handlers additionally enforce exact lengths.
The previously optional unauthenticated raw world dump/upload path was removed.

The 8192-byte limit covers the implemented vanilla-client input subset. Large
custom/plugin payloads are unsupported. This does not add compression, proxy
support or a general asynchronous networking framework.

## Verification

```sh
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

`tests/security.c` covers forged slot/cursor predictions, stack conservation,
normal/shift crafting, window spoofing, hostile chest pointers, close/disconnect
cleanup, furnace capacity, full-width hotbar bounds, malformed frame lengths,
frame isolation, progress for a second peer and absolute receive deadlines.
The existing item, recipe, command, sign, circuit, mob and world tests also run.
Tests use local socket pairs and deterministic clocks; no external server,
Minecraft JAR or Java runtime is required. A live TCP smoke check also confirmed
that three status clients receive replies while another connection withholds its
packet header. ASan and UBSan are enabled in sanitizer tests; LeakSanitizer is
disabled by the existing sandbox test configuration.

## Remaining limits

Outbound `send_all` still waits for socket writability and can stall the tick loop
when a peer stops reading; a bounded outbound queue is a separate follow-up.
Connections that have sent no first byte are not covered by the frame deadline.
The inherited offline login does not authenticate UUIDs and transport remains
unencrypted. Administrator access still requires the configured token and should
be used over a trusted network or protected tunnel. Frame isolation does not
replace semantic validation inside every handler or a comprehensive audit.
