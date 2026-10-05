// SPDX-License-Identifier: GPL-3.0-only
import { spawn } from "node:child_process";
import { once } from "node:events";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import path from "node:path";
import assert from "node:assert/strict";
import { WebSocket } from "ws";
const root = fileURLToPath(new URL("../", import.meta.url));
const directory = path.join(
  root,
  `release/lapis-obsidian-client-${process.platform}-${process.arch}`,
);
const executable = path.join(
  directory,
  process.platform === "win32" ? "node.exe" : "node",
);
const child = spawn(executable, ["runtime/launcher.mjs", "--bootstrap-stdio"], {
  cwd: directory,
  stdio: ["pipe", "pipe", "inherit"],
});
let ws;
const deadline = setTimeout(() => child.kill(), 15000);
try {
  const lines = createInterface({ input: child.stdout });
  const [line] = await once(lines, "line");
  lines.close();
  const bootstrap = JSON.parse(line);
  const response = await fetch(bootstrap.origin);
  assert.equal(response.status, 200);
  assert.match(await response.text(), /Lapis Obsidian Client/);
  ws = new WebSocket(bootstrap.origin.replace("http:", "ws:") + "/bridge", {
    origin: bootstrap.origin,
  });
  await once(ws, "open");
  ws.send(JSON.stringify({ type: "AUTH", token: bootstrap.token }));
  const [message] = await once(ws, "message");
  assert.equal(JSON.parse(message).type, "AUTH_OK");
  const exit = once(child, "exit");
  ws.close();
  const [code] = await exit;
  assert.equal(code, 0);
  await assert.rejects(() => fetch(bootstrap.origin));
  console.log(
    "Packaged runtime: assets, authentication, automatic shutdown and released port passed.",
  );
} finally {
  clearTimeout(deadline);
  ws?.terminate();
  if (child.exitCode === null) {
    child.stdin.end();
    child.kill();
  }
}
