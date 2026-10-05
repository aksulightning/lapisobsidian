// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import { once } from "node:events";
import { Readable, Writable } from "node:stream";
import { HTTPSession, configuration } from "../gateway/session.mjs";
import gateway from "../gateway/handler.mjs";
import { HostedTransport, gatewayURL } from "../web/hosted-transport.js";
import { ProtocolClient } from "../web/protocol/client.js";
import { startServer, until } from "./helpers.mjs";

const origin = "https://client.example";
const env = { SERVER_HOST: "game.example", SERVER_PORT: 25565, ALLOWED_ORIGIN: origin };
const tick = () => new Promise(resolve => setImmediate(resolve));
function harness(connect) {
  const sessions = new Map();
  const bindings = { ...env, TCP_SESSIONS: {
    newUniqueId: () => crypto.randomUUID().replaceAll("-", "").repeat(2),
    idFromString: id => id,
    get(id) {
      if (!sessions.has(id)) sessions.set(id, new HTTPSession(env, connect));
      return { fetch: (input, init) => sessions.get(id).fetch(new Request(input, init)) };
    },
  } };
  const requests = [];
  const fetcher = (input, init = {}) => {
    requests.push({ url: input, ...init });
    return gateway.fetch(new Request(input, { ...init, headers: { Origin: origin, ...init.headers } }), bindings);
  };
  return { bindings, fetcher, requests, sessions, stop: () => sessions.forEach(s => s.stop()) };
}
function realConnect(port) {
  return address => {
    assert.deepEqual(address, { hostname: env.SERVER_HOST, port: env.SERVER_PORT });
    const peer = net.connect(port, "127.0.0.1");
    return {
      opened: once(peer, "connect"), closed: once(peer, "close"),
      readable: Readable.toWeb(peer), writable: Writable.toWeb(peer),
      close: async () => peer.destroy(),
    };
  };
}
function fakeTCP(opened = Promise.resolve()) {
  let incoming;
  const writes = [];
  return {
    opened, closed: new Promise(() => {}), writes, closes: 0,
    readable: new ReadableStream({ start(c) { incoming = c; } }),
    writable: new WritableStream({ write: b => writes.push(b.slice()) }),
    enqueue: bytes => incoming.enqueue(bytes), end: () => incoming.close(),
    close() { this.closes++; return Promise.resolve(); },
  };
}
const req = (path, method = "GET", headers = {}, body) => new Request(`https://session${path}`, { method, headers, body });

test("HTTPS URLs and fixed operator configuration", () => {
  assert.equal(gatewayURL("https://game.example/bridge/"), "https://game.example/bridge");
  for (const url of ["", "ws://game.example", "wss://game.example", "http://game.example", "https://u:p@game.example", "https://game.example/#secret", "https://game.example/?token=x"])
    assert.throws(() => gatewayURL(url));
  assert.deepEqual(configuration(env), { host: env.SERVER_HOST, port: 25565, origin });
  for (const invalid of [{}, { ...env, SERVER_PORT: 0 }, { ...env, ALLOWED_ORIGIN: origin + "/path" }]) assert.throws(() => configuration(invalid));
});

test("HTTPS routing enforces Origin, target, bearer sessions, methods, CORS, and no upgrades", async () => {
  let dials = 0;
  const h = harness(() => { dials++; return fakeTCP(); });
  try {
    const call = (path, method = "GET", headers = {}, body) => gateway.fetch(new Request(`https://gateway.example${path}`, {
      method, headers: { Origin: origin, ...headers }, body,
    }), h.bindings);
    assert.equal((await call("/bridge", "POST", { Origin: "https://bad.example" })).status, 403);
    assert.equal((await call("/bridge", "GET", { Upgrade: "websocket" })).status, 405);
    assert.equal((await call("/tcp")).status, 404);
    assert.equal((await call("/bridge/read")).status, 401);
    assert.equal((await call("/bridge/read", "GET", { Authorization: "Bearer invalid" })).status, 401);
    assert.equal((await call("/bridge/read", "GET", { Authorization: `Bearer ${"a".repeat(64)}` })).status, 410);
    const preflight = await call("/bridge/write", "OPTIONS");
    assert.equal(preflight.status, 204);
    assert.equal(preflight.headers.get("Access-Control-Allow-Origin"), origin);
    assert.match(preflight.headers.get("Access-Control-Allow-Headers"), /Authorization/);
    assert.equal((await call("/bridge", "POST", { "Content-Type": "application/json" }, JSON.stringify({ host: "other.example", port: 25565 }))).status, 403);
    assert.equal((await call("/bridge", "POST", { "Content-Type": "application/json" }, "{" )).status, 400);
    assert.equal(dials, 0);
    const response = await call("/bridge", "POST", { "Content-Type": "application/json" }, JSON.stringify({ host: env.SERVER_HOST, port: env.SERVER_PORT }));
    assert.equal(response.status, 201);
    assert.equal(response.headers.get("Cache-Control"), "no-store");
    assert.match((await response.json()).session, /^[a-f0-9]{64}$/);
    assert.equal(dials, 1);
  } finally { h.stop(); }
});

test("HTTP reads retain pending TCP reads across polls, EOF, and idle expiry", async t => {
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const tcp = fakeTCP(), session = new HTTPSession(env, () => tcp);
  await session.open();
  const first = session.fetch(req("/read"));
  assert.equal((await session.fetch(req("/read"))).status, 409);
  t.mock.timers.tick(20000);
  assert.equal((await first).status, 204);
  const second = session.fetch(req("/read"));
  tcp.enqueue(new Uint8Array([0, 255, 128]));
  assert.deepEqual(new Uint8Array(await (await second).arrayBuffer()), new Uint8Array([0, 255, 128]));
  tcp.end();
  assert.equal((await session.fetch(req("/read"))).status, 205);
  assert.equal(tcp.closes, 1);
  const idle = fakeTCP(), abandoned = new HTTPSession(env, () => idle);
  await abandoned.open(); t.mock.timers.tick(60000);
  assert.equal(idle.closes, 1);
  assert.equal((await abandoned.fetch(req("/read"))).status, 410);
});

test("ordered writes reject duplicates, text and oversized bodies; failed dials close TCP", async t => {
  const tcp = fakeTCP(), session = new HTTPSession(env, () => tcp);
  await session.open();
  const write = (sequence, body, type = "application/octet-stream") => session.fetch(req("/write", "POST", {
    "Content-Type": type, "X-Sequence": String(sequence),
  }, body));
  try {
    assert.equal((await write(0, new Uint8Array([0, 254]))).status, 204);
    assert.equal((await write(0, new Uint8Array([9]))).status, 409);
    assert.equal((await write(1, "text", "text/plain")).status, 415);
    assert.equal((await write(1, new Uint8Array(65537))).status, 413);
    assert.deepEqual(tcp.writes, [new Uint8Array([0, 254])]);
    assert.equal(tcp.closes, 1);
  } finally { session.stop(); }
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const stalled = fakeTCP(new Promise(() => {}));
  const dial = new HTTPSession(env, () => stalled).open();
  t.mock.timers.tick(10000);
  assert.equal((await dial).status, 502);
  assert.equal(stalled.closes, 1);
  assert.equal((await new HTTPSession(env, () => { throw Error("Offline"); }).open()).status, 502);
});

test("browser HTTPS transport preserves arbitrary bytes with separate real TCP sockets and reconnects", async () => {
  const received = [], peers = new Set();
  const server = net.createServer(peer => {
    peers.add(peer); peer.on("close", () => peers.delete(peer));
    const chunks = []; received.push(chunks);
    peer.write(new Uint8Array([128, 255, 0]));
    peer.on("data", bytes => { chunks.push(bytes); peer.write(bytes); });
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  const h = harness(realConnect(server.address().port));
  const transports = [new HostedTransport(() => {}, h.fetcher), new HostedTransport(() => {}, h.fetcher)];
  const outputs = [[], []];
  try {
    for (const [i, transport] of transports.entries()) {
      transport.onData = bytes => outputs[i].push(bytes);
      await transport.prepare("https://gateway.example/bridge");
      await transport.connect(env.SERVER_HOST, env.SERVER_PORT);
    }
    const data = Uint8Array.from({ length: 150000 }, (_, n) => n % 256);
    transports[0].send(data.subarray(0, 777)); transports[0].send(data.subarray(777));
    transports[1].send(new Uint8Array([5, 0, 6]));
    await until(() => Buffer.concat(outputs[0]).length === data.length + 3 && Buffer.concat(outputs[1]).length === 6);
    assert.deepEqual(Buffer.concat(received[0]), Buffer.from(data));
    assert.deepEqual(Buffer.concat(outputs[0]), Buffer.concat([Buffer.from([128, 255, 0]), data]));
    assert.deepEqual(Buffer.concat(received[1]), Buffer.from([5, 0, 6]));
    transports[0].disconnect(); await until(() => peers.size === 1);
    assert.equal(transports[1].connected, true);
    await transports[0].connect(env.SERVER_HOST, env.SERVER_PORT);
    assert.equal(received.length, 3);
    assert.ok(h.requests.every(r => r.url.startsWith("https://")));
    const writes = h.requests.filter(r => r.url.endsWith("/write"));
    assert.ok(writes.length >= 4);
    assert.ok(writes.every(r => r.body.byteLength <= 65536));
  } finally {
    transports.forEach(t => t.close()); h.stop(); peers.forEach(p => p.destroy());
    await new Promise(resolve => server.close(resolve));
  }
});

test("browser failed writes are never retried and disconnect cancels the session", async () => {
  let writes = 0, closes = 0, errors = 0;
  const tcp = fakeTCP(), h = harness(() => tcp);
  const transport = new HostedTransport(() => {}, async (url, init) => {
    if (url.endsWith("/write")) { writes++; return new Response("Unavailable", { status: 502 }); }
    return h.fetcher(url, init);
  });
  transport.onClose = () => closes++;
  transport.onError = () => errors++;
  await transport.prepare("https://gateway.example/bridge");
  await transport.connect(env.SERVER_HOST, env.SERVER_PORT);
  transport.send(new Uint8Array([1])); await tick();
  assert.equal(writes, 1); assert.equal(closes, 1); assert.equal(errors, 1);
  assert.equal(transport.connected, false);
  transport.close(); h.stop();
});

test("HTTPS client plays against the actual C TCP server without a WebSocket", async () => {
  const server = await startServer();
  const h = harness(realConnect(server.port));
  const transport = new HostedTransport(() => {}, h.fetcher);
  const events = [];
  let failure;
  transport.onError = e => { failure = e; };
  try {
    await transport.prepare("https://gateway.example/bridge");
    for (let round = 0; round < 2; round++) {
      events.length = 0;
      const client = new ProtocolClient(transport, (type, value) => {
        events.push({ type, value });
        if (type === "chunk") client.loaded();
      });
      await transport.connect(env.SERVER_HOST, env.SERVER_PORT);
      client.login(env.SERVER_HOST, env.SERVER_PORT, "HTTPSPlayer", new Uint8Array(16).fill(12), 2);
      await until(() => events.some(e => e.type === "chunk"), "HTTPS gameplay chunk");
      client.chat("HTTPS TCP works");
      await until(() => events.some(e => e.type === "chat" && e.value.includes("HTTPS TCP works")), "HTTPS chat");
      assert.equal(failure, undefined);
      transport.disconnect();
      await until(() => [...h.sessions.values()].every(s => s.state === "closed"));
    }
  } finally { transport.close(); h.stop(); await server.stop(); }
});

test("read errors, stalled writes and client queue overflow clean up both sides", async t => {
  const tcp = fakeTCP(), session = new HTTPSession(env, () => tcp);
  tcp.readable = new ReadableStream({ start(controller) { controller.error(Error("Read failed")); } });
  await session.open();
  assert.equal((await session.fetch(req("/read"))).status, 502);
  assert.equal(tcp.closes, 1);
  t.mock.timers.enable({ apis: ["setTimeout"] });
  const stalled = fakeTCP();
  const release = Promise.withResolvers();
  stalled.writable = new WritableStream({ write: () => release.promise });
  const writing = new HTTPSession(env, () => stalled);
  await writing.open();
  const result = writing.fetch(req("/write", "POST", { "Content-Type": "application/octet-stream", "X-Sequence": "0" }, new Uint8Array([1])));
  await tick();
  t.mock.timers.tick(10000);
  assert.equal((await result).status, 502);
  assert.equal(stalled.closes, 1);
  release.resolve();
  t.mock.timers.reset();
  const h = harness(() => fakeTCP());
  const transport = new HostedTransport(() => {}, h.fetcher);
  await transport.prepare("https://gateway.example/bridge");
  await transport.connect(env.SERVER_HOST, env.SERVER_PORT);
  assert.throws(() => transport.send(new Uint8Array(256 * 1024 + 1)), /queue limit/);
  assert.equal(transport.connected, false);
  await transport.cleanup;
  assert.ok([...h.sessions.values()].every(s => s.state === "closed"));
  transport.close(); h.stop();
});

test("closing during connection setup disposes a late session without reconnecting", async () => {
  const opening = Promise.withResolvers(), calls = [];
  const transport = new HostedTransport(() => {}, (url, options) => {
    calls.push(options.method);
    if (options.method === "POST") return opening.promise;
    return Promise.resolve(new Response(null, { status: 204 }));
  });
  await transport.prepare("https://gateway.example/bridge");
  const connecting = transport.connect(env.SERVER_HOST, env.SERVER_PORT);
  await tick();
  transport.close();
  opening.resolve(Response.json({ session: "a".repeat(64) }, { status: 201 }));
  await assert.rejects(connecting, /cancelled/);
  assert.equal(transport.connected, false);
  assert.equal(transport.ready, false);
  assert.deepEqual(calls, ["POST", "DELETE"]);
});
