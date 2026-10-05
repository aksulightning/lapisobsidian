// SPDX-License-Identifier: GPL-3.0-only
import { spawn } from "node:child_process";
import { mkdir, mkdtemp, writeFile, readFile } from "node:fs/promises";
import net from "node:net";
import { once } from "node:events";
import { fileURLToPath } from "node:url";
import { WebSocket } from "ws";
import { startBridge } from "../runtime/bridge.mjs";
export async function until(check, description = "condition", timeout = 15000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    const v = await check();
    if (v) return v;
    await new Promise((r) => setTimeout(r, 20));
  }
  throw Error(`Timed out: ${description}`);
}
export async function unusedPort() {
  const server = net.createServer();
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const port = server.address().port;
  await new Promise((r) => server.close(r));
  return port;
}
export async function startServer(mode = "creative") {
  const port = await unusedPort();
  const base = fileURLToPath(new URL("../test-results/", import.meta.url));
  await mkdir(base, { recursive: true });
  const cwd = await mkdtemp(base + "world-");
  await writeFile(
    cwd + "/server.txt",
    `port=${port}\ngamemode=${mode}\nseed=42\n`,
  );
  const child = spawn(
    fileURLToPath(new URL("../../lapis-obsidian", import.meta.url)),
    [],
    { cwd, stdio: ["ignore", "pipe", "pipe"] },
  );
  let log = "";
  child.stdout.on("data", (b) => {
    log += b;
  });
  child.stderr.on("data", (b) => {
    log += b;
  });
  let failure;
  child.on("error", (e) => (failure = e));
  await until(async () => {
    if (failure) throw failure;
    if (child.exitCode !== null) throw Error(log);
    return new Promise((r) => {
      const socket = net.connect(port, "127.0.0.1");
      socket.on("connect", () => {
        socket.destroy();
        r(true);
      });
      socket.on("error", () => r(false));
    });
  }, "C server listen");
  return {
    port,
    cwd,
    child,
    get log() {
      return log;
    },
    async stop() {
      if (child.exitCode === null) {
        child.kill("SIGTERM");
        await once(child, "exit");
      }
      await writeFile(cwd + "/server.log", log);
    },
  };
}
export async function socketFor(
  bridge,
  token = bridge.token,
  origin = bridge.origin,
) {
  const ws = new WebSocket(bridge.origin.replace("http:", "ws:") + "/bridge", {
    origin,
  });
  await once(ws, "open");
  const controls = [],
    binary = [];
  ws.on("message", (b, isBinary) => {
    if (isBinary) binary.push(new Uint8Array(b));
    else controls.push(JSON.parse(b.toString()));
  });
  ws.send(JSON.stringify({ type: "AUTH", token }));
  return {
    ws,
    controls,
    binary,
    async control(type) {
      return until(() => controls.find((c) => c.type === type), type);
    },
  };
}
export async function protocolTransport(bridge) {
  const peer = await socketFor(bridge);
  await peer.control("AUTH_OK");
  const t = {
    onData: () => {},
    send: (data) => peer.ws.send(data),
    disconnect: () => peer.ws.send('{"type":"DISCONNECT"}'),
    async connect(host, port) {
      peer.ws.send(JSON.stringify({ type: "CONNECT", host, port }));
      await peer.control("CONNECTED");
    },
  };
  peer.ws.on("message", (b, binary) => {
    if (binary) t.onData(new Uint8Array(b));
  });
  return { ...peer, transport: t };
}
export { startBridge };
