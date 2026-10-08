# Alternative client license boundary

The C server and existing mode 1 retain the repository's GPL v3 licensing and
notices. LabyStudio/js-minecraft code is **CC BY-NC 4.0**, which imposes a
noncommercial restriction and cannot be relicensed as GPL. This feature does
not claim that those licenses are compatible for one combined/linked program.

Instead `client2` is a separate browser application with its own license,
notices, independent build and protocol boundary. It is not linked into, imported
by or embedded in the GPL executable or GPL mode 1 browser modules. The server
serves it like an independent static application; the compile-time whitelist
contains paths/MIME metadata only. The root GPL license does not override
`client2/LICENSE`. Distributions aggregating both programs must keep their
licenses separate. Noncommercial CC BY-NC distribution/use is supported;
commercial use of this client requires separate upstream permission.

Three.js remains MIT and long.js Apache-2.0; their headers and full texts are
preserved. Fresh adapter/build/test source is MIT and may be used in the NC
application; upstream modifications remain an attributed CC BY-NC adaptation.
Original artwork and synthesis output are dedicated under CC0-1.0; no upstream
asset is asserted to be CC0, and the application as a whole is not CC0. Installed
system font rendering does not distribute a font file or change its license.

Before adding code, record author, exact version/commit, source URL, license,
modified files and obligations in `provenance.json` and the code documentation.
Update recorded current hashes only after reviewing changes. Before adding
media, individually verify ownership and a compatible license; unclear rights
mean rejection. This implementation does not authorize obtaining proprietary
game resources or adding another asset repository. Retain documentary evidence.

Run `node client2/scripts/audit.mjs` and the provenance test. They detect imported
media, missing records/hash mismatches, prohibited resource endpoints/loaders
and inherited presentation strings. Manual review of the actual generator and
asset provenance remains necessary; text matching cannot establish copyright
clearance. Browser tests assert no external or inherited resource requests.
