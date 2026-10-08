// Original server entity integration, MIT. Procedural model designs: CC0-1.0.
// IDs and metadata are verified against src/{mob_packets,items,procedures}.c.
import * as THREE from "../libraries/three.module.js";
import Entity from "../src/js/net/minecraft/client/entity/Entity.js";
import PlayerEntity from "../src/js/net/minecraft/client/entity/PlayerEntity.js";
import Block from "../src/js/net/minecraft/client/world/block/Block.js";
import { itemToBlock, items } from "./registry.mjs";
import { stack } from "./wire.mjs";

export const kinds = {
  6: ["arrow", 0.25, 0.25],
  25: ["chicken", 0.4, 0.7],
  28: ["cow", 0.9, 1.4],
  30: ["creeper", 0.6, 1.7],
  55: ["ghast", 4, 4],
  69: ["item", 0.3, 0.3],
  50: ["fireball", 0.5, 0.5],
  95: ["pig", 0.9, 0.9],
  106: ["sheep", 0.9, 1.3],
  110: ["skeleton", 0.6, 1.99],
  119: ["spider", 1.4, 0.9],
  145: ["zombie", 0.6, 1.95],
  148: ["zombie_pigman", 0.6, 1.95],
  149: ["player", 0.6, 1.8],
};
const wrap = (n) => ((((n + 540) % 360) + 360) % 360) - 180;
export function moveEntity(
  entity,
  x,
  y,
  z,
  yaw = entity.rotationYaw,
  pitch = entity.rotationPitch,
) {
  entity.wirePosition = [x, y, z];
  entity.target = { x, y, z, yaw, pitch, ticks: 3 };
}
export function tickRemote() {
  this.onEntityUpdate();
  this.prevRenderYawOffset = this.renderYawOffset;
  this.prevRotationYawHead = this.rotationYawHead;
  this.prevLimbSwingStrength = this.limbSwingStrength || 0;
  this.prevSwingProgress = this.swingProgress || 0;
  if (this.target?.ticks > 0) {
    const t = this.target,
      n = t.ticks--;
    this.setPosition(
      this.x + (t.x - this.x) / n,
      this.y + (t.y - this.y) / n,
      this.z + (t.z - this.z) / n,
    );
    this.setRotation(
      this.rotationYaw + wrap(t.yaw - this.rotationYaw) / n,
      this.rotationPitch + (t.pitch - this.rotationPitch) / n,
    );
  }
  this.renderYawOffset = this.rotationYaw;
  this.rotationYawHead = this.headYaw ?? this.rotationYaw;
  this.limbSwingStrength = Math.min(
    1,
    Math.hypot(this.x - this.prevX, this.z - this.prevZ) * 8,
  );
  this.limbSwingProgress =
    (this.limbSwingProgress || 0) + this.limbSwingStrength;
  if (this.isSwingInProgress) this.updateArmSwingProgress();
  if (this.hurtTicks > 0) this.hurtTicks--;
}

export class ServerEntity extends Entity {
  constructor(app, world, id, type) {
    super(app, world, id);
    this.type = type;
    [this.kind, this.width, this.height] = kinds[type] || ["unknown", 0.6, 0.6];
    this.renderYawOffset = this.rotationYawHead = 0;
  }
  initRenderer() {
    this.renderer = new ServerRenderer(this);
  }
  onUpdate() {
    tickRemote.call(this);
  }
}

export function spawnEntity(app, id, type, x, y, z, yaw, pitch) {
  const entity =
    type === 149
      ? new PlayerEntity(app, app.world, id)
      : new ServerEntity(app, app.world, id, type);
  entity.type = type;
  entity.kind = kinds[type]?.[0] || "unknown";
  entity.onUpdate = tickRemote;
  entity.setPositionAndRotation(x, y, z, yaw, pitch);
  entity.wirePosition = [x, y, z];
  entity.renderYawOffset = entity.prevRenderYawOffset = yaw;
  entity.rotationYawHead = entity.prevRotationYawHead = yaw;
  return entity;
}

export function metadata(entity, p) {
  for (let index = p.u8(); index !== 255; index = p.u8()) {
    const type = p.vi();
    let value;
    if (type === 0 || type === 8) value = p.u8();
    else if (type === 1 || type === 21) value = p.vi();
    else if (type === 3) value = p.f32();
    else if (type === 7) value = stack(p);
    else return; // Unknown serializer: skip this packet, never guess its size.
    if (!entity) continue;
    entity.metaData[index] = { id: index, type, value };
    if (index === 8 && type === 7) entity.itemStack = value;
    if (index === 17 && entity.type === 106) entity.sheared = !!(value & 16);
    if (index === 16 && entity.type === 30) entity.primed = value > 0;
  }
}

class ServerRenderer {
  constructor(entity) {
    this.group = new THREE.Group();
    this.entity = entity;
    this.signature = "";
    this.legs = [];
  }
  dispose() {
    this.group.traverse((o) => {
      o.geometry?.dispose();
      if (o.material && !o.userData.sharedMaterial) o.material.dispose();
    });
    this.group.clear();
  }
  box(w, h, d, x, y, z, color) {
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(w, h, d),
      new THREE.MeshBasicMaterial({ color }),
    );
    mesh.position.set(x, y, z);
    this.group.add(mesh);
    return mesh;
  }
  rebuild(e) {
    this.dispose();
    this.legs = [];
    const box = (...args) => this.box(...args);
    if (e.kind === "item") {
      const block = Block.getById(itemToBlock.get(e.itemStack?.item));
      if (block) {
        const holder = new THREE.Group();
        this.group.add(holder);
        e.minecraft.worldRenderer.blockRenderer.renderBlockInHandThirdPerson(
          holder,
          block,
          1,
        );
        const mesh = holder.children[0];
        mesh.position.set(0, 0.16, 0);
        mesh.scale.set(0.28, 0.28, 0.28);
        // The upstream renderer shares the atlas material with terrain.
        holder.traverse((o) => {
          if (o.material) o.userData.sharedMaterial = true;
        });
      } else {
        const name = items[e.itemStack?.item] || "";
        if (/axe|shovel|hoe|sword/.test(name)) {
          box(0.04, 0.35, 0.04, 0, 0.16, 0, 0x9d7756);
          box(0.2, 0.07, 0.05, 0.04, 0.34, 0, 0x83beca);
        } else
          box(
            0.24,
            0.16,
            0.06,
            0,
            0.13,
            0,
            /apple|meat|beef|pork|chicken|mutton/.test(name)
              ? 0xce8365
              : 0xc3b36d,
          );
      }
      return;
    }
    if (e.kind === "arrow") {
      box(0.05, 0.05, 0.7, 0, 0.04, 0, 0xc9bda0);
      box(0.12, 0.12, 0.13, 0, 0.04, -0.37, 0x8c9fb2);
      return;
    }
    if (e.kind === "fireball") {
      box(0.4, 0.4, 0.4, 0, 0.2, 0, 0xee9b4e);
      return;
    }
    if (e.kind === "spider") {
      box(0.8, 0.4, 0.85, 0, 0.5, 0.15, 0x465566);
      box(0.55, 0.3, 0.4, 0, 0.5, -0.4, 0x667b89);
      for (let side of [-1, 1])
        for (let i = 0; i < 4; i++) {
          const leg = box(
            0.55,
            0.09,
            0.09,
            side * 0.55,
            0.3,
            -0.4 + i * 0.25,
            0x506475,
          );
          leg.rotation.z = side * 0.35;
          this.legs.push(leg);
        }
      return;
    }
    if (e.kind === "ghast") {
      box(3.4, 2.6, 3.4, 0, 2.7, 0, 0xc5d6e5);
      for (let x = -1; x <= 1; x++)
        for (let z = -1; z <= 1; z++)
          this.legs.push(box(0.3, 1.3, 0.3, x, 0.7, z, 0x819daf));
      box(1.4, 0.25, 0.05, 0, 2.7, -1.72, 0x455368);
      return;
    }
    const quadruped = ["cow", "pig", "sheep"].includes(e.kind);
    if (quadruped) {
      const color = {
        cow: 0x9d856b,
        pig: 0xcd9995,
        sheep: e.sheared ? 0xbaa895 : 0xd6d4bf,
      }[e.kind];
      box(0.7, 0.55, 1.05, 0, e.height * 0.63, 0, color);
      box(0.48, 0.46, 0.46, 0, e.height * 0.8, -0.6, color);
      for (let x of [-0.25, 0.25])
        for (let z of [-0.35, 0.35])
          this.legs.push(
            box(0.18, e.height * 0.42, 0.18, x, e.height * 0.21, z, 0x665d59),
          );
      if (e.kind === "cow")
        for (let x of [-0.2, 0.2])
          box(0.08, 0.2, 0.08, x, e.height * 0.99, -0.6, 0xddceaa);
      if (e.kind === "pig")
        box(0.28, 0.16, 0.12, 0, e.height * 0.76, -0.88, 0xac777a);
    } else if (e.kind === "chicken") {
      box(0.4, 0.38, 0.45, 0, 0.36, 0, 0xd5d9cd);
      box(0.24, 0.25, 0.24, 0, 0.6, -0.18, 0xe9e2c8);
      box(0.15, 0.08, 0.16, 0, 0.59, -0.35, 0xd3a04d);
      for (let x of [-0.1, 0.1])
        this.legs.push(box(0.06, 0.2, 0.06, x, 0.1, 0, 0xc59650));
    } else if (e.kind === "creeper") {
      box(0.5, 0.75, 0.4, 0, 0.95, 0, 0x749c83);
      box(0.6, 0.5, 0.5, 0, 1.55, 0, 0x92b995);
      for (let x of [-0.18, 0.18])
        for (let z of [-0.2, 0.2])
          this.legs.push(box(0.23, 0.5, 0.23, x, 0.25, z, 0x4f786d));
    } else if (["zombie", "zombie_pigman", "skeleton"].includes(e.kind)) {
      const c = {
          zombie: 0x809b92,
          zombie_pigman: 0xb59789,
          skeleton: 0xc6cbb9,
        }[e.kind],
        thin = e.kind === "skeleton";
      box(thin ? 0.28 : 0.5, 0.65, 0.28, 0, 1.1, 0, c);
      box(0.45, 0.45, 0.45, 0, 1.7, 0, c);
      for (let x of [-0.17, 0.17])
        this.legs.push(box(thin ? 0.12 : 0.2, 0.75, 0.18, x, 0.38, 0, c));
      for (let x of [-0.37, 0.37]) box(0.13, 0.15, 0.65, x, 1.25, -0.28, c);
      if (e.equipment?.count) box(0.06, 0.65, 0.12, 0.45, 1.1, -0.45, 0xa28b60);
    } else box(0.5, 0.5, 0.5, 0, 0.25, 0, 0xa789bb);
    if (e.kind !== "unknown")
      for (let x of [-0.12, 0.12])
        box(
          0.07,
          0.07,
          0.04,
          x,
          e.height * 0.9,
          -(quadruped ? 0.85 : e.kind === "chicken" ? 0.31 : 0.26),
          0x293844,
        );
  }
  render(e, partial) {
    const signature = JSON.stringify([
      e.kind,
      e.itemStack,
      e.sheared,
      e.equipment,
    ]);
    if (signature !== this.signature) {
      this.signature = signature;
      this.rebuild(e);
    }
    this.group.position.set(
      e.prevX + (e.x - e.prevX) * partial,
      e.prevY + (e.y - e.prevY) * partial,
      e.prevZ + (e.z - e.prevZ) * partial,
    );
    this.group.rotation.y = ((180 - e.rotationYaw) * Math.PI) / 180;
    if (e.kind === "item") {
      this.group.position.y +=
        0.12 + Math.sin((e.ticksExisted + partial) * 0.12) * 0.04;
      this.group.rotation.y = (e.ticksExisted + partial) * 0.04;
    }
    if (e.kind === "arrow")
      this.group.rotation.x = (e.rotationPitch * Math.PI) / 180;
    const pulse = e.primed ? 1 + Math.sin(e.ticksExisted * 1.4) * 0.07 : 1;
    this.group.scale.setScalar(pulse);
    this.legs.forEach(
      (leg, i) =>
        (leg.rotation.x =
          Math.sin((e.limbSwingProgress || 0) * 0.7 + i * Math.PI) *
          0.3 *
          (e.limbSwingStrength || 0)),
    );
    this.group.traverse((o) => {
      if (o.material && !o.userData.sharedMaterial) {
        if (!o.userData.baseColor)
          o.userData.baseColor = o.material.color.clone();
        o.material.color.copy(o.userData.baseColor);
        if (e.hurtTicks > 0)
          o.material.color.lerp(new THREE.Color(0xef6b62), 0.55);
      }
    });
  }
}

// Slab ray/AABB intersection; terrain hit distance prevents attacks through walls.
export function pickEntity(player, entities, blockHit, ticks) {
  const eye = player.getPositionEyes(ticks),
    dir = player.getLook(ticks);
  let distance = Math.min(
      4,
      blockHit?.vector ? eye.distanceTo(blockHit.vector) : 4,
    ),
    found = null;
  for (const e of entities.values()) {
    if (["item", "arrow", "fireball"].includes(e.kind)) continue;
    let near = 0,
      far = distance;
    for (const axis of ["x", "y", "z"]) {
      const lo = e.boundingBox["min" + axis.toUpperCase()] - 0.08,
        hi = e.boundingBox["max" + axis.toUpperCase()] + 0.08;
      if (Math.abs(dir[axis]) < 1e-8) {
        if (eye[axis] < lo || eye[axis] > hi) {
          far = -1;
          break;
        }
      } else {
        const a = (lo - eye[axis]) / dir[axis],
          b = (hi - eye[axis]) / dir[axis];
        near = Math.max(near, Math.min(a, b));
        far = Math.min(far, Math.max(a, b));
      }
    }
    if (far >= near && near < distance) {
      distance = near;
      found = e;
    }
  }
  return found;
}
