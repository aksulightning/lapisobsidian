// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import http from "node:http";
import { once } from "node:events";
import { WebSocket } from "ws";
import { startBridge, socketFor, until, unusedPort } from "./helpers.mjs";
import { packet } from "../web/protocol/binary.js";
import { validTarget } from "../runtime/bridge.mjs";
test("loopback, random token/port, Origin and Host validation, auth rejection", async () => {
  const a = await startBridge(),
    b = await startBridge();
  try {
    assert.notEqual(a.port, b.port);
    assert.notEqual(a.token, b.token);
    assert.match(a.origin, /^http:\/\/127\.0\.0\.1:/);
    for (const token of ["0".repeat(64), a.token + "a"]) {
      const p = await socketFor(a, token);
      assert.equal((await p.control("ERROR")).code, "AUTH_FAILED");
      p.ws.terminate();
    }
    const foreign = new WebSocket(a.origin.replace("http", "ws") + "/bridge", {
      origin: "https://evil.example",
    });
    const [err] = await once(foreign, "error");
    assert.match(err.message, /403/);
    const badHost = await new Promise((resolve) =>
      http.get(a.origin, { headers: { host: "evil.example" } }, (res) => {
        res.resume();
        resolve(res.statusCode);
      }),
    );
    assert.equal(badHost, 403);
    assert.equal((await fetch(a.origin + "/runtime/launcher.mjs")).status, 404);
    const anonymous = new WebSocket(
      a.origin.replace("http", "ws") + "/bridge",
      { origin: a.origin },
    );
    await once(anonymous, "open");
    anonymous.send('{"type":"CONNECT","host":"localhost","port":25565}');
    await once(anonymous, "close");
    const p = await socketFor(a);
    await p.control("AUTH_OK");
    p.ws.send('{"type":"PING","extra":true}');
    assert.equal((await p.control("ERROR")).code, "INVALID_CONTROL");
    p.ws.terminate();
  } finally {
    await a.close();
    await b.close();
  }
  for (const [host, port] of [
    ["", 1],
    ["https://host", 1],
    ["a b", 1],
    ["a", 0],
    ["a", 65536],
    ["a", 1.2],
    ["-bad", 1],
  ])
    assert.equal(validTarget(host, port), false);
  assert.ok(validTarget("::1", 25565));
  assert.ok(validTarget("game.example", 25565));
});
test("binary integrity, denied generic forwarding, connection failure and complete shutdown", async () => {
  let remote;
  const received = [];
  const tcp = net.createServer((s) => {
    remote = s;
    s.on("data", (b) => received.push(b));
  });
  await new Promise((r) => tcp.listen(0, "127.0.0.1", r));
  const port = tcp.address().port;
  const bridge = await startBridge();
  let p;
  try {
    p = await socketFor(bridge);
    await p.control("AUTH_OK");
    p.ws.send(JSON.stringify({ type: "CONNECT", host: "127.0.0.1", port }));
    await p.control("CONNECTED");
    const hello = packet(0, (w) =>
      w.varint(772).string("127.0.0.1").u16(port).varint(2),
    );
    p.ws.send(hello.subarray(0, 4));
    p.ws.send(hello.subarray(4));
    await until(
      () => Buffer.concat(received).length === hello.length,
      "exact outbound bytes",
    );
    assert.deepEqual(new Uint8Array(Buffer.concat(received)), hello);
    const bytes = Buffer.alloc(150000);
    for (let i = 0; i < bytes.length; i++) bytes[i] = i % 251;
    remote.write(bytes);
    await until(
      () => p.binary.reduce((n, b) => n + b.length, 0) === bytes.length,
      "exact inbound bytes",
    );
    assert.deepEqual(Buffer.concat(p.binary), bytes);
    const closed = once(remote, "close");
    await bridge.close();
    await closed;
    await onceIfOpen(p.ws);
    const connection = net.connect(bridge.port, "127.0.0.1");
    const [error] = await once(connection, "error");
    assert.equal(error.code, "ECONNREFUSED");
  } finally {
    p?.ws.terminate();
    remote?.destroy();
    await bridge.close();
    await new Promise((r) => tcp.close(r));
  }
  const b = await startBridge();
  try {
    const q = await socketFor(b);
    await q.control("AUTH_OK");
    q.ws.send(
      JSON.stringify({
        type: "CONNECT",
        host: "127.0.0.1",
        port: await unusedPort(),
      }),
    );
    assert.equal((await q.control("ERROR")).code, "CONNECT_FAILED");
    await q.control("DISCONNECTED");
    q.ws.send('{"type":"PING"}');
    await q.control("PONG");
    q.ws.terminate();
  } finally {
    await b.close();
  }
});
async function onceIfOpen(ws) {
  if (ws.readyState !== WebSocket.CLOSED) await once(ws, "close");
}
