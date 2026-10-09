# Client code provenance

## Shipped client

The browser ES modules, WebGL shaders, mesher, collision, packet codec, input,
UI, synthesis and embedded WebSocket implementation are independently written
for Lapis Obsidian. No code, shaders, graphics, sounds or dependencies were copied
or vendored from LabyStudio/js-minecraft. No Three.js or other rendering library
is bundled: the client uses standard WebGL and browser APIs directly.

Architectural reference only:
[LabyStudio/js-minecraft](https://github.com/LabyStudio/js-minecraft), commit
`468f942c4984578a03e472c49646198680489723`, author LabyStudio. Its LICENSE declares
CC BY-NC 4.0. Its README feature overview informed the checklist (chunk meshes,
first-person controls, inventory, multiplayer, lighting and menus). Retained
files: **none**. Its noncommercial code/assets have not been relicensed or imported.

## Existing server and compatibility data

The client invokes the existing bareiron-derived GPL server handlers; their
upstream attribution remains in root `NOTICE.md`. The compile-time client catalog
contains only identifiers/IDs already retained in `include/registries.h` and
`src/registries.c`; `tools/client-catalog.sh` is original extraction code.

Existing retained protocol sources, authors, pinned commits, selected files and
notices are recorded in [registries](registries.md), [signs](signs.md),
[doors](doors.md) and root `NOTICE.md`: bareiron, PrismarineJS/minecraft-data and
misode/mcmeta. This change adds no external protocol data or proprietary assets.
Numeric identifiers are not used as texture filenames or asset-download requests.

## Optional test dependency

Playwright, Microsoft and contributors, https://github.com/microsoft/playwright,
version **1.51.1**, Apache-2.0. It is installed unmodified into ignored
`.tests/browser/node_modules/playwright` solely to automate Chromium integration
and mobile tests. No files are retained in the repository or shipped in client
builds. Installation retains package LICENSE/NOTICE files; preserve those and
Apache attribution if redistributing test tooling. Browser downloads are test
executables, not game assets. Node's standard-library tools require no npm package.

Assets are separately inventoried in [client-asset-sources](client-asset-sources.md).

The separately licensed mode 2 client is documented in [client2-third-party-code.md](client2-third-party-code.md). Mode 1 behavior remains unchanged.
