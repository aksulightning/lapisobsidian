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
records integration changes to Server, input, screens, font/skin paths, branding
and the entrypoint; no upstream copyright header is removed. Builds apply those changes only to a staged copy.

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

`src/LapisFacts.h` is generated numeric interoperability data: state/item IDs and
identifier names from the pinned server registry snapshot, paired with original
CC0 atlas mappings. No registry implementation, NBT pack, Minecraft textures or
models are copied. Regeneration reads `generated/registry_snapshot.json`,
`src/registries.c` and `include/registries.h`; see the server registry provenance
document for upstream factual data sources. The client does not link server code.

## Assets

The initial asset collection uses Kenney Vleugels' verified CC0 Voxel Pack and
original geometric UI/model designs and synthesized effects dedicated under CC0.
`assets/manifest.json` records hashes and per-file provenance; exact notices are
in `assets/licenses`. Original audiovisual designs/output are CC0, independently
of the BSD license of code that creates or renders them.

The launcher entrypoint displays usage, and Lapis skin requests are disabled.
The package script ships this notice, client license, full engine license and
credits, third-party license files, asset sources and license evidence. Existing
user-chosen texture packs are not redistributed. See `docs/assets.md` for coverage
limits and `docs/validation.md` for tested builds. This experimental package is
not a declaration that final release/provenance review is complete.
