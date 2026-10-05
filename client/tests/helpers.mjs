// SPDX-License-Identifier: GPL-3.0-only
import { spawn } from "node:child_process";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import net from "node:net";
import { once } from "node:events";
import { fileURLToPath } from "node:url";
import { serve } from "../scripts/serve.mjs";
export async function until(check, description = "condition", timeout = 15000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { const v = await check(); if (v) return v; await new Promise(r => setTimeout(r, 20)); }
  throw Error(`Timed out: ${description}`);
}
export async function unusedPort() {
  const server = net.createServer(); await new Promise(r => server.listen(0, "127.0.0.1", r));
  const port = server.address().port; await new Promise(r => server.close(r)); return port;
}
export async function startWeb() {
  const server = await serve(0);
  return { origin:`http://127.0.0.1:${server.address().port}`, close:() => new Promise(r => server.close(r)) };
}
export async function startServer(mode = "creative", origin = "http://127.0.0.1:8080", extra = {}) {
  const port = await unusedPort(), wsPort = await unusedPort();
  const base = fileURLToPath(new URL("../test-results/", import.meta.url)); await mkdir(base, { recursive:true });
  const cwd = await mkdtemp(base + "world-");
  await writeFile(cwd + "/server.txt", `port=${port}\ngamemode=${mode}\nseed=42\n`);
  const child = spawn(fileURLToPath(new URL("../../lapis-obsidian", import.meta.url)), [], {
    cwd, stdio:["ignore", "pipe", "pipe"], env:{...process.env, LAPIS_WS_PORT:String(wsPort), LAPIS_WS_ORIGINS:origin, ...extra},
  });
  let log = "", failure;
  child.stdout.on("data", b => log += b); child.stderr.on("data", b => log += b); child.on("error", e => failure = e);
  await until(async () => {
    if (failure) throw failure; if (child.exitCode !== null) throw Error(log);
    return new Promise(r => { const socket = net.connect(wsPort,"127.0.0.1"); socket.on("connect", () => {socket.destroy();r(true);}); socket.on("error", () => r(false)); });
  }, "C server listen");
  return { port, wsPort, url:`ws://127.0.0.1:${wsPort}/lapisclient`, cwd, child, get log(){return log;}, async stop(){if(child.exitCode===null){child.kill();await once(child,"exit");}await writeFile(cwd+"/server.log",log);if(/ERROR: AddressSanitizer|runtime error:/.test(log))throw Error(log);} };
}
