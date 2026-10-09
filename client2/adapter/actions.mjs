// Original held-action state machine. SPDX-License-Identifier: MIT
import { items, nameToBlock } from "./registry.mjs";
import { pickEntity } from "./entities.mjs";
const names = new Map(Array.from(nameToBlock, ([name, id]) => [id, name]));
export const isFood = (name) =>
  /^(apple|bread|chicken|beef|porkchop|mutton|cooked_chicken|cooked_beef|cooked_porkchop|cooked_mutton|rotten_flesh)$/.test(
    name || "",
  );
export function miningTime(
  name,
  tool = "",
  grounded = true,
  underwater = false,
) {
  if (!name || /^(air|bedrock|water.*|lava.*)$/.test(name)) return Infinity;
  if (
    /torch|flower|dandelion|poppy|mushroom|fern|short_grass|dead_bush|sapling|lily_pad|lever|wheat/.test(
      name,
    )
  )
    return 0;
  if (
    (/snow/.test(name) &&
      /^(stone|iron|golden|diamond|netherite)_shovel$/.test(tool)) ||
    (/leaves/.test(name) && tool === "shears")
  )
    return 0;
  let hardness = 0.6,
    category = "shovel",
    needsTool = false;
  if (
    /stone|ore|brick|furnace|obsidian|iron_block|gold_block|lapis_block|diamond_block/.test(
      name,
    )
  ) {
    hardness = /obsidian/.test(name)
      ? 50
      : /ore/.test(name)
        ? 3
        : /cobblestone|brick/.test(name)
          ? 2
          : 1.5;
    category = "pickaxe";
    needsTool = true;
  } else if (
    /log|planks|wood|chest|crafting|bookshelf|door|trapdoor|note_block|jukebox|sign/.test(
      name,
    )
  ) {
    hardness = /chest|crafting/.test(name) ? 2.5 : 2;
    category = "axe";
  } else if (/leaves|glass/.test(name)) {
    hardness = 0.3;
    category = "shears";
  } else if (/wool/.test(name)) {
    hardness = 0.8;
    category = "shears";
  } else if (/sand|dirt/.test(name)) hardness = 0.5;
  const correct = tool.endsWith("_" + category) || tool === category;
  const speed = correct
    ? { wooden: 2, stone: 4, iron: 6, golden: 12, diamond: 8, netherite: 9 }[
        tool.split("_")[0]
      ] || 5
    : 1;
  const harvest =
    !needsTool ||
    (correct && (!/obsidian/.test(name) || /diamond|netherite/.test(tool)));
  return (
    Math.ceil(
      ((hardness * (harvest ? 30 : 100)) / speed) *
        (grounded ? 1 : 5) *
        (underwater ? 5 : 1),
    ) * 50
  );
}
const faceId = (face) =>
  face.y === 1
    ? 1
    : face.y === -1
      ? 0
      : face.z === -1
        ? 2
        : face.z === 1
          ? 3
          : face.x === -1
            ? 4
            : 5;
const same = (a, b) => a && b && a.x === b.x && a.y === b.y && a.z === b.z;
export class Actions {
  constructor(connection) {
    this.c = connection;
    this.held = new Set();
    this.next = 0;
    this.progress = 0;
  }
  press(button) {
    if (this.held.has(button)) return;
    this.held.add(button);
    this.tick();
  }
  release(button) {
    this.held.delete(button);
    if (button === 0) this.cancelDig();
    if (button === 2 && this.using) {
      this.digPacket(5, { x: 0, y: 0, z: 0, face: 0 });
      this.using = false;
    }
  }
  cancel() {
    this.release(0);
    this.release(2);
    this.held.clear();
    this.next = 0;
  }
  digPacket(status, hit) {
    this.c.send(0x28, (p) =>
      p
        .vi(status)
        .pos(hit.x, hit.y, hit.z)
        .u8(typeof hit.face === "number" ? hit.face : faceId(hit.face))
        .vi(++this.c.sequence),
    );
  }
  cancelDig() {
    if (this.dig && !this.dig.finished) this.digPacket(1, this.dig.hit);
    this.dig = null;
    this.progress = 0;
  }
  tick(now = performance.now()) {
    const c = this.c,
      app = c.app,
      player = app.player;
    if (
      !c.ready ||
      !player ||
      player.health <= 0 ||
      !app.hasInGameFocus() ||
      c.mode === 3
    ) {
      this.cancel();
      return;
    }
    c.syncSelection();
    const selected = player.inventory.selectedSlotIndex,
      held = c.slots.get(`0:${36 + selected}`),
      tool = items[held?.item] || "";
    if (this.slot !== selected || this.tool !== tool) {
      this.cancelDig();
      if (this.using) this.release(2);
      this.slot = selected;
      this.tool = tool;
    }
    const hit = player.rayTrace(c.mode === 1 ? 5 : 4.5, app.timer.partialTicks);
    if (this.held.has(0)) {
      const entity = pickEntity(
        player,
        c.entities,
        hit,
        app.timer.partialTicks,
      );
      if (entity) {
        this.cancelDig();
        if (now >= this.next) {
          player.swingArm();
          c.send(0x19, (p) =>
            p
              .vi(entity.id)
              .vi(1)
              .u8(player.isSneaking() ? 1 : 0),
          );
          this.next = now + 500;
        }
        return;
      }
      if (c.mode === 2 || !hit) {
        this.cancelDig();
        return;
      }
      const block = app.world.getBlockAt(hit.x, hit.y, hit.z);
      if (this.dig && (!same(this.dig.hit, hit) || this.dig.block !== block))
        this.cancelDig();
      if (!this.dig && now >= this.next) {
        const duration =
          c.mode === 1
            ? 0
            : miningTime(
                names.get(block),
                tool,
                player.onGround,
                player.isInWater(),
              );
        if (!Number.isFinite(duration)) return;
        this.dig = { hit, block, start: now, duration };
        player.swingArm();
        this.digPacket(0, hit);
        if (c.mode === 1 || duration === 0) {
          this.dig.finished = true;
          this.next = now + 250;
        }
      }
      if (this.dig && !this.dig.finished) {
        this.progress = Math.min(1, (now - this.dig.start) / this.dig.duration);
        if (player.ticksExisted % 5 === 0) player.swingArm();
        if (this.progress >= 1) {
          this.digPacket(2, this.dig.hit);
          this.dig.finished = true;
          this.next = now + 150;
        }
      }
    } else if (this.held.has(2) && now >= this.next && !this.using) {
      const entity = pickEntity(
        player,
        c.entities,
        hit,
        app.timer.partialTicks,
      );
      if (entity) {
        c.send(0x19, (p) =>
          p
            .vi(entity.id)
            .vi(0)
            .vi(0)
            .u8(player.isSneaking() ? 1 : 0),
        );
        this.next = now + 300;
        return;
      }
      const useAir =
        isFood(tool) || /bucket|helmet|chestplate|leggings|boots/.test(tool);
      if (isFood(tool) && (player.food >= 20 || c.mode === 1)) return;
      if (hit && !useAir)
        c.send(0x3f, (p) =>
          p
            .vi(0)
            .pos(hit.x, hit.y, hit.z)
            .vi(faceId(hit.face))
            .f32(
              Math.max(0, Math.min(1, (hit.vector?.x ?? hit.x + 0.5) - hit.x)),
            )
            .f32(
              Math.max(0, Math.min(1, (hit.vector?.y ?? hit.y + 0.5) - hit.y)),
            )
            .f32(
              Math.max(0, Math.min(1, (hit.vector?.z ?? hit.z + 0.5) - hit.z)),
            )
            .u8(0)
            .u8(0)
            .vi(++c.sequence),
        );
      else
        c.send(0x40, (p) =>
          p
            .vi(0)
            .vi(++c.sequence)
            .f32(player.rotationYaw)
            .f32(player.rotationPitch),
        );
      this.using = isFood(tool);
      this.next = now + 300;
    }
  }
}
