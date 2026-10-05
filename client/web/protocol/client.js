// SPDX-License-Identifier: GPL-3.0-only
// Wire layouts are traced to testing e96af887: src/main.c, packets.c,
// command_packets.c, sign_packets.c, inventory_packets.c and mob_packets.c.
import { Reader, Framer, packet, nbtText } from "./binary.js";
import { registry } from "./registry.js";
export class ProtocolClient {
  constructor(transport, emit) {
    this.transport = transport;
    this.emit = emit;
    this.state = "login";
    this.sequence = 0;
    this.framer = new Framer((b) => this.receive(b));
    transport.onData = (b) => {
      try {
        this.framer.push(b);
      } catch (e) {
        console.error(e);
        emit(
          "error",
          "The server sent unsupported or malformed protocol data.",
        );
        transport.disconnect();
      }
    };
  }
  send(id, write) {
    this.transport.send(packet(id, write));
  }
  login(host, port, name, uuid, view) {
    this.send(0, (w) =>
      w.varint(registry.protocol).string(host).u16(port).varint(2),
    );
    this.send(0, (w) => w.string(name).raw(uuid));
    this.view = view;
  }
  receive(bytes) {
    const r = new Reader(bytes),
      id = r.varint();
    this.emit("packet", id);
    if (this.state === "login") {
      if (id === 0) {
        this.emit("error", r.string(4096));
        this.transport.disconnect();
        return;
      }
      if (id === 1 || id === 3)
        throw Error(
          "Encrypted or compressed login is not supported by the testing server",
        );
      if (id !== 2) throw Error("Unexpected login response");
      r.take(16);
      r.string(16);
      if (r.varint() !== 0) throw Error("Unsupported login properties");
      r.done();
      this.send(3);
      this.state = "configuration";
      this.emit("status", "Authenticating...");
      this.send(0, (w) =>
        w
          .string("en_us")
          .u8(this.view)
          .varint(0)
          .u8(1)
          .u8(0)
          .varint(1)
          .u8(0)
          .u8(1)
          .varint(0),
      );
      return;
    }
    if (this.state === "configuration") {
      if (id === 14) {
        if (
          r.varint() !== 1 ||
          r.string() !== "minecraft" ||
          r.string() !== "core" ||
          r.string() !== registry.version
        )
          throw Error("Unsupported core registry");
        r.done();
        // Acknowledge semantic registry compatibility, not ownership of any assets.
        this.send(7, (w) =>
          w
            .varint(1)
            .string("minecraft")
            .string("core")
            .string(registry.version),
        );
      } else if (id === 3) {
        r.done();
        this.send(3);
        this.state = "play";
        this.emit("status", "Loading world...");
      } else if (id === 2) {
        this.emit("error", nbtText(r));
        this.transport.disconnect();
      } else if (![1, 7, 13].includes(id))
        throw Error("Unexpected configuration response");
      return;
    }
    switch (id) {
      case 0x2b: {
        const entity = r.i32();
        r.u8();
        const n = r.varint();
        if (n < 0 || n > 128) throw Error("Invalid dimensions");
        for (let i = 0; i < n; i++) r.string();
        r.varint();
        const distance = r.varint();
        r.varint();
        r.take(3);
        r.varint();
        const dimension = r.string();
        r.take(8);
        const mode = r.u8();
        this.emit("join", { entity, distance, dimension, mode });
        break;
      }
      case 0x41: {
        const teleport = r.varint();
        const p = {
          x: r.f64(),
          y: r.f64(),
          z: r.f64(),
          vx: r.f64(),
          vy: r.f64(),
          vz: r.f64(),
          yaw: r.f32(),
          pitch: r.f32(),
          flags: r.u32(),
        };
        r.done();
        if (Object.values(p).some((n) => !Number.isFinite(n)))
          throw Error("Invalid position");
        this.send(0, (w) => w.varint(teleport));
        this.emit("position", p);
        break;
      }
      case 0x27:
        this.emit("chunk", bytes.slice());
        break;
      case 0x08: {
        const p = r.position();
        p.state = r.varint();
        this.emit("block", p);
        break;
      }
      case 0x26: {
        const value = r.take(8);
        this.send(0x1b, (w) => w.raw(value));
        break;
      }
      case 0x57:
        this.emit("center", { x: r.varint(), z: r.varint() });
        break;
      case 0x21: {
        const z = r.i32(),
          x = r.i32();
        this.emit("unload", { x, z });
        break;
      }
      case 0x14: {
        const window = r.varint(),
          revision = r.varint(),
          slot = r.i16(),
          stack = this.stack(r);
        this.emit("slot", { window, revision, slot, ...stack });
        break;
      }
      case 0x59:
        this.emit("cursor", this.stack(r));
        break;
      case 0x62:
        this.emit("held", r.u8());
        break;
      case 0x34: {
        const window = r.varint(),
          type = r.varint();
        this.emit("inventory", { window, type, title: nbtText(r) });
        break;
      }
      case 0x61:
        this.emit("health", {
          health: r.f32(),
          food: r.varint(),
          saturation: r.f32(),
        });
        break;
      case 0x39:
        this.emit("abilities", { flags: r.u8(), fly: r.f32(), walk: r.f32() });
        break;
      case 0x72:
        this.emit("chat", nbtText(r));
        break;
      case 0x1c:
        this.emit("error", nbtText(r));
        this.transport.disconnect();
        break;
      case 0x4b:
        r.varint();
        this.emit("respawn", { dimension: r.string() });
        r.take(8);
        this.emit("mode", r.u8());
        break;
      case 0x22: {
        const event = r.u8(),
          value = r.f32();
        if (event === 3) this.emit("mode", value);
        break;
      }
      case 0x01: {
        const entity = r.varint();
        r.take(16);
        const type = r.varint();
        this.emit("entity", {
          id: entity,
          type,
          x: r.f64(),
          y: r.f64(),
          z: r.f64(),
          pitch: (r.u8() * 360) / 256,
          yaw: (r.u8() * 360) / 256,
        });
        break;
      }
      case 0x1f: {
        const entity = r.varint(),
          x = r.f64(),
          y = r.f64(),
          z = r.f64();
        r.take(24);
        this.emit("entityMove", {
          id: entity,
          x,
          y,
          z,
          yaw: r.f32(),
          pitch: r.f32(),
        });
        break;
      }
      case 0x2f:
        this.emit("entityRelative", {
          id: r.varint(),
          dx: r.i16() / 4096,
          dy: r.i16() / 4096,
          dz: r.i16() / 4096,
          yaw: (r.u8() * 360) / 256,
          pitch: (r.u8() * 360) / 256,
        });
        break;
      case 0x30:
        this.emit("entityMove", {
          id: r.varint(),
          yaw: (r.u8() * 360) / 256,
          pitch: (r.u8() * 360) / 256,
        });
        break;
      case 0x46: {
        const n = r.varint();
        if (n < 0 || n > 4096) throw Error("Invalid entity count");
        for (let i = 0; i < n; i++) this.emit("entityRemove", r.varint());
        break;
      }
      case 0x75:
        this.emit("entityRemove", r.varint());
        break;
      case 0x1e: {
        const entity = r.i32(),
          event = r.u8();
        if (event === 3) this.emit("entityRemove", entity);
        break;
      }
      // Framed optional cosmetics/registries are safely skipped without losing alignment.
    }
  }
  stack(r) {
    const count = r.varint();
    if (!count) return { count: 0, item: 0 };
    const item = r.varint();
    if (count < 0 || count > 127 || r.varint() !== 0 || r.varint() !== 0)
      throw Error("Unsupported item components");
    return { count, item };
  }
  loaded() {
    this.send(0x2b);
  }
  move(p) {
    this.send(0x1e, (w) =>
      w
        .f64(p.x)
        .f64(p.y)
        .f64(p.z)
        .f32(p.yaw)
        .f32(p.pitch)
        .u8(p.grounded ? 1 : 0),
    );
  }
  input(flags) {
    this.send(0x2a, (w) => w.u8(flags));
  }
  select(slot) {
    this.send(0x34, (w) => w.u16(slot));
  }
  action(type, p = { x: 0, y: 0, z: 0, face: 0 }) {
    this.send(0x28, (w) =>
      w.varint(type).position(p).u8(p.face).varint(++this.sequence),
    );
  }
  swing() {
    this.send(0x3c, (w) => w.varint(0));
  }
  use(p, yaw, pitch) {
    if (p)
      this.send(0x3f, (w) =>
        w
          .varint(0)
          .position(p)
          .varint(p.face)
          .f32(0.5)
          .f32(0.5)
          .f32(0.5)
          .u8(0)
          .u8(0)
          .varint(++this.sequence),
      );
    else
      this.send(0x40, (w) =>
        w.varint(0).varint(++this.sequence).f32(yaw).f32(pitch),
      );
  }
  attack(id, sneak = false) {
    this.send(0x19, (w) =>
      w
        .varint(id)
        .varint(1)
        .u8(sneak ? 1 : 0),
    );
  }
  chat(text) {
    if (!text || new TextEncoder().encode(text).length > 224)
      throw Error("Chat is limited to 224 UTF-8 bytes.");
    if (text.startsWith("/")) this.send(6, (w) => w.string(text.slice(1)));
    else
      this.send(8, (w) =>
        w.string(text).u64(Date.now()).u64(0).u8(0).varint(0).i32(0),
      );
  }
  click(window, slot, button = 0, shift = false) {
    this.send(0x11, (w) =>
      w
        .varint(window)
        .varint(0)
        .u16(slot)
        .u8(button)
        .varint(shift ? 1 : 0)
        .varint(0)
        .u8(0),
    );
  }
  closeInventory(window) {
    this.send(0x12, (w) => w.varint(window));
  }
  creative(slot, item) {
    this.send(0x37, (w) =>
      w.u16(slot).varint(64).varint(item).varint(0).varint(0),
    );
  }
  respawn() {
    this.send(0x0b, (w) => w.varint(0));
  }
}
