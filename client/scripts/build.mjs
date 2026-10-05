// SPDX-License-Identifier: GPL-3.0-only
import {
  readFile,
  writeFile,
  mkdir,
  cp,
  rm,
  chmod,
  access,
} from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
const root = fileURLToPath(new URL("../", import.meta.url));
process.chdir(root);
const snapshot = JSON.parse(
  await readFile("../generated/registry_snapshot.json", "utf8"),
);
const header = await readFile("../include/protocol.h", "utf8");
if (!header.includes(`LAPIS_PROTOCOL_VERSION ${snapshot.protocol}`))
  throw Error("Registry/protocol mismatch");
await writeFile(
  "web/protocol/registry.js",
  `// Generated from testing's redistributable semantic registry snapshot. Do not hand edit.\nexport const registry = ${JSON.stringify(snapshot)};\n`,
);
await rm("dist", { recursive: true, force: true });
await cp("web", "dist", { recursive: true });
await cp("../LICENSE", "dist/LICENSE");
console.log("HTML5 modules and registry built. No WASM compilation is needed.");
if (!process.argv.includes("--web-only")) {
  const out = `release/lapis-obsidian-client-${process.platform}-${process.arch}`;
  await rm(out, { recursive: true, force: true });
  await mkdir(out, { recursive: true });
  for (const dir of ["runtime", "dist", "web/protocol"])
    await cp(dir, path.join(out, dir), { recursive: true });
  await cp("node_modules/ws", path.join(out, "node_modules/ws"), {
    recursive: true,
  });
  await cp("package.json", path.join(out, "package.json"));
  for (const f of ["LICENSE", "NOTICE.md"])
    await cp("../" + f, path.join(out, f));
  await cp("README.md", path.join(out, "README.md"));
  await cp("runtime/NODE-LICENSE", path.join(out, "NODE-LICENSE"));
  const exe = process.platform === "win32" ? "node.exe" : "node";
  let executable = process.execPath;
  if (!path.isAbsolute(executable)) {
    executable = null;
    for (const dir of process.env.PATH.split(path.delimiter)) {
      const candidate = path.join(dir, exe);
      try {
        await access(candidate);
        executable = candidate;
        break;
      } catch {}
    }
    if (!executable)
      throw Error("Cannot locate Node executable for standalone packaging");
  }
  await cp(executable, path.join(out, exe));
  await chmod(path.join(out, exe), 0o755);
  await writeFile(
    path.join(out, "Lapis-Obsidian-Client.sh"),
    '#!/bin/sh\ncd "$(dirname "$0")" || exit 1\nexec ./node runtime/launcher.mjs "$@"\n',
  );
  await chmod(path.join(out, "Lapis-Obsidian-Client.sh"), 0o755);
  await writeFile(
    path.join(out, "Lapis-Obsidian-Client.cmd"),
    '@echo off\r\ncd /d "%~dp0"\r\nnode.exe runtime\\launcher.mjs %*\r\n',
  );
  // Include corresponding project source, excluding dependencies, outputs, and test worlds.
  await mkdir(path.join(out, "source/client"), { recursive: true });
  for (const d of ["web", "runtime", "scripts", "tests"])
    await cp(d, path.join(out, "source/client", d), { recursive: true });
  for (const f of ["package.json", "package-lock.json", "README.md"])
    await cp(f, path.join(out, "source/client", f));
  for (const d of ["include", "src", "generated", "docs", "tests"])
    await cp("../" + d, path.join(out, "source", d), { recursive: true });
  for (const f of ["build.sh", "build_registries.js", "LICENSE", "NOTICE.md"])
    await cp("../" + f, path.join(out, "source", f));
  console.log(`Standalone runtime and launcher: ${out}`);
}
