# Experimental persistent drops and authentication

Both features are compile-time opt-ins. Setting an environment variable when
starting an already-built server cannot enable either feature.

```sh
# Persistent item drops; existing offline login.
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 ./build.sh

# Authenticated Java multiplayer; transient item drops.
LAPIS_ENABLE_EXPERIMENTAL_AUTH=1 ./build.sh

# Both features.
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 \
LAPIS_ENABLE_EXPERIMENTAL_AUTH=1 ./build.sh
```

Unset variables and `0` preserve the corresponding default behavior. Other
values are rejected. The same variables work with `build-alpine.sh` and are
inherited by `build_clientsrv.sh`. Builds without authentication need no new
external library. Both experimental features reject the inherited `--9x`
target; ESP-IDF is not supported by these opt-ins.

## Drop behavior

Mining, harvesting, mob loot, shearing and inventory drops use the existing
bounded item-entity system. There are still at most 128 active drops per world
or Plate. Stack merging, gravity, pickup eligibility, pickup delays and partial
pickup retain their existing rules.

Lava is the only environmental damage source that removes drops. Cactus does
not destroy them. There is no fire simulation in the current server, and no
fire-destruction path has been introduced. Drops falling below the world stop
at Y=0 instead of being deleted. These rules also apply to transient builds.

The five-minute despawn timer is a separate lifetime rule. Saved age and pickup
delay survive restart. Time spent with the server stopped or the Plate unloaded
does not count. A restart cannot reset a drop to a new five-minute lifetime.

With persistence enabled, each world has `drops.bin`; Plates use
`plates/<name>/drops.bin`. Records contain stable 64-bit IDs, item/count,
position, vertical velocity, elapsed lifetime and remaining pickup delay.
Network entity IDs and visibility masks are rebuilt, not stored. Files have
an explicit version, protocol/limit checks, fixed-width little-endian fields
and CRC32. Corrupt, oversized or unsupported files stop loading with an error.
The existing `world.bin` and packed player layout are unchanged.

Complete pickup moves the stack to the player's inventory and removes its
registry record. Partial pickup records only the remaining quantity. A full
inventory leaves the drop in place. Merging, lava destruction and expiration
also update the registry.

## Recovery and operational cost

Updating `drops.bin` independently of inventory or mined blocks would create
duplication/loss windows. Persistent builds therefore wrap gameplay actions
and simulation updates in bounded undo-journal transactions:

1. Durably save the previous affected files in `drops.journal`.
2. Apply the action, buffering its outgoing client updates.
3. Save world edits, players, drops and the affected world's other sidecars.
4. Sync the files, durably remove the journal, then deliver the client updates.

On restart, an interrupted transaction is restored before reading world or
player data. Repeating recovery is safe. Plate recovery occurs before loading
the shared `plates/players.bin`. A failed save stops the server and retains
recovery information; it does not acknowledge an unsaved item transfer.

The journal is bounded to seven files of at most 1 MiB each (normal saves are
much smaller). The output spool is bounded to 16 MiB. This first experimental
implementation favors recovery correctness over disk throughput: it copies
the affected save files synchronously and flushes them frequently. Use local
storage with reliable atomic replacement/fsync semantics. High player counts,
slow disks and network filesystems have not been performance-qualified.

Back up the entire save directory together, preferably while stopped. Preserve
an existing journal after a failed save. Do not edit or remove it to get past a
recovery error. A corrupt journal is rejected before any restoration starts.

A build without persistent drops refuses a world containing `drops.bin` or an
unrecovered journal, even if the registry is empty. This prevents spending
inventory in transient mode and later resurrecting a stale saved drop. To
return deliberately to transient mode, first stop a working persistent build,
back up the whole save, and archive each `drops.bin` outside the world directory.
Any items still on the ground are then deliberately discarded. Recover any
pending transaction before doing this.

## Online-mode authentication

Authentication-enabled builds require OpenSSL 3.x, libcurl 7.85 or newer with
TLS and asynchronous DNS support, and a working CA certificate store.

```sh
# Debian/Ubuntu prerequisites
sudo apt-get install build-essential libssl-dev libcurl4-openssl-dev ca-certificates

# Alpine prerequisites (dynamic authentication build)
apk add build-base openssl-dev curl-dev ca-certificates
LAPIS_ENABLE_EXPERIMENTAL_AUTH=1 ./build-alpine.sh
```

The Alpine `--static` authentication combination is explicitly unsupported in
this first version. Cross builds need target-compatible libraries. Default
and drops-only static Alpine builds remain available. Native Windows builds
need corresponding MinGW OpenSSL/libcurl development libraries and CA setup;
live Windows authentication acceptance remains a separate check.

The supported client is Minecraft Java 1.21.8, protocol 772. The server sends a
fresh cryptographic challenge, validates the RSA response, enables continuous
AES-128-CFB8 encryption, and checks the connecting session through Mojang's
HTTPS `session/minecraft/hasJoined` service. Only the verified profile UUID and
name can be used to reserve player data and send Login Success. A username
lookup, client-supplied UUID, or claim to own the game cannot bypass this check.
The server does not ask for or store a player's Microsoft password/access token.

Certificate and hostname validation remain enabled; redirects are disabled.
Session requests use libcurl's nonblocking multi API, a 5-second connection
timeout, a 10-second total request timeout and a 16 KiB response limit. The
pre-play connection deadline is 30 seconds. Malformed/out-of-order packets,
replayed challenges, malformed profiles, duplicate active UUIDs, HTTP errors,
TLS failures and timeouts deny entry. There is no automatic offline fallback
or production session-endpoint override. The existing 8 KiB inbound frame and
15-second incomplete-frame limits also apply to encrypted connections.

Authenticated UUIDs may differ from old offline identities. The server does
not transfer inventories by username: this could give another identity access
to an existing player's items. Back up first and treat any identity migration
as a separate, administrator-reviewed operation. Full 16-character names now
work without changing the packed player save layout.

LapisCube currently supports the offline protocol profile only. It cannot join
an authentication-enabled server, on native TCP or browser WebSocket. The web
transport has no authentication bypass. Adding Microsoft sign-in and online
session support to LapisCube is separate work.

## Standalone browser singleplayer

```sh
LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1 make -C clients/lapiscube singleplayer
```

Singleplayer uses its private local stream and stays offline. Combining it with
`LAPIS_ENABLE_EXPERIMENTAL_AUTH=1` is rejected at build time. Drop files are
included in the existing IDBFS/IndexedDB save, Save world and Save & quit flows.
Browser durability is the last completed `FS.syncfs` operation; filesystem
journaling in WebAssembly does not make unsaved browser memory durable. Keep
using Save & quit before closing and retain the same browser profile/origin.

## Verification

Run the usual build, tests and sanitizers with either/both flags exported:

```sh
export LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM=1
export LAPIS_ENABLE_EXPERIMENTAL_AUTH=1
./build.sh
./tests/run.sh
SANITIZE=1 ./tests/run.sh
```

CI covers all four flag combinations and both singleplayer persistence modes.
Drop tests fork and stop between source/registry writes, replay recovery,
check delayed client updates, verify partial pickup and Plate isolation, and
reject corrupt files. Authentication tests use real RSA/AES operations and
fragmented encrypted frames; only the session-service response is a test
fixture. That fixture is compiled out of production builds. The browser test
mines through the real protocol and checks saved drop identity/count/age after
an offline reload.

A real licensed Java account remains a manual acceptance gate: join with a
signed-in Java 1.21.8 client, confirm reconnect/inventory continuity, confirm an
offline client cannot join, and confirm failed verification never admits a
player. Mocked service responses do not establish live account acceptance.

Protocol/reference implementations: [PrismarineJS login](https://github.com/PrismarineJS/node-minecraft-protocol/blob/master/src/server/login.js)
and [session verification](https://github.com/PrismarineJS/node-yggdrasil/blob/master/src/Server.js).
Crypto/TLS API references: [OpenSSL EVP](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/)
and [libcurl peer verification](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html).
