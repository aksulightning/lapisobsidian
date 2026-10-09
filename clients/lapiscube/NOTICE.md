# Code and asset notices

LapisCube is an independent project, not an official ClassiCube, Mojang or
Microsoft release. Names used for protocol interoperability do not imply
endorsement. No Mojang/Microsoft textures, sounds, models, JARs or client runtime
components are included in this change.

## Original code

`src/`, `tools/`, `tests/`, documentation and build integration in this directory
are original LapisCube work under the accompanying BSD-3-Clause `LICENSE`.
Protocol identifiers and wire behavior were studied from the Lapis testing
source; the client does not link or copy Lapis server implementation code.
The test server remains a separate executable.

## Modified upstream code

`engine/` is the unmodified ClassiCube Git submodule at
`d41c3f7eef2038f59702b58bdb373483fb0d28f9` from
https://github.com/ClassiCube/ClassiCube. Its `license.txt`, `credits.txt`, source
headers and third-party licenses are retained. `patches/engine.patch` explicitly
records the changes to `src/Server.c` and `src/main_impl.h`; no upstream copyright
header is removed. Builds apply those changes only to a staged copy.

ClassiCube's principal source license is BSD-3-Clause. Source redistribution
must preserve its notices, conditions and disclaimer. Binary redistribution
must reproduce them in documentation/materials. Its name and contributors'
names cannot be used to endorse derived products without permission.

Do not treat this summary as a replacement for `engine/license.txt`. That file
also records OpenTK/Mono, Emscripten, BearSSL and FreeType terms, algorithm
provenance and other notices. In particular it notes that the cited ray-box
algorithm source did not appear to have an attached license; retain that note
and resolve provenance before the final distribution review. Other embedded
platform dependencies retain their own notices in `engine/third_party`.

The offline UUID helper calls ClassiCube's existing BearSSL MD5 implementation,
copyright Thomas Pornin, under the retained MIT license. MD5 is used only for
the protocol's UUIDv3 identity derivation, not for security.

ClassiCube's contribution policy does not accept AI-generated pull requests.
This work is maintained in the requested Lapis branch, not submitted upstream.

## Server and compatibility data

The repository's Lapis Obsidian server remains under its existing root GPLv3
license and notices; it has not been relicensed. See root `LICENSE`, `NOTICE.md`
and `docs/registries.md` for its provenance. The native client is a separate
program communicating over TCP; its build does not link server source.

No copied registry snapshot, textures or models from minecraft-data/mcmeta are
bundled into the client in M1. Numeric packet IDs and identifier strings are
used for interoperability. Any future imported tables must retain their specific
source revision/license/provenance separately from original code.

## Assets

M1 contains no gameplay texture/model/audio bundle. `assets/manifest.json` has
an empty `assets` array intentionally; it is not a claim of a completed CC0 pack.
The upstream launcher entrypoint is replaced by usage text so LapisCube does not
offer its proprietary-asset download workflow. Existing user-chosen local
texture packs are not redistributed by this project.

See `docs/assets.md` for investigated candidate sources and admission rules.
Before distributing binaries, ship this notice, this directory's license,
the complete engine license/credits and licenses for every packaged asset or
additional linked component. No distributable binary package is claimed in M1.
