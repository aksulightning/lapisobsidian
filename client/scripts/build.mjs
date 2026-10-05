// SPDX-License-Identifier: GPL-3.0-only
import { readFile, writeFile, cp, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
process.chdir(fileURLToPath(new URL("../", import.meta.url)));
const snapshot = JSON.parse(await readFile("../generated/registry_snapshot.json", "utf8"));
await writeFile("web/protocol/registry.js", `// Render and item registry shared with lapisclient v1.\nexport const registry = ${JSON.stringify(snapshot)};\n`);
await rm("dist", { recursive: true, force: true });
await cp("web", "dist", { recursive: true });
await cp("../LICENSE", "dist/LICENSE");
console.log("Built Lapis Obsidian Client in dist/. Serve these static files over HTTP(S).");
