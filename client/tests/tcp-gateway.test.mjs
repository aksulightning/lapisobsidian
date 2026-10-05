// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import { once } from "node:events";
import { Readable, Writable } from "node:stream";
import { attachTCP } from "../gateway/tcp.mjs";
import { createGateway } from "../gateway/handler.mjs";

const target = { host: "tcp.example", port: 25565 };
const tick = () => new Promise(resolve => setImmediate(resolve));
async function until(check) {
  for (let n = 0; n < 200; n++) {
    if (check()) return;
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  throw Error("Condition timed out");
}
class Socket extends EventTarget {
  sent = []; closes = [];
  accept() { this.accepted = true; }
  send(data) { this.sent.push(data); }
  close(code, reason) { this.closes.push({ code, reason }); }
  message(data) {
    const event = new Event("message");
    event.data = data;
    this.dispatchEvent(event);
  }
}
function tcp({ opened = Promise.resolve(), write = () => {} } = {}) {
  let incoming, failClosed;
  const socket = {
    opened,
    closed: new Promise((resolve, reject) => { failClosed = reject; }),
    readable: new ReadableStream({ start(controller) { incoming = controller; } }),
    writable: new WritableStream({ write }),
    closes: 0,
    close() {
      this.closes++;
      try { incoming.close(); } catch {}
      return Promise.resolve();
    },
  };
  return { socket, input: () => incoming, failClosed: error => failClosed(error) };
}

test("raw gateway dials once per client and preserves arbitrary bytes in both directions", async () => {
  const peers = new Set(), received = [];
  const greeting = Buffer.from([255, 0, 128, 1]);
  const server = net.createServer(peer => {
    peers.add(peer);
    peer.on("close", () => peers.delete(peer));
    const chunks = [];
    received.push(chunks);
    peer.write(greeting);
    peer.on("data", bytes => { chunks.push(bytes); peer.write(bytes); });
  });
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  const dials = [], sockets = [new Socket(), new Socket()], stops = [];
  try {
    for (const ws of sockets) {
      stops.push(attachTCP(ws, target, (address, options) => {
        dials.push(address);
        assert.equal(options.secureTransport, "off");
        const peer = net.connect(server.address().port, "127.0.0.1");
        return {
          opened: once(peer, "connect"), closed: once(peer, "close"),
          readable: Readable.toWeb(peer), writable: Writable.toWeb(peer),
          close: async () => peer.destroy(),
        };
      }));
    }
    // No AUTH/CONNECT handshake; even bytes arriving during the dial are kept.
    const first = Uint8Array.from({ length: 256 }, (_, n) => n);
    const second = new Uint8Array([9, 0, 254, 8]).subarray(1, 3);
    sockets[0].message(first.buffer);
    sockets[0].message(second);
    sockets[1].message(new Uint8Array([17, 18]).buffer);
    const payloads = [Buffer.concat([first, second]), Buffer.from([17, 18])];
    await until(() => sockets.every((ws, i) =>
      Buffer.concat(ws.sent).length === greeting.length + payloads[i].length));
    assert.deepEqual(dials, Array(2).fill({ hostname: target.host, port: target.port }));
    sockets.forEach((ws, i) => {
      assert.deepEqual(Buffer.concat(ws.sent), Buffer.concat([greeting, payloads[i]]));
      assert.deepEqual(Buffer.concat(received[i]), payloads[i]);
    });
    sockets[0].dispatchEvent(new Event("close"));
    await until(() => peers.size === 1);
    assert.equal(sockets[1].closes.length, 0);
    [...peers][0].end(Buffer.from([99]));
    await until(() => sockets[1].closes.length === 1);
    assert.equal(Buffer.concat(sockets[1].sent).at(-1), 99);
    assert.equal(sockets[1].closes[0].code, 1000);
  } finally {
    stops.forEach(stop => stop());
    peers.forEach(peer => peer.destroy());
    await new Promise(resolve => server.close(resolve));
  }
});

test("TCP writes wait for opening and drain serially under backpressure", async () => {
  const opening = Promise.withResolvers(), firstWrite = Promise.withResolvers();
  const written = [];
  const fake = tcp({ opened: opening.promise, write: async bytes => {
    written.push([...bytes]);
    if (written.length === 1) await firstWrite.promise;
  } });
  const ws = new Socket(), stop = attachTCP(ws, target, () => fake.socket);
  try {
    ws.message(new Uint8Array([1])); ws.message(new Uint8Array([2]));
    await tick(); assert.deepEqual(written, []);
    opening.resolve(); await tick(); assert.deepEqual(written, [[1]]);
    firstWrite.resolve(); await tick(); assert.deepEqual(written, [[1], [2]]);
  } finally { opening.resolve(); firstWrite.resolve(); stop(); }
  await tick();
  assert.equal(fake.socket.readable.locked, false);
  assert.equal(fake.socket.writable.locked, false);
});

test("text, oversized messages, and overflowing queues close without protocol decoding", async t => {
  for (const [name, send, code] of [
    ["text", ws => ws.message('{"type":"AUTH","token":""}'), 1003],
    ["large frame", ws => ws.message(new Uint8Array(1024 * 1024 + 1)), 1009],
    ["byte limit", ws => { for (let i = 0; i < 5; i++) ws.message(new Uint8Array(1024 * 1024)); }, 1009],
    ["message limit", ws => { for (let i = 0; i < 1025; i++) ws.message(new Uint8Array([1])); }, 1009],
  ]) await t.test(name, async () => {
    const opening = Promise.withResolvers();
    let written = 0;
    const fake = tcp({ opened: opening.promise, write: () => written++ });
    const ws = new Socket(), stop = attachTCP(ws, target, () => fake.socket);
    send(ws); stop(); opening.resolve(); await tick();
    assert.equal(ws.closes[0].code, code);
    assert.equal(ws.closes.length, 1);
    assert.equal(fake.socket.closes, 1);
    assert.equal(written, 0);
  });
});

test("TCP and WebSocket failures close both sides once", async t => {
  for (const failure of ["dial", "opened", "closed", "read", "write", "send", "websocket"]) {
    await t.test(failure, async () => {
      const error = Error("Failure");
      const fake = tcp({
        opened: failure === "opened" ? Promise.reject(error) : Promise.resolve(),
        write: () => { if (failure === "write") throw error; },
      });
      const ws = new Socket();
      if (failure === "send") ws.send = () => { throw error; };
      const stop = attachTCP(ws, target, () => {
        if (failure === "dial") throw error;
        return fake.socket;
      });
      if (failure === "closed") fake.failClosed(error);
      if (failure === "read") fake.input().error(error);
      if (failure === "write") ws.message(new Uint8Array([1]));
      if (failure === "send") fake.input().enqueue(new Uint8Array([1]));
      if (failure === "websocket") ws.dispatchEvent(new Event("error"));
      await tick(); stop();
      assert.equal(ws.closes[0].code, 1011);
      assert.equal(ws.closes.length, 1);
      assert.equal(fake.socket.closes, failure === "dial" ? 0 : 1);
    });
  }
});

test("closing during a dial discards queued bytes and clears the dial timer", async t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const opening = Promise.withResolvers();
  let written = 0;
  const fake = tcp({ opened: opening.promise, write: () => written++ });
  const ws = new Socket();
  attachTCP(ws, target, () => fake.socket);
  ws.message(new Uint8Array([1]));
  ws.dispatchEvent(new Event("close"));
  opening.resolve(); await tick();
  t.mock.timers.tick(10000);
  assert.equal(written, 0);
  assert.equal(fake.socket.closes, 1);
  assert.deepEqual(ws.closes.map(x => x.code), [1000]);
});

test("a stalled TCP dial times out and releases the connection", async t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const opening = Promise.withResolvers(), fake = tcp({ opened: opening.promise });
  const ws = new Socket();
  attachTCP(ws, target, () => fake.socket);
  t.mock.timers.tick(10000);
  opening.resolve(); await tick();
  assert.equal(fake.socket.closes, 1);
  assert.equal(ws.closes[0].code, 1011);
  assert.match(ws.closes[0].reason, /timed out/);
});

test("Worker validates upgrades and configuration before dialing the fixed target", async () => {
  const env = { SERVER_HOST: target.host, SERVER_PORT: target.port, ALLOWED_ORIGIN: "https://client.example" };
  const pairs = [], dials = [];
  const platform = {
    WebSocketPair: class {
      constructor() { this[0] = new Socket(); this[1] = new Socket(); pairs.push(this); }
    },
    Response: class { constructor(body, init) { this.body = body; Object.assign(this, init); } },
  };
  const gateway = createGateway(address => { dials.push(address); return tcp().socket; }, platform);
  const request = (path = "/tcp", headers = {}, method = "GET") => new Request(`https://gateway.example${path}`, {
    method, headers: { Origin: env.ALLOWED_ORIGIN, Upgrade: "websocket", ...headers },
  });
  assert.equal(gateway.fetch(request("/missing"), env).status, 404);
  assert.equal(gateway.fetch(request(), { ...env, SERVER_HOST: "" }).status, 503);
  assert.equal(gateway.fetch(request(), { ...env, SERVER_PORT: -1 }).status, 503);
  assert.equal(gateway.fetch(request("/tcp", { Origin: "https://other.example" }), env).status, 403);
  assert.equal(gateway.fetch(request("/tcp", { Origin: "" }), env).status, 403);
  assert.equal(gateway.fetch(request("/tcp", { Upgrade: "" }), env).status, 426);
  assert.equal(gateway.fetch(request("/tcp", {}, "POST"), env).status, 426);
  assert.equal(dials.length, 0);
  const response = gateway.fetch(request("/tcp?host=evil.example&port=1234"), env);
  assert.equal(response.status, 101);
  assert.equal(response.webSocket, pairs[0][0]);
  assert.equal(pairs[0][1].accepted, true);
  assert.equal(pairs[0][1].binaryType, "arraybuffer");
  assert.deepEqual(dials, [{ hostname: target.host, port: target.port }]);
  // The game's control-protocol route remains available and waits for CONNECT.
  assert.equal(gateway.fetch(request("/bridge"), env).status, 101);
  assert.equal(dials.length, 1);
  pairs.forEach(pair => pair[1].dispatchEvent(new Event("close")));
  await tick();
});
