// Original regression tests. SPDX-License-Identifier: MIT
import test from "node:test";
import assert from "node:assert/strict";
import { Actions, miningTime } from "../adapter/actions.mjs";
import {
  spawnEntity,
  moveEntity,
  tickRemote,
  metadata,
  pickEntity,
  kinds,
} from "../adapter/entities.mjs";
import { Payload, Cursor } from "../adapter/wire.mjs";
import { canonicalSlot } from "../adapter/inventory.mjs";
import { synthesize } from "../adapter/audio.mjs";
import { items, stateToBlock, registerBlocks } from "../adapter/registry.mjs";
import { BlockRegistry } from "../src/js/net/minecraft/client/world/block/BlockRegistry.js";
import Block from "../src/js/net/minecraft/client/world/block/Block.js";
import SoundManager from "../src/js/net/minecraft/client/sound/SoundManager.js";
import Vector3 from "../src/js/net/minecraft/util/Vector3.js";
import Connection from "../adapter/connection.mjs";
const itemId = (name) =>
  Number(Object.entries(items).find(([, n]) => n === name)[0]);
function fixture() {
  let hit = {
    x: 0,
    y: 5,
    z: 2,
    face: { x: 0, y: 0, z: -1 },
    vector: new Vector3(0, 5, 2),
  };
  const sent = [],
    player = {
      health: 20,
      food: 12,
      onGround: true,
      ticksExisted: 1,
      inventory: { selectedSlotIndex: 0 },
      getPositionEyes: () => new Vector3(0, 6, 0),
      getLook: () => new Vector3(0, 0, 1),
      rayTrace: () => hit,
      isInWater: () => false,
      isSneaking: () => false,
      swingArm() {},
    };
  const app = {
    player,
    timer: { partialTicks: 1 },
    world: { getBlockAt: () => 3 },
    hasInGameFocus: () => true,
  };
  const c = {
    app,
    ready: true,
    mode: 0,
    slots: new Map(),
    entities: new Map(),
    sequence: 0,
    syncSelection() {},
    send(id, fill) {
      const p = new Payload();
      fill?.(p);
      sent.push([id, new Cursor(p.finish())]);
    },
  };
  const actions = new Actions(c);
  c.actions = actions;
  return { actions, c, app, player, sent, setHit: (h) => (hit = h) };
}
const actionsSent = (f) =>
  f.sent.filter(([id]) => id === 0x28).map(([, p]) => new Cursor(p.data).vi());
test("held survival digging starts once, releases/cancels and never finishes after release", () => {
  const f = fixture();
  f.actions.press(0);
  const start = f.actions.dig.start;
  f.actions.press(0);
  f.actions.tick(start + 250);
  assert.deepEqual(actionsSent(f), [0]);
  f.actions.release(0);
  f.actions.tick(start + 10000);
  assert.deepEqual(actionsSent(f), [0, 1]);
  f.actions.press(0);
  const next = f.actions.dig.start;
  f.actions.tick(next + 1000);
  assert.deepEqual(actionsSent(f), [0, 1, 0, 2]);
  f.actions.tick(next + 2000);
  assert.deepEqual(actionsSent(f), [0, 1, 0, 2]);
});
test("changing aim or tool cancels previous mining and durations depend on material", () => {
  const f = fixture();
  f.actions.press(0);
  f.setHit({ x: 1, y: 5, z: 2, face: { z: -1 }, vector: new Vector3(1, 5, 2) });
  f.actions.tick();
  assert.deepEqual(actionsSent(f), [0, 1, 0]);
  f.c.slots.set("0:36", { count: 1, item: itemId("iron_pickaxe") });
  f.actions.tick();
  assert.deepEqual(actionsSent(f), [0, 1, 0, 1, 0]);
  assert.ok(miningTime("stone", "iron_pickaxe") < miningTime("stone"));
  assert.equal(miningTime("bedrock"), Infinity);
  assert.equal(miningTime("water_4"), Infinity);
  assert.equal(miningTime("oak_leaves", "shears"), 0);
});
test("holding food starts use once; release, death and focus loss cancel server eating", () => {
  const f = fixture();
  f.c.slots.set("0:36", { count: 3, item: itemId("bread") });
  f.actions.press(2);
  for (let n = 0; n < 12; n++) {
    f.actions.press(2);
    f.actions.tick(performance.now() + n * 250);
  }
  assert.equal(f.sent.filter(([id]) => id === 0x40).length, 1);
  assert.equal(f.actions.using, true);
  f.actions.release(2);
  assert.deepEqual(actionsSent(f), [5]);
  f.actions.next = 0;
  f.actions.press(2);
  f.player.health = 0;
  f.actions.tick();
  assert.equal(f.actions.using, false);
  assert.equal(f.actions.held.size, 0);
  f.player.health = 20;
  f.actions.press(0);
  f.app.hasInGameFocus = () => false;
  f.actions.tick();
  assert.equal(f.actions.dig, null);
});
test("entities retain distinct types, interpolate accumulated wire deltas and decode item metadata", () => {
  for (const type of Object.keys(kinds).map(Number)) {
    const e = spawnEntity({ world: {} }, -2, type, 1, 2, 3, 90, 0);
    assert.equal(e.kind, kinds[type][0]);
    assert.equal(e.x, 1);
    moveEntity(e, 2, 2, 3, 180, 10);
    moveEntity(e, e.wirePosition[0] + 1, 2, 3, 180, 10);
    for (let i = 0; i < 3; i++) tickRemote.call(e);
    assert.equal(e.x, 3);
    assert.equal(e.rotationYaw, 180);
    assert.ok(Math.abs(e.prevX - 7 / 3) < 1e-9);
  }
  const e = spawnEntity({ world: {} }, -20, 69, 0, 0, 0, 0, 0);
  const data = new Payload()
    .u8(5)
    .vi(8)
    .u8(1)
    .u8(8)
    .vi(7)
    .vi(3)
    .vi(itemId("bread"))
    .vi(0)
    .vi(0)
    .u8(255);
  metadata(e, new Cursor(data.finish()));
  assert.equal(e.itemStack.count, 3);
  assert.equal(e.itemStack.item, itemId("bread"));
});
test("entity attack ray is occluded by blocks and excludes dropped items", () => {
  const f = fixture(),
    e = spawnEntity({ world: {} }, -2, 28, 0, 5, 2, 0, 0);
  f.c.entities.set(e.id, e);
  assert.equal(pickEntity(f.player, f.c.entities, null, 1), e);
  assert.equal(
    pickEntity(f.player, f.c.entities, { vector: new Vector3(0, 6, 1) }, 1),
    null,
  );
  f.setHit(null);
  f.actions.press(0);
  assert.equal(f.sent[0][0], 0x19);
  assert.equal(f.sent[0][1].vi(), -2);
  assert.equal(f.sent[0][1].vi(), 1);
});
test("audio plays first occurrence, sets actual playback pitch, and synthesizes audible distinct signals", () => {
  const manager = new SoundManager();
  manager.audioListener = { context: { resume: () => Promise.resolve() } };
  let played = 0,
    rate;
  manager.loadSoundPool = (name) =>
    (manager.soundPool[name] = [
      {
        isPlaying: false,
        position: { set() {} },
        updateMatrixWorld() {},
        setVolume() {},
        setPlaybackRate(v) {
          rate = v;
        },
        play() {
          played++;
        },
      },
    ]);
  manager.playSound("minecraft:block.note_block.harp", 0, 0, 0, 1, 2);
  assert.equal(played, 1);
  assert.equal(rate, 2);
  const harp = synthesize("minecraft:block.note_block.harp", 24000),
    cow = synthesize("minecraft:entity.cow.ambient", 24000);
  for (const data of [harp, cow]) {
    assert.ok(data.every(Number.isFinite));
    assert.ok(data.some((v) => Math.abs(v) > 0.1));
    assert.ok(data.every((v) => Math.abs(v) < 1));
  }
  assert.notDeepEqual(harp, cow);
});
test("container slots map back to canonical hotbar and all flowing water remains non-solid", () => {
  assert.equal(canonicalSlot(2, 54), 36);
  assert.equal(canonicalSlot(12, 37), 36);
  assert.equal(canonicalSlot(14, 30), 36);
  assert.equal(canonicalSlot(12, 0), null);
  BlockRegistry.create();
  registerBlocks();
  for (let state = 86; state <= 93; state++) {
    assert.equal(stateToBlock.get(state), 9);
    assert.equal(Block.getById(stateToBlock.get(state)).isSolid(), false);
  }
});
test("server inline sound and eating completion packets reach the client", () => {
  const calls = [],
    app = {
      player: { id: 7, x: 1, y: 2, z: 3 },
      soundManager: {
        playSound(...args) {
          calls.push(args);
        },
      },
    };
  const c = new Connection(app);
  c.phase = "play";
  c.actions.using = true;
  c.receive(
    0x6e,
    new Cursor(
      new Payload()
        .vi(0)
        .str("minecraft:entity.cow.ambient")
        .u8(0)
        .vi(6)
        .i32(-12)
        .i32(16)
        .i32(28)
        .f32(0.8)
        .f32(0.5)
        .u64(0)
        .finish(),
    ),
  );
  assert.deepEqual(calls[0].slice(0, 4), [
    "minecraft:entity.cow.ambient",
    -1.5,
    2,
    3.5,
  ]);
  assert.equal(calls[0][5], 0.5);
  c.receive(0x1e, new Cursor(new Payload().i32(7).u8(9).finish()));
  assert.equal(c.actions.using, false);
  assert.equal(calls[1][0], "player.eat");
});
