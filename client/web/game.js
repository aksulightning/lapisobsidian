// SPDX-License-Identifier: GPL-3.0-only
import { World, key } from "./world/world.js";
import { material } from "./world/blocks.js";
import { ProtocolClient } from "./protocol/client.js";
import { direction } from "./renderer.js";
export class Game {
  constructor(transport, renderer, settings, event) {
    this.transport = transport;
    this.renderer = renderer;
    this.settings = settings;
    this.event = event;
    this.world = new World();
    this.entities = new Map();
    this.generation = 0;
    this.worker = new Worker(new URL("./world/worker.js", import.meta.url), {
      type: "module",
    });
    this.worker.onmessage = ({ data: m }) => this.workerMessage(m);
    this.worker.onerror = () =>
      event("error", "World processing failed. Disconnect and try again.");
    this.reset();
  }
  post(type, fields = {}, transfer = []) {
    this.worker.postMessage(
      { type, generation: this.generation, ...fields },
      transfer,
    );
  }
  reset() {
    this.generation++;
    this.world.clear();
    this.entities.clear();
    this.renderer.clear();
    this.post("reset");
    this.player = {
      x: 8,
      y: 80,
      z: 8,
      yaw: 0,
      pitch: 0,
      vy: 0,
      grounded: false,
      sneaking: false,
    };
    this.playing = false;
    this.positioned = false;
    this.loaded = false;
    this.inventory = new Map();
    this.cursor = { count: 0, item: 0 };
    this.slot = 0;
    this.window = 0;
    this.mode = 0;
    this.health = 20;
    this.sequence = 0;
    this.elapsed = 0;
    this.actionTime = 0;
    this.mine = null;
    this.inputFlags = -1;
    this.pendingChunks = 0;
    this.blockUpdates = 0;
    this.center = { x: 0, z: 0 };
    this.entityId = 0;
  }
  async connect(host, port, name, uuid) {
    this.reset();
    this.protocol = new ProtocolClient(this.transport, (t, v) =>
      this.receive(t, v),
    );
    await this.transport.connect(host, port);
    this.protocol.login(host, port, name, uuid, this.settings.distance);
    this.event("status", "Authenticating...");
  }
  receive(type, value) {
    switch (type) {
      case "join":
        this.entityId = value.entity;
        this.mode = value.mode;
        this.serverDistance = value.distance;
        break;
      case "position": {
        const p = this.player;
        for (const [axis, flag] of [
          ["x", 1],
          ["y", 2],
          ["z", 4],
          ["yaw", 8],
          ["pitch", 16],
        ])
          p[axis] = (value.flags & flag ? p[axis] : 0) + value[axis];
        p.vy = value.vy;
        p.grounded = false;
        this.positioned = true;
        this.checkLoaded();
        break;
      }
      case "chunk":
        if (this.pendingChunks >= 64) throw Error("World data queue exceeded");
        this.pendingChunks++;
        this.post("chunk", { bytes: value }, [value.buffer]);
        break;
      case "block":
        this.blockUpdates++;
        this.world.set(value);
        this.post("block", { block: value });
        break;
      case "center":
        this.center = value;
        this.prune();
        break;
      case "unload":
        this.unload(value.x, value.z);
        break;
      case "slot": {
        let slots = this.inventory.get(value.window);
        if (!slots) {
          slots = new Map();
          this.inventory.set(value.window, slots);
        }
        slots.set(value.slot, value);
        break;
      }
      case "cursor":
        this.cursor = value;
        break;
      case "held":
        this.slot = value;
        break;
      case "inventory":
        this.window = value.window;
        break;
      case "mode":
        this.mode = value;
        break;
      case "health":
        this.health = value.health;
        break;
      case "abilities":
        this.abilities = value;
        break;
      case "entity":
        if (this.entities.size < 2048) this.entities.set(value.id, value);
        break;
      case "entityMove": {
        const e = this.entities.get(value.id);
        if (e) Object.assign(e, value);
        break;
      }
      case "entityRelative": {
        const e = this.entities.get(value.id);
        if (e) {
          e.x += value.dx;
          e.y += value.dy;
          e.z += value.dz;
          e.yaw = value.yaw;
          e.pitch = value.pitch;
        }
        break;
      }
      case "entityRemove":
        this.entities.delete(value);
        break;
      case "respawn": {
        this.world.clear();
        this.renderer.clear();
        this.entities.clear();
        this.generation++;
        this.post("reset");
        this.loaded = this.playing = this.positioned = false;
        this.pendingChunks = 0;
        this.blockUpdates = 0;
        break;
      }
    }
    if (type !== "packet" && type !== "chunk") this.event(type, value);
  }
  workerMessage(m) {
    if (m.generation !== this.generation) return;
    if (m.type === "chunk") {
      this.pendingChunks--;
      this.world.add(m);
      this.prune();
      this.checkLoaded();
    } else if (m.type === "mesh") {
      if (this.world.chunks.has(key(m.x, m.z))) this.renderer.update(m);
    } else if (m.type === "error") {
      this.event(
        "error",
        "Could not decode the server world. This client requires the matching Lapis Obsidian server.",
      );
      this.disconnect();
    }
  }
  checkLoaded() {
    if (
      !this.loaded &&
      this.positioned &&
      this.world.has(this.player.x, this.player.z)
    ) {
      this.loaded = this.playing = true;
      this.protocol.loaded();
      this.event("status", "Connected");
      this.event("ready");
    }
  }
  prune() {
    const radius =
      Math.max(this.settings.distance, this.serverDistance || 2) + 4;
    for (const c of this.world.chunks.values())
      if (
        Math.abs(c.x - this.center.x) > radius ||
        Math.abs(c.z - this.center.z) > radius
      )
        this.unload(c.x, c.z);
  }
  unload(x, z) {
    this.world.chunks.delete(key(x, z));
    this.renderer.remove(x, z);
    this.post("unload", { x, z });
  }
  disconnect() {
    this.playing = false;
    this.transport.disconnect();
  }
  select(slot) {
    if (!this.playing) return;
    this.slot = (slot + 9) % 9;
    this.protocol.select(this.slot);
    this.event("held", this.slot);
  }
  update(dt, input) {
    if (!this.playing || !this.transport.connected) return;
    const p = this.player;
    p.yaw = (p.yaw + input.lookX) % 360;
    p.pitch = Math.max(-89.9, Math.min(89.9, p.pitch + input.lookY));
    p.sneaking = input.sneak;
    const length = Math.max(1, Math.hypot(input.forward, input.strafe)),
      yaw = (p.yaw * Math.PI) / 180,
      speed = input.sneak ? 1.3 : 4.317;
    if (this.health > 0 && this.world.has(p.x, p.z)) {
      const water = material(
        this.world.get(Math.floor(p.x), Math.floor(p.y + 0.5), Math.floor(p.z)),
      ).fluid;
      if (input.jump && (p.grounded || water)) p.vy = water ? 3 : 8.4;
      p.vy = Math.max(-50, p.vy - (water ? 5 : 26) * dt);
      const dx =
          ((-Math.sin(yaw) * input.forward - Math.cos(yaw) * input.strafe) /
            length) *
          speed *
          dt,
        dz =
          ((Math.cos(yaw) * input.forward - Math.sin(yaw) * input.strafe) /
            length) *
          speed *
          dt;
      const height = input.sneak ? 1.5 : 1.8;
      for (const [axis, delta] of [
        ["x", dx],
        ["z", dz],
        ["y", p.vy * dt],
      ]) {
        if (axis === "y") p.grounded = false;
        const steps = Math.max(1, Math.ceil(Math.abs(delta) / 0.08));
        for (let i = 0; i < steps; i++) {
          const next = { ...p, [axis]: p[axis] + delta / steps };
          if (!this.world.has(next.x, next.z)) break;
          if (
            axis !== "y" &&
            input.sneak &&
            p.vy <= 0 &&
            !this.world.collides(next.x, next.y - 0.08, next.z, height)
          )
            break;
          if (this.world.collides(next.x, next.y, next.z, height)) {
            if (axis === "y") {
              if (delta < 0) p.grounded = true;
              p.vy = 0;
            } else if (
              this.world.collides(p.x, p.y - 0.06, p.z, height) &&
              !this.world.collides(next.x, next.y + 0.55, next.z, height)
            ) {
              p[axis] = next[axis];
              p.y += 0.55;
            }
            break;
          }
          p[axis] = next[axis];
        }
      }
    }
    this.elapsed += dt;
    this.actionTime += dt;
    if (this.elapsed >= 0.05) {
      this.elapsed %= 0.05;
      this.protocol.move(p);
      const flags =
        (input.forward > 0 ? 1 : 0) |
        (input.forward < 0 ? 2 : 0) |
        (input.strafe < 0 ? 4 : 0) |
        (input.strafe > 0 ? 8 : 0) |
        (input.jump ? 16 : 0) |
        (input.sneak ? 32 : 0);
      if (flags !== this.inputFlags) {
        this.inputFlags = flags;
        this.protocol.input(flags);
      }
    }
    const origin = { x: p.x, y: p.y + (p.sneaking ? 1.35 : 1.62), z: p.z },
      dir = direction(p.yaw, p.pitch),
      target = this.world.ray(origin, dir);
    this.target = target;
    if (input.primary && this.health > 0) {
      let entity = null,
        nearest = target?.distance || 4.5;
      for (const e of this.entities.values()) {
        const dx = e.x - origin.x,
          dy = e.y + 0.7 - origin.y,
          dz = e.z - origin.z,
          t = dx * dir.x + dy * dir.y + dz * dir.z;
        if (
          t > 0 &&
          t < nearest &&
          Math.hypot(dx - dir.x * t, dy - dir.y * t, dz - dir.z * t) < 0.65
        ) {
          entity = e;
          nearest = t;
        }
      }
      if (entity) {
        if (this.actionTime > 0.6) {
          this.protocol.attack(entity.id, input.sneak);
          this.protocol.swing();
          this.actionTime = 0;
        }
      } else if (target) {
        const k = `${target.x},${target.y},${target.z}`;
        if (this.mine?.key !== k) {
          this.protocol.action(0, target);
          this.protocol.swing();
          this.mine = { key: k, target, time: 0 };
        }
        this.mine.time += dt;
        if (this.mine.time > (this.mode === 1 ? 0.18 : 0.65)) {
          if (this.mode !== 1) this.protocol.action(2, target);
          this.mine = null;
        }
      }
    } else if (this.mine) {
      this.protocol.action(1, this.mine.target);
      this.mine = null;
    }
    if (input.secondary && this.health > 0 && this.actionTime > 0.25) {
      this.protocol.use(target, p.yaw, p.pitch);
      this.actionTime = 0;
    }
  }
}
