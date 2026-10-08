# Alternative client third-party code

This mode is an actual adaptation, not merely architectural inspiration.
`client2/provenance.json` is the exact per-file inventory, including local path,
source URL, pinned commit, author, license, original/current SHA-256 hashes and
modification status. No upstream copyright/license notice is removed.

| Component | Source/version | License and retained files |
| --- | --- | --- |
| LabyStudio/js-minecraft, LabyStudio and contributors | https://github.com/LabyStudio/js-minecraft, commit `468f942c4984578a03e472c49646198680489723` | CC BY-NC 4.0; reachable `client2/src/js/**/*.js` and adapted `client2/style.css`; full license in `client2/LICENSE`, attribution/change notice in `client2/NOTICE.md` |
| Three.js, Three.js Authors | Same upstream snapshot, bundled revision string `141dev`; exact bytes/hash in manifest (not claimed to be a tagged release) | MIT; `client2/libraries/three.module.js`, unchanged; header and full notice `licenses/THREE-MIT.txt` preserved |
| long.js, Closure Library Authors; Daniel Wirtz / long.js Authors | Same pinned snapshot; bundled file has no reliable version tag, exact bytes/hash in manifest | Apache-2.0; `client2/libraries/long.js`, unchanged; header and `licenses/Apache-2.0.txt` preserved |

Other upstream libraries (chat/pako/AES/RSA/SHA-1) and legacy proxy/network
handlers are excluded by reachable-import selection. No upstream media or
screenshots are copied. The source README is consulted but not distributed.

Modified files in the machine-readable inventory cover bootstrap, branding,
Direct Connect, Connecting, multiplayer actions/loading/readiness/texture cache,
cached tinted glyph sheets replacing per-glyph Canvas filters, health/food overlay, 320-block chunks/world, world timer cleanup, independent logo layout, removal of costly backdrop blur, sound buffer
synthesis and survival flight restrictions. The original renderer, mesher,
models, GUI widgets, player controller and movement/collision code otherwise
remain upstream code. Fresh MIT adapter modules translate the existing server
wire format; factual registry mappings come from the server's checked-in data.

The upstream README credits porting from LabyStudio/java-minecraft; preserve that
historical acknowledgement here. This does not grant rights in official assets.
Distribution must retain license texts, attribution, source reference and
modification records. The CC BY-NC restriction applies to upstream code and its
adaptation; commercial permission cannot be granted by the GPL server license.
