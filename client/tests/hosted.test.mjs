// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import { Readable, Writable } from "node:stream";
import { once } from "node:events";
import { attachSession, configuration } from "../gateway/session.mjs";
import { HostedTransport, gatewayURL } from "../web/hosted-transport.js";
import { packet } from "../web/protocol/binary.js";

const target = { host: "game.example", port: 25565 };
const tick = () => new Promise(resolve => setImmediate(resolve));
async function until(check) {
  for (let n = 0; n < 200; n++) {
    if (check()) return;
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  throw Error("Condition timed out");
}
class Socket extends EventTarget {
  sent = []; bufferedAmount = 0; ended = false;
  send(data) { this.sent.push(data); }
  close() { this.ended = true; }
  message(data) {
    const event = new Event("message"); event.data = data;
    this.dispatchEvent(event);
  }
  control(type, values = {}) { this.message(JSON.stringify({ type, ...values })); }
  controls() { return this.sent.filter(x => typeof x === "string").map(JSON.parse); }
}

test("secure gateway URLs and explicit operator configuration", () => {
  assert.equal(gatewayURL("wss://game.example/bridge"), "wss://game.example/bridge");
  for (const url of ["", "ws://game.example", "https://game.example", "wss://user:pass@game.example", "wss://game.example/#secret", "wss://game.example/?token=x"])
    assert.throws(() => gatewayURL(url));
  assert.throws(() => configuration({}));
  const env = { SERVER_HOST: "game.example", SERVER_PORT: "25565", ALLOWED_ORIGIN: "https://aksulightning.github.io" };
  assert.deepEqual(configuration(env), { ...target, origin: env.ALLOWED_ORIGIN });
  assert.throws(() => configuration({ ...env, SERVER_PORT: "0" }));
  assert.throws(() => configuration({ ...env, ALLOWED_ORIGIN: "https://example.org/path" }));
});

test("gateway rejects unapproved targets and raw non-protocol forwarding", async () => {
  let dials = 0;
  const ws = new Socket();
  const stop = attachSession(ws, target, () => { dials++; });
  try {
    ws.control("AUTH", { token: "" });
    ws.control("CONNECT", { host: "other.example", port: 25565 });
    assert.equal(dials, 0);
    assert.equal(ws.controls().at(-1).code, "TARGET_NOT_ALLOWED");
    ws.message(new Uint8Array([1, 2]).buffer);
    assert.equal(ws.controls().at(-1).code, "PROTOCOL_REQUIRED");
    assert.equal(ws.ended, true);
  } finally { stop(); }
});

test("gateway preserves bytes through a real TCP socket, disconnects and reconnects", async () => {
  const received = [], peers = new Set();
  const server = net.createServer(peer => {
    peers.add(peer);
    peer.on("close", () => peers.delete(peer));
    peer.on("data", bytes => { received.push(bytes); peer.write(bytes); });
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  let dials = 0;
  const connect = (address, options) => {
    assert.deepEqual(address, { hostname: target.host, port: target.port });
    assert.equal(options.secureTransport, "off");
    dials++;
    const peer = net.connect(server.address().port, "127.0.0.1");
    return {
      opened: once(peer, "connect"), closed: once(peer, "close"),
      readable: Readable.toWeb(peer), writable: Writable.toWeb(peer),
      close: async () => peer.destroy(),
    };
  };
  const ws = new Socket(), stop = attachSession(ws, target, connect);
  try {
    ws.control("AUTH", { token: "" });
    for (let round = 0; round < 2; round++) {
      ws.control("CONNECT", target);
      await until(() => ws.controls().filter(x => x.type === "CONNECTED").length === round + 1);
      const hello = packet(0, w => w.varint(772).string(target.host).u16(target.port).varint(2));
      ws.message(hello.slice(0, 3).buffer); ws.message(hello.slice(3).buffer);
      await until(() => Buffer.concat(received).length === hello.length * (round + 1));
      await until(() => ws.sent.filter(x => typeof x !== "string").reduce((n, x) => n + x.byteLength, 0) === hello.length * (round + 1));
      assert.deepEqual(Buffer.concat(received), Buffer.concat(Array(round + 1).fill(Buffer.from(hello))));
      ws.control("DISCONNECT");
      assert.equal(ws.controls().at(-1).type, "DISCONNECTED");
    }
    assert.equal(dials, 2);
  } finally {
    stop(); for (const peer of peers) peer.destroy();
    await new Promise(resolve => server.close(resolve));
  }
});

test("malformed handshake never reaches TCP and failed dials are reported", async () => {
  let written = 0, shut = 0;
  const ws = new Socket();
  const stop = attachSession(ws, target, () => ({
    opened: Promise.resolve(), closed: Promise.resolve(),
    readable: new ReadableStream(),
    writable: new WritableStream({ write: () => written++ }),
    close: async () => shut++,
  }));
  try {
    ws.control("AUTH", { token: "" }); ws.control("CONNECT", target); await tick();
    ws.message(packet(0, w => w.varint(772).string("other.example").u16(25565).varint(2)).buffer);
    await tick(); assert.equal(written, 0); assert.equal(shut, 1);
    assert.equal(ws.controls().at(-1).code, "PROTOCOL_REQUIRED");
  } finally { stop(); }
  const failed = new Socket();
  const stopFailed = attachSession(failed, target, () => ({
    opened: Promise.reject(Error("Offline")), closed: Promise.resolve(), close: async () => {},
  }));
  try {
    failed.control("AUTH", { token: "" }); failed.control("CONNECT", target);
    await tick(); assert.equal(failed.controls().at(-2).code, "CONNECT_FAILED");
    assert.equal(failed.controls().at(-1).type, "DISCONNECTED");
  } finally { stopFailed(); }
});

test("browser hosted transport authenticates, connects, and recovers after gateway errors", async () => {
  const original = globalThis.WebSocket;
  let latest, failNext = false;
  class FakeBrowserSocket {
    static OPEN = 1;
    readyState = 1; bufferedAmount = 0; sent = [];
    constructor(url) {
      this.url = url; latest = this;
      const fail = failNext; failNext = false;
      queueMicrotask(() => fail ? this.onerror?.() : this.onopen?.());
    }
    send(data) {
      this.sent.push(data);
      const message = typeof data === "string" && JSON.parse(data);
      if (message.type === "AUTH") queueMicrotask(() => this.onmessage({ data: '{"type":"AUTH_OK"}' }));
      if (message.type === "CONNECT") queueMicrotask(() => this.onmessage({ data: '{"type":"CONNECTED"}' }));
      if (message.type === "DISCONNECT") queueMicrotask(() => this.onmessage?.({ data: '{"type":"DISCONNECTED"}' }));
    }
    close() { this.readyState = 3; queueMicrotask(() => this.onclose?.()); }
  }
  globalThis.WebSocket = FakeBrowserSocket;
  const transport = new HostedTransport(() => {});
  try {
    await transport.prepare("wss://game.example/bridge");
    assert.deepEqual(JSON.parse(latest.sent[0]), { type: "AUTH", token: "" });
    await transport.connect(target.host, target.port);
    assert.equal(transport.connected, true);
    transport.disconnect(); await tick(); assert.equal(transport.connected, false);
    await transport.connect(target.host, target.port);
    transport.close(); await tick();
    assert.equal(transport.ready, false);
    await transport.prepare("wss://game.example/bridge");
    assert.equal(transport.ready, true);
    transport.close();
    failNext = true;
    await assert.rejects(transport.prepare("wss://game.example/bridge"), /Cannot reach/);
    assert.equal(transport.ready, false);
    await transport.prepare("wss://game.example/bridge");
    assert.equal(transport.ready, true);
  } finally { transport.close(); globalThis.WebSocket = original; }
});
