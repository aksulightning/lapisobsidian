// Real C server + production mode-2 adapter/actions, without a graphics driver.
// Original test harness, MIT. Rendering/audio output is tested separately in Chromium.
import assert from "node:assert/strict";
import net from "node:net";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, writeFile, rm, mkdir } from "node:fs/promises";
import { resolve } from "node:path";
import Connection from "../client2/adapter/connection.mjs";
import Inventory from "../client2/adapter/inventory.mjs";
import { Stream, frame, movement } from "../client2/adapter/wire.mjs";
import {
  items,
  nameToBlock,
  registerBlocks,
} from "../client2/adapter/registry.mjs";
import { BlockRegistry } from "../client2/src/js/net/minecraft/client/world/block/BlockRegistry.js";
import * as THREE from "../client2/libraries/three.module.js";
const pause = (ms) => new Promise((r) => setTimeout(r, ms));
const token = "mode2-test-only-admin-token-123456";
async function port() {
  const s = net.createServer().listen(0, "127.0.0.1");
  await once(s, "listening");
  const p = s.address().port;
  await new Promise((r) => s.close(r));
  return p;
}
const native = await port(),
  web = await port();
await mkdir(".tests", { recursive: true });
const cwd = await mkdtemp(resolve(".tests/mode2-survival-"));
await writeFile(
  `${cwd}/server.txt`,
  `port=${native}\nweb-port=${web}\nweb-address=127.0.0.1\ngamemode=creative\n`,
);
const server = spawn(resolve(process.argv[2] || "lapis-obsidian"), [], {
  cwd,
  env: {
    ...process.env,
    LAPIS_ADMIN_TOKEN: token,
    LAPIS_OBSIDIAN_CLIENT2_DIR: resolve("client2/dist"),
  },
  stdio: ["pipe", "pipe", "pipe"],
});
let logs = "",
  error;
server.stdout.on("data", (b) => (logs += b));
server.stderr.on("data", (b) => (logs += b));
async function wait(fn, label, timeout = 20000) {
  const end = Date.now() + timeout;
  while (!fn()) {
    if (error) throw error;
    if (Date.now() > end) throw Error(`Timed out: ${label}`);
    await pause(20);
  }
}
const sounds = [],
  chat = [],
  sent = [],
  received = new Map();
const app = {
  settings: { ambientOcclusion: false, viewDistance: 2 },
  timer: { partialTicks: 1 },
  loadingScreen: null,
  worldRenderer: {
    rebuildAll() {},
    flushRebuild: false,
    entityRenderManager: {
      createEntityRendererByEntity: () => ({ group: new THREE.Group() }),
    },
  },
  itemRenderer: { scheduleDirty() {} },
  particleRenderer: { spawnBlockBreakParticle() {} },
  soundManager: {
    playSound(...args) {
      sounds.push(args);
    },
  },
  ingameOverlay: {
    chatOverlay: {
      addMessage(m) {
        chat.push(m);
      },
    },
    playerListOverlay: {},
  },
  hasInGameFocus() {
    return !this.currentScreen;
  },
  displayScreen(screen) {
    this.currentScreen = screen;
  },
  loadWorld(world) {
    if (this.world) clearInterval(this.world.lightTimer);
    this.world = world;
    if (world) {
      clearInterval(world.lightTimer);
      this.player = this.playerController.createPlayer(world);
      this.player.onGround = true;
    }
  },
};
BlockRegistry.create();
registerBlocks();
const connection = new Connection(app);
app.lapisConnection = connection;
let socket, ticker;
const item = (name) =>
  Number(Object.entries(items).find(([, n]) => n === name)[0]);
async function command(text) {
  const before = chat.length;
  connection.send(6, (p) => p.str(text));
  await wait(() => chat.length > before, text);
  assert.doesNotMatch(chat.at(-1), /denied|Cannot|Invalid|permission|failed/i);
}
async function give(name, slot = 36, count = 4) {
  connection.send(0x37, (p) =>
    p.u16(slot).vi(count).vi(item(name)).vi(0).vi(0),
  );
  await wait(
    () => connection.slots.get(`0:${slot}`)?.item === item(name),
    `give ${name}`,
  );
}
function aim(x, y, z) {
  const p = app.player,
    dx = x - p.x,
    dy = y - p.y - p.getEyeHeight(),
    dz = z - p.z;
  p.setRotation(
    (-Math.atan2(dx, dz) * 180) / Math.PI,
    (-Math.atan2(dy, Math.hypot(dx, dz)) * 180) / Math.PI,
  );
}
try {
  await wait(() => logs.includes("Server listening"), "startup");
  socket = net.connect(web, "127.0.0.1");
  await once(socket, "connect");
  let buffer = Buffer.alloc(0),
    upgraded = false;
  const stream = new Stream((id, p) => {
    received.set(id, (received.get(id) || 0) + 1);
    connection.receive(id, p);
  });
  connection.socket = {
    readyState: 1,
    bufferedAmount: 0,
    close() {
      socket.end();
    },
    send(data) {
      const packet = Buffer.from(data),
        n = packet.length,
        h = Buffer.alloc(n < 126 ? 6 : 8),
        mask = Buffer.from([7, 11, 17, 23]);
      h[0] = 0x82;
      h[1] = 0x80 | (n < 126 ? n : 126);
      if (n >= 126) h.writeUInt16BE(n, 2);
      mask.copy(h, h.length - 4);
      for (let i = 0; i < n; i++) packet[i] ^= mask[i % 4];
      socket.write(Buffer.concat([h, packet]));
    },
  };
  const originalSend = connection.send.bind(connection);
  connection.send = (id, fn) => {
    sent.push(id);
    return originalSend(id, fn);
  };
  socket.on("error", (e) => (error = e));
  socket.on("data", (bytes) => {
    try {
      buffer = Buffer.concat([buffer, bytes]);
      if (!upgraded) {
        const end = buffer.indexOf("\r\n\r\n");
        if (end < 0) return;
        assert.match(
          buffer.subarray(0, end).toString(),
          /101 Switching Protocols/,
        );
        buffer = buffer.subarray(end + 4);
        upgraded = true;
        connection.send(0, (p) => p.vi(772).str("localhost").u16(web).vi(2));
        connection.send(0, (p) =>
          p.str("SurvivalTest").bytes(new Uint8Array(16).fill(17)),
        );
      }
      while (buffer.length >= 2) {
        let n = buffer[1] & 127,
          h = 2;
        if (n === 126) {
          if (buffer.length < 4) return;
          n = buffer.readUInt16BE(2);
          h = 4;
        }
        assert.notEqual(n, 127);
        if (buffer.length < h + n) return;
        const op = buffer[0] & 15,
          body = buffer.subarray(h, h + n);
        buffer = buffer.subarray(h + n);
        if (op === 2) stream.push(body);
      }
    } catch (e) {
      error = e;
    }
  });
  socket.write(
    `GET /ws HTTP/1.1\r\nHost: 127.0.0.1:${web}\r\nOrigin: http://127.0.0.1:${web}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n`,
  );
  await wait(
    () => connection.ready && app.world.getChunkProvider().chunks.size >= 25,
    "production adapter login/chunks",
  );
  await command(`admin ${token}`);
  const p = app.player;
  const x = Math.floor(p.x),
    y = Math.floor(p.y),
    z = Math.floor(p.z);
  ticker = setInterval(() => {
    try {
      connection.actions.tick();
      for (const e of connection.entities.values()) e.onUpdate();
    } catch (e) {
      error = e;
    }
  }, 50);
  // Seed fixtures through the real server's creative inventory, then use survival.
  await give("dirt");
  await give("bread", 37);
  await give("oak_log", 38);
  await give("note_block", 39);
  p.inventory.selectedSlotIndex = 0;
  connection.syncSelection();
  // Place a known dirt block within view, on top of the spawn ground.
  connection.send(0x3f, (w) =>
    w
      .vi(0)
      .pos(x, y - 1, z + 2)
      .vi(1)
      .f32(0.5)
      .f32(1)
      .f32(0.5)
      .u8(0)
      .u8(0)
      .vi(++connection.sequence),
  );
  await wait(
    () => app.world.getBlockAt(x, y, z + 2) === 3,
    "fixture placement",
  );
  await command("gamemode survival");
  await wait(() => connection.mode === 0, "survival mode");
  aim(x + 0.5, y + 0.5, z + 2.5);
  connection.actions.press(0);
  await pause(150);
  connection.actions.release(0);
  await pause(900);
  assert.equal(
    app.world.getBlockAt(x, y, z + 2),
    3,
    "released digging must not break later",
  );
  connection.actions.press(0);
  await wait(
    () => app.world.getBlockAt(x, y, z + 2) === 0,
    "held survival mining",
  );
  connection.actions.release(0);
  await wait(
    () =>
      [...connection.entities.values()].some(
        (e) => e.kind === "item" && e.itemStack?.item === item("dirt"),
      ),
    "dropped item metadata",
  );
  const drop = [...connection.entities.values()].find(
    (e) => e.kind === "item" && e.itemStack?.item === item("dirt"),
  );
  p.setPositionAndRotation(drop.x, drop.y, drop.z, 0, 0);
  connection.socket.send(movement(p));
  await wait(
    () => !connection.entities.has(drop.id),
    "authoritative item pickup",
  );
  assert.ok(sounds.some((s) => s[0] === "item.pickup"));
  // Exhaust saturation using the server's sprint/movement protocol in bounded batches.
  p.inventory.selectedSlotIndex = 1;
  connection.syncSelection();
  connection.send(0x29, (w) => w.vi(p.id).vi(1).vi(0));
  for (let batch = 0; batch < 28; batch++) {
    for (let i = 0; i < 100; i++) connection.socket.send(movement(p));
    await pause(30);
  }
  connection.send(0x29, (w) => w.vi(p.id).vi(2).vi(0));
  await wait(() => p.food < 20, "server hunger");
  const beforeFood = p.food,
    beforeCount = connection.slots.get("0:37").count;
  connection.actions.press(2);
  await pause(100);
  connection.actions.release(2);
  await pause(2100);
  assert.equal(
    connection.slots.get("0:37").count,
    beforeCount,
    "released eating cannot consume food",
  );
  connection.actions.press(2);
  await wait(
    () => connection.slots.get("0:37").count === beforeCount - 1,
    "held food consumption",
  );
  connection.actions.release(2);
  assert.ok(p.food > beforeFood);
  assert.ok(sounds.some((s) => s[0] === "player.eat"));
  // Survival crafting: one log -> four planks, right click places one ingredient.
  const gui = new Inventory(connection);
  gui.minecraft = app;
  app.displayScreen(gui);
  gui.clickSlot(38);
  await wait(() => connection.cursor?.item === item("oak_log"), "pick up logs");
  gui.clickSlot(1, 1);
  await wait(
    () => connection.slots.get("0:0")?.item === item("oak_planks"),
    "craft preview",
  );
  gui.clickSlot(38);
  await wait(() => !connection.cursor?.count, "return logs");
  gui.clickSlot(0);
  await wait(
    () => connection.cursor?.item === item("oak_planks"),
    "craft output",
  );
  gui.clickSlot(40);
  await wait(() => connection.slots.get("0:40")?.count === 4, "crafted stack");
  gui.onClose();
  app.displayScreen(null);
  // Note-block sound comes from the server's inline sound holder packet.
  p.inventory.selectedSlotIndex = 3;
  connection.syncSelection();
  p.setPositionAndRotation(x + 0.5, y, z + 0.5, 0, 0);
  connection.socket.send(movement(p));
  await pause(100);
  const bx = x,
    by = y,
    bz = z + 1;
  connection.send(0x3f, (w) =>
    w
      .vi(0)
      .pos(bx, by - 1, bz + 1)
      .vi(1)
      .f32(0.5)
      .f32(1)
      .f32(0.5)
      .u8(0)
      .u8(0)
      .vi(++connection.sequence),
  );
  await wait(
    () =>
      app.world.getBlockAt(bx, by, bz + 1) === nameToBlock.get("note_block"),
    "note block placement",
  );
  // Notes are intentionally silent if another block is directly above them.
  connection.send(0x28, (w) =>
    w
      .vi(2)
      .pos(bx, by + 1, bz + 1)
      .u8(1)
      .vi(++connection.sequence),
  );
  await wait(
    () => app.world.getBlockAt(bx, by + 1, bz + 1) === 0,
    "clear note overhead",
  );
  aim(bx + 0.5, by + 0.5, bz + 1.5);
  connection.actions.press(2);
  await wait(
    () => sounds.some((s) => s[0].includes("note_block")),
    "server note audio",
  );
  connection.actions.release(2);
  // Spawn on known nearby safe ground and validate type-specific entity handling.
  let spot;
  for (let dx = 2; dx <= 4 && !spot; dx++)
    for (let dz = 1; dz <= 4 && !spot; dz++) {
      let sy = 250;
      while (sy > 0 && (!app.world.getBlockAt(x + dx, sy, z + dz) || ['snow','moss_carpet','short_grass','fern'].some(name=>nameToBlock.get(name)===app.world.getBlockAt(x+dx,sy,z+dz)))) sy--;
      if(app.world.getBlockAt(x+dx,sy,z+dz)===9)continue;
      if (Math.abs(sy + 1 - y) < 4) spot = [x + dx, sy + 1, z + dz];
    }
  assert.ok(spot, "safe nearby mob spawn");
  await command(`spawnmob cow ${spot.join(" ")}`);
  await wait(
    () => [...connection.entities.values()].some((e) => e.kind === "cow"),
    "cow spawn",
  );
  const cow = [...connection.entities.values()].find((e) => e.kind === "cow");
  assert.equal(cow.constructor.name, "ServerEntity");
  cow.renderer.render(cow, 1);
  assert.ok(cow.renderer.group.children.length >= 8);
  // Attack using the production ray picker; observe server hurt sound and damage metadata.
  p.inventory.selectedSlotIndex = 8;
  connection.syncSelection();
  p.setPositionAndRotation(cow.x, cow.y, cow.z - 2.5, 0, 0);
  connection.socket.send(movement(p));
  aim(cow.x, cow.y + 0.8, cow.z);
  connection.actions.press(0);
  await wait(
    () => sounds.some((s) => s[0].includes("cow.hurt")),
    "entity attack and hurt sound",
  );
  connection.actions.release(0);
  assert.ok(received.get(0x5c) > 0);
  assert.ok(received.get(0x6e) > 0);
  console.log(
    "Mode 2 real server: production adapter, chunks, canceled/held survival mining, dropped-item metadata/pickup, canceled/held eating and hunger, right-click crafting, note audio, cow model and ray-targeted attack passed",
  );
} catch (e) {
  console.error(
    "Chat:",
    chat.slice(-8),
    "Actions:",
    {
      held: [...connection.actions.held],
      using: connection.actions.using,
      next: connection.actions.next,
      mode: connection.mode,
      ready: connection.ready,
    },
    "Sounds:",
    sounds.slice(-6),
    "Sent:",
    sent.slice(-15),
  );
  throw e;
} finally {
  clearInterval(ticker);
  connection.close();
  socket?.destroy();
  if (app.world) clearInterval(app.world.lightTimer);
  server.stdin.end("stop\n");
  const timer = setTimeout(() => server.kill(), 3000);
  if (server.exitCode === null) await once(server, "exit");
  clearTimeout(timer);
  if (error) console.error(error);
  await rm(cwd, { recursive: true, force: true });
}
