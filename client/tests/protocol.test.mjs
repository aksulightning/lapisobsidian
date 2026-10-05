// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import {
  Reader,
  Writer,
  Framer,
  packet,
  nbtText,
} from "../web/protocol/binary.js";
import { decodeChunk, World } from "../web/world/world.js";
import { ProtocolClient } from "../web/protocol/client.js";
import { ProtocolGate } from "../runtime/gate.mjs";
import { registry } from "../web/protocol/registry.js";
import {
  startBridge,
  startServer,
  protocolTransport,
  until,
} from "./helpers.mjs";
test("framing preserves split/coalesced bytes, signed VarInts and packed negative coordinates", () => {
  for (const n of [0, 127, 128, 772, 2147483647, -1, -2147483648])
    assert.equal(new Reader(new Writer().varint(n).finish()).varint(), n);
  const position = { x: -335, y: -64, z: 32767 };
  assert.deepEqual(
    new Reader(new Writer().position(position).finish()).position(),
    position,
  );
  const a = packet(0x27, (w) => w.raw(new Uint8Array(150000).fill(43))),
    b = packet(2, (w) => w.string("hello")),
    all = new Uint8Array(a.length + b.length);
  all.set(a);
  all.set(b, a.length);
  const received = [],
    f = new Framer((p) => received.push(p));
  for (let i = 0; i < all.length; i += 137) f.push(all.subarray(i, i + 137));
  assert.equal(received.length, 2);
  assert.equal(new Reader(received[0]).varint(), 0x27);
  assert.equal(received[0].length, 150001);
  assert.throws(() =>
    new Framer(() => {}).push(new Uint8Array([255, 255, 255])),
  );
  assert.throws(() =>
    new Reader(new Uint8Array([255, 255, 255, 255, 255])).varint(),
  );
  assert.throws(() => new Reader(new Uint8Array([255])).string());
});
test("bridge gate permits exact protocol handshake and rejects arbitrary traffic", () => {
  const sent = [],
    gate = new ProtocolGate("example.org", 25565, (b) => sent.push(b));
  gate.push(new TextEncoder().encode("GET / HTTP/1.1\r\n"));
  assert.equal(sent.length, 0);
  assert.throws(() => gate.push(new Uint8Array(100)));
  const accepted = new ProtocolGate("example.org", 25565, (b) => sent.push(b)),
    hello = packet(0, (w) =>
      w.varint(registry.protocol).string("example.org").u16(25565).varint(2),
    );
  for (const b of hello) accepted.push(new Uint8Array([b]));
  assert.deepEqual(sent[0], hello);
  assert.throws(() =>
    new ProtocolGate("example.org", 25565, () => {}).push(
      packet(0, (w) => w.varint(772).string("other.org").u16(25565).varint(2)),
    ),
  );
});
test("actual testing C server: login, chunks, movement, chat, creative inventory, block updates, reconnect", async () => {
  const server = await startServer(),
    bridge = await startBridge();
  let peer;
  try {
    peer = await protocolTransport(bridge);
    const events = [],
      chunks = [],
      world = new World();
    let position,
      loaded = false,
      error;
    const client = new ProtocolClient(peer.transport, (type, value) => {
      events.push({ type, value });
      if (type === "error") error = value;
      if (type === "position") position = value;
      if (type === "chunk") {
        const c = decodeChunk(value);
        world.add(c);
        chunks.push(c);
        if (!loaded) {
          loaded = true;
          client.loaded();
        }
      }
      if (type === "block") world.set(value);
    });
    await peer.transport.connect("127.0.0.1", server.port);
    client.login(
      "127.0.0.1",
      server.port,
      "Integration",
      new Uint8Array(16).fill(11),
      4,
    );
    await until(() => chunks.length >= 25, "25 real server chunks", 30000);
    assert.equal(error, undefined);
    assert.ok(position.y > 0);
    assert.ok(world.has(position.x, position.z));
    assert.ok(chunks.some((c) => c.blocks.some((b) => b !== 0 && b !== 85)));
    assert.ok(events.some((e) => e.type === "health"));
    assert.ok(events.some((e) => e.type === "slot"));
    // Server validates this movement and receives chat on the same live connection.
    client.move({ ...position, x: position.x + 1, grounded: true });
    client.input(32);
    client.select(2);
    client.chat("Protocol integration ✓");
    await until(
      () =>
        events.some(
          (e) =>
            e.type === "chat" && e.value.includes("Protocol integration ✓"),
        ),
      "chat echo",
    );
    client.creative(38, registry.items.stone);
    await until(
      () =>
        events.some(
          (e) =>
            e.type === "slot" &&
            e.value.slot === 38 &&
            e.value.item === registry.items.stone,
        ),
      "creative slot",
    );
    const x = Math.floor(position.x) + 1,
      z = Math.floor(position.z);
    let y = Math.floor(position.y) - 1;
    while (y > 0 && !world.get(x, y, z)) y--;
    const before = events.length;
    client.action(0, { x, y, z, face: 1 });
    await until(
      () =>
        events
          .slice(before)
          .some(
            (e) =>
              e.type === "block" &&
              e.value.x === x &&
              e.value.y === y &&
              e.value.z === z &&
              e.value.state === 0,
          ),
      "server block mining",
    );
    client.use({ x, y: y - 1, z, face: 1 }, 0, 0);
    await until(
      () =>
        events
          .slice(before)
          .some(
            (e) =>
              e.type === "block" &&
              e.value.x === x &&
              e.value.y === y &&
              e.value.z === z &&
              e.value.state === registry.palette.stone,
          ),
      "server placement",
    );
    client.click(0, 38);
    await until(
      () =>
        events.some(
          (e) => e.type === "cursor" && e.value.item === registry.items.stone,
        ),
      "inventory pickup",
    );
    client.click(0, 37);
    client.closeInventory(0);
    peer.transport.disconnect();
    await peer.control("DISCONNECTED");
    peer.controls.length = 0;
    const secondEvents = [];
    const second = new ProtocolClient(peer.transport, (type, value) => {
      secondEvents.push(type);
      if (type === "chunk") second.loaded();
    });
    await peer.transport.connect("127.0.0.1", server.port);
    second.login(
      "127.0.0.1",
      server.port,
      "Integration",
      new Uint8Array(16).fill(11),
      2,
    );
    await until(() => secondEvents.includes("chunk"), "reconnect chunk");
    assert.ok(
      !peer.controls.some((c) => c.type === "ERROR"),
      JSON.stringify(peer.controls),
    );
  } finally {
    peer?.ws.terminate();
    await bridge.close();
    await server.stop();
  }
  assert.ok(
    !server.log.includes("Packet length discrepancy"),
    server.log.slice(-3000),
  );
});
