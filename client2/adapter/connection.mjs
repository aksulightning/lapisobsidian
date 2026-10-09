// Original protocol-772 integration with js-minecraft. SPDX-License-Identifier: MIT
import {
  Cursor,
  Payload,
  Stream,
  frame,
  chunkData,
  movement,
  stack,
} from "./wire.mjs";
import { stateToBlock, itemToBlock, blockToItem, items } from "./registry.mjs";
import WorldClient from "../src/js/net/minecraft/client/world/WorldClient.js";
import Chunk from "../src/js/net/minecraft/client/world/Chunk.js";
import PlayerControllerMultiplayer from "../src/js/net/minecraft/client/network/controller/PlayerControllerMultiplayer.js";
import { spawnEntity, moveEntity, metadata } from "./entities.mjs";
import { Actions } from "./actions.mjs";
import GuiDisconnected from "../src/js/net/minecraft/client/gui/screens/GuiDisconnected.js";
import ProtocolState from "../src/js/net/minecraft/client/network/ProtocolState.js";
import GuiInventory from "./inventory.mjs";
import Block from "../src/js/net/minecraft/client/world/block/Block.js";
export default class Connection {
  constructor(app) {
    this.app = app;
    this.phase = "login";
    this.ready = false;
    this.hasPosition = false;
    this.sequence = 0;
    this.slots = new Map();
    this.entities = new Map();
    this.registries = new Map();
    this.playerInfoMap = new Map();
    this.windowId = 0;
    this.stateId = 0;
    this.mode = 0;
    this.closed = false;
    this.selected = -1;
    this.timers = new Set();
    this.actions = new Actions(this);
  }
  connect(address) {
    let url;
    try {
      url = new URL(
        address.includes("://") ? address : `${location.protocol}//${address}`,
      );
      if (
        !["http:", "https:"].includes(url.protocol) ||
        url.username ||
        url.password ||
        url.pathname !== "/" ||
        url.search ||
        url.hash
      )
        throw Error("Enter the server HTTP(S) origin.");
      if (url.origin !== location.origin) {
        location.assign(url.origin);
        return;
      }
      const name = this.app.getSession().getProfile().getUsername();
      if (!/^[A-Za-z0-9_]{1,15}$/.test(name))
        throw Error("Name must contain 1–15 letters, digits or underscores.");
      this.app.setSession?.(this.app.getSession(), true);
      this.socket = new WebSocket(
        `${url.protocol === "https:" ? "wss:" : "ws:"}//${url.host}/ws`,
      );
      this.socket.binaryType = "arraybuffer";
      const stream = new Stream((id, p) => this.receive(id, p));
      this.socket.onopen = () => {
        this.send(0, (p) => p.vi(772).str(url.hostname).u16(25565).vi(2));
        const compact = this.app.getSession().getProfile().getCompactUUID();
        this.send(0, (p) =>
          p
            .str(name)
            .bytes(
              Uint8Array.from(compact.match(/../g), (n) => parseInt(n, 16)),
            ),
        );
      };
      this.socket.onmessage = (event) => {
        try {
          stream.push(new Uint8Array(event.data));
        } catch (error) {
          this.fail(error.message);
        }
      };
      this.socket.onerror = () =>
        this.fail("Cannot connect to the server web port.");
      this.socket.onclose = () => {
        if (!this.closed) this.fail("Server closed the connection.");
      };
      this.deadline = setTimeout(
        () =>
          this.fail(
            "Login or terrain timed out. Check the server log and protocol 772.",
          ),
        60000,
      );
    } catch (error) {
      this.fail(error.message);
    }
  }
  send(id, fill) {
    if (this.socket?.readyState === WebSocket.OPEN) {
      if (this.socket.bufferedAmount > 65536) {
        this.fail("Connection is too slow.");
        return;
      }
      this.socket.send(frame(id, fill));
    }
  }
  getState() {
    return this.phase === "play" ? ProtocolState.PLAY : ProtocolState.LOGIN;
  }
  getNetworkManager() {
    return this;
  }
  getPlayerInfoMap() {
    return this.playerInfoMap;
  }
  isConnected() {
    return this.socket?.readyState === WebSocket.OPEN;
  }
  close() {
    this.actions.cancel();
    for (const entity of this.entities.values()) entity.renderer?.dispose?.();
    this.entities.clear();
    this.closed = true;
    this.ready = false;
    clearTimeout(this.deadline);
    for (const timer of this.timers) clearTimeout(timer);
    this.timers.clear();
    this.socket?.close(1000);
  }
  fail(message) {
    if (this.closed) return;
    this.close();
    this.app.loadWorld(null);
    this.app.displayScreen(new GuiDisconnected(message));
  }
  sendPacket(packet) {
    const kind = packet.constructor.name,
      player = this.app.player;
    if (kind === "ClientChatPacket") {
      const message = packet.message.slice(0, 256);
      if (message.startsWith("/")) this.send(6, (p) => p.str(message.slice(1)));
      else
        this.send(8, (p) =>
          p
            .str(message)
            .u64(BigInt(Date.now()))
            .u64(0)
            .u8(0)
            .vi(0)
            .bytes(new Uint8Array(3))
            .u8(0),
        );
    } else if (kind === "ClientSwingArmPacket") this.send(0x3c, (p) => p.vi(0));
    else if (kind === "ClientPlayerStatePacket") {
      if (packet.state === 3 || packet.state === 4)
        this.send(0x29, (p) =>
          p
            .vi(player.id)
            .vi(packet.state === 3 ? 1 : 2)
            .vi(0),
        );
    } else if (kind.startsWith("ClientPlayer") && this.ready) {
      this.syncSelection();
      if (this.socket?.readyState === WebSocket.OPEN)
        this.socket.send(movement(player));
      this.send(0x2a, (p) =>
        p.u8((player.isSneaking() ? 32 : 0) | (player.isSprinting() ? 64 : 0)),
      );
    }
  }
  syncSelection() {
    const index = this.app.player?.inventory.selectedSlotIndex;
    if (Number.isInteger(index) && index !== this.selected) {
      this.selected = index;
      this.send(0x34, (p) => p.u16(index));
    }
  }
  receive(id, p) {
    if (this.phase === "login") {
      if (id !== 2)
        throw Error("Login rejected (offline protocol 772 required).");
      this.phase = "configuration";
      this.send(3);
      this.send(0, (w) =>
        w.str("en_US").u8(2).vi(0).u8(1).u8(127).vi(1).u8(0).u8(1).vi(0),
      );
      return;
    }
    if (this.phase === "configuration") {
      if (id === 14) {
        if (p.vi() !== 1) throw Error("Unsupported core packs");
        const pack = [p.str(), p.str(), p.str()];
        if (pack.join(":") !== "minecraft:core:1.21.8")
          throw Error("Protocol 772 / core 1.21.8 required");
        this.send(7, (w) => {
          w.vi(1);
          for (const value of pack) w.str(value);
        });
      } else if (id === 7) {
        const name = p.str(),
          count = p.vi();
        if (count < 0 || count > 4096) throw Error("Registry limit exceeded");
        const entries = [];
        for (let n = 0; n < count; n++) {
          entries.push(p.str());
          if (p.u8()) throw Error("Unsupported registry data");
        }
        this.registries.set(name, entries);
      } else if (id === 3) {
        this.phase = "play";
        this.send(3);
      }
      return;
    }
    const app = this.app,
      player = app.player;
    switch (id) {
      case 0x2b: {
        const entity = p.i32();
        p.u8();
        const count = p.vi();
        for (let n = 0; n < count; n++) p.str();
        p.vi();
        p.vi();
        p.vi();
        p.bytes(3);
        p.vi();
        p.str();
        p.u64();
        this.mode = p.u8();
        app.playerController = new PlayerControllerMultiplayer(
          app,
          this,
          entity,
        );
        app.loadWorld(new WorldClient(app));
        app.player.health = 20;
        app.player.food = 20;
        app.player.inventory.items = [];
        break;
      }
      case 0x41: {
        const teleport = p.vi(),
          coords = [p.f64(), p.f64(), p.f64()];
        p.bytes(24);
        const yaw = p.f32(),
          pitch = p.f32();
        if (p.i32() !== 0) throw Error("Relative teleport unsupported");
        player.setPositionAndRotation(...coords, yaw, pitch);
        player.motionX = player.motionY = player.motionZ = 0;
        this.hasPosition = true;
        this.send(0, (w) => w.vi(teleport));
        this.loaded();
        break;
      }
      case 0x27: {
        const incoming = chunkData(p),
          provider = app.world.getChunkProvider();
        const old = provider.chunks.get(incoming.x + (incoming.z << 16));
        if (old) app.world.group.remove(old.group);
        const chunk = new Chunk(app.world, incoming.x, incoming.z);
        for (let layer = 0; layer < 20; layer++) {
          const section = chunk.sections[layer];
          section.blocks = Array.from(
            incoming.blocks.subarray(layer * 4096, (layer + 1) * 4096),
            (state) => stateToBlock.get(state) ?? 1,
          );
          section.empty = !section.blocks.some(Boolean);
        }
        chunk.loaded = true;
        chunk.biomes = incoming.biomes;
        for (let x = 0; x < 16; x++)
          for (let z = 0; z < 16; z++) {
            let y = 319;
            while (y > 0 && !chunk.getBlockAt(x, y, z)) y--;
            chunk.heightMap[z * 16 + x] = y + 1;
          }
        provider.chunks.set(incoming.x + (incoming.z << 16), chunk);
        app.world.group.add(chunk.group);
        app.worldRenderer.flushRebuild = true;
        this.loaded();
        break;
      }
      case 0x08: {
        const [x, y, z] = p.pos(),
          state = p.vi();
        if (y < 0 || y >= 320) break;
        const old = app.world.getBlockAt(x, y, z);
        if (old && !state)
          app.particleRenderer.spawnBlockBreakParticle(app.world, x, y, z);
        app.world.setBlockAt(x, y, z, stateToBlock.get(state) ?? 1);
        app.worldRenderer.flushRebuild = true;
        const sound = app.world.getBlockAt(x, y, z) || old;
        app.soundManager.playSound(this.blockSound(sound), x, y, z, 0.5, 1);
        break;
      }
      case 0x26:
        this.send(0x1b, (w) => w.u64(p.u64()));
        break;
      case 0x14: {
        let window = p.vi();
        this.stateId = p.vi();
        const slot = p.u16(),
          value = stack(p);
        if (window === -2) window = 0;
        this.slots.set(`${window}:${slot}`, value);
        // Open containers use their own wire slots for the same player inventory.
        const first =
          window === 2 ? 27 : window === 12 ? 10 : window === 14 ? 3 : null;
        if (first !== null && slot >= first && slot < first + 36) {
          const canonical =
            slot - first < 27 ? 9 + slot - first : 36 + slot - first - 27;
          this.slots.set(`0:${canonical}`, value);
        }
        this.refreshHotbar();
        break;
      }
      case 0x59:
        this.cursor = stack(p);
        break;
      case 0x62:
        player.inventory.selectedSlotIndex = p.u8();
        this.selected = player.inventory.selectedSlotIndex;
        break;
      case 0x61:
        player.health = p.f32();
        player.food = p.vi();
        player.saturation = p.f32();
        if (player.food <= 6 && this.mode !== 1) player.sprinting = false;
        if (player.health <= 0) this.actions.cancel();
        if (player.health <= 0)
          app.ingameOverlay.chatOverlay.addMessage(
            "You died. Press R to respawn.",
          );
        break;
      case 0x72:
        if (p.u8() === 8)
          app.ingameOverlay.chatOverlay.addMessage(
            new TextDecoder().decode(p.bytes(p.u16())),
          );
        break;
      case 0x22:
        if (p.u8() === 3) {
          this.actions.cancel();
          this.mode = p.f32();
          if (this.mode !== 1) player.flying = false;
        }
        break;
      case 0x34:
        // Closing an old screen must not close the newly opened server window.
        if (app.currentScreen instanceof GuiInventory)
          app.currentScreen.connection = null;
        this.windowId = p.vi();
        this.windowType = p.vi();
        app.displayScreen(new GuiInventory(this));
        break;
      case 0x3f: {
        const flags = p.u8(),
          count = p.vi();
        // This server emits Add Player + Update Game Mode, without profile properties.
        if (flags !== 5 || count < 0 || count > 128) break;
        for (let n = 0; n < count; n++) {
          const uuid = Array.from(p.bytes(16), (b) =>
              b.toString(16).padStart(2, "0"),
            ).join(""),
            name = p.str();
          const properties = p.vi();
          for (let i = 0; i < properties; i++) {
            p.str();
            p.str();
            if (p.u8()) p.str();
          }
          const mode = p.vi();
          this.playerInfoMap.set(uuid, {
            name,
            mode,
            displayName: null,
            profile: { getUsername: () => name },
            ping: 0,
          });
          for (const other of this.entities.values())
            if (other.uuid === uuid) other.username = name;
        }
        app.ingameOverlay.playerListOverlay.dirty = true;
        break;
      }
      case 0x39: {
        const flags = p.u8();
        p.f32();
        p.f32();
        if (!(flags & 4)) player.flying = false;
        break;
      }
      case 0x01: {
        const entity = p.vi();
        const uuid = Array.from(p.bytes(16), (b) =>
          b.toString(16).padStart(2, "0"),
        ).join("");
        const type = p.vi(),
          x = p.f64(),
          y = p.f64(),
          z = p.f64();
        const pitch = (p.u8() * 360) / 256,
          yaw = (p.u8() * 360) / 256,
          head = (p.u8() * 360) / 256;
        if (entity === player.id || this.entities.size >= 1024) break;
        if (this.entities.has(entity)) this.removeEntity(entity);
        const other = spawnEntity(app, entity, type, x, y, z, yaw, pitch);
        other.uuid = uuid;
        other.username =
          this.playerInfoMap.get(uuid)?.name ||
          (type === 149 ? "Player" : other.kind);
        other.headYaw = head;
        this.entities.set(entity, other);
        app.world.addEntity(other);
        break;
      }
      case 0x1f: {
        const other = this.entities.get(p.vi()),
          x = p.f64(),
          y = p.f64(),
          z = p.f64();
        p.bytes(24);
        const yaw = p.f32(),
          pitch = p.f32();
        p.u8();
        if (other) moveEntity(other, x, y, z, yaw, pitch);
        break;
      }
      case 0x2e:
      case 0x2f: {
        const other = this.entities.get(p.vi()),
          delta = [p.u16(), p.u16(), p.u16()].map(
            (n) => (n > 32767 ? n - 65536 : n) / 4096,
          );
        const yaw = id === 0x2f ? (p.u8() * 360) / 256 : other?.rotationYaw;
        const pitch = id === 0x2f ? (p.u8() * 360) / 256 : other?.rotationPitch;
        p.u8();
        if (other)
          moveEntity(
            other,
            ...other.wirePosition.map((n, i) => n + delta[i]),
            yaw,
            pitch,
          );
        break;
      }
      case 0x31: {
        const other = this.entities.get(p.vi()),
          yaw = (p.u8() * 360) / 256,
          pitch = (p.u8() * 360) / 256;
        if (other) moveEntity(other, ...other.wirePosition, yaw, pitch);
        break;
      }
      case 0x4c: {
        const other = this.entities.get(p.vi()),
          yaw = (p.u8() * 360) / 256;
        if (other) other.headYaw = yaw;
        break;
      }
      case 0x5c:
        metadata(this.entities.get(p.vi()), p);
        break;
      case 0x5f: {
        const other = this.entities.get(p.vi());
        let slot;
        do {
          slot = p.u8();
          const value = stack(p);
          if (other && (slot & 127) === 0) {
            other.equipment = value;
            other.inventory?.setItem(0, itemToBlock.get(value.item) || 0);
          }
        } while (slot & 128);
        break;
      }
      case 0x02: {
        const other = this.entities.get(p.vi());
        if (p.u8() === 0) other?.swingArm?.();
        break;
      }
      case 0x19: {
        const other = this.entities.get(p.vi());
        if (other) other.hurtTicks = 6;
        else
          app.soundManager.playSound(
            "player.hurt",
            player.x,
            player.y,
            player.z,
            0.6,
            1,
          );
        break;
      }
      case 0x1e: {
        const entity = p.i32(),
          event = p.u8();
        if (entity === player.id && event === 9) {
          this.actions.using = false;
          app.soundManager.playSound(
            "player.eat",
            player.x,
            player.y,
            player.z,
            0.5,
            1,
          );
        }
        if (entity === player.id && event === 47)
          app.soundManager.playSound(
            "item.break",
            player.x,
            player.y,
            player.z,
            0.5,
            1,
          );
        break;
      }
      case 0x75: {
        const collected = p.vi(),
          collector = p.vi();
        p.vi();
        this.removeEntity(collected);
        if (collector === player.id)
          app.soundManager.playSound(
            "item.pickup",
            player.x,
            player.y,
            player.z,
            0.4,
            1,
          );
        break;
      }
      case 0x6e: {
        const holder = p.vi();
        if (holder !== 0) break;
        const name = p.str();
        if (p.u8()) p.f32();
        p.vi();
        const x = p.i32() / 8,
          y = p.i32() / 8,
          z = p.i32() / 8,
          volume = p.f32(),
          pitch = p.f32();
        p.u64();
        app.soundManager.playSound(name, x, y, z, volume, pitch);
        break;
      }
      case 0x46:
        for (let n = p.vi(); n > 0; n--) {
          const entity = p.vi();
          this.removeEntity(entity);
        }
        break;
      case 0x6a:
        p.u64();
        app.world.time = Number(p.u64() % 24000n);
        break;
      case 0x4b:
        this.actions.cancel();
        this.ready = this.hasPosition = false;
        for (const entity of this.entities.values())
          entity.renderer?.dispose?.();
        this.entities.clear();
        this.slots.clear();
        this.cursor = null;
        this.windowId = 0;
        this.selected = -1;
        p.vi();
        p.str();
        p.u64();
        this.mode = p.u8();
        app.loadWorld(new WorldClient(app));
        app.player.inventory.items = [];
        break;
    }
  }
  loaded() {
    const player = this.app.player;
    if (
      this.ready ||
      !this.hasPosition ||
      !this.app.world
        .getChunkProvider()
        .chunkExists(Math.floor(player.x / 16), Math.floor(player.z / 16))
    )
      return;
    this.ready = true;
    clearTimeout(this.deadline);
    this.app.loadingScreen = null;
    this.app.displayScreen(null);
    this.send(0x2b);
    this.syncSelection();
  }
  action(button) {
    this.actions.press(button);
  }
  removeEntity(id) {
    const entity = this.entities.get(id);
    entity?.renderer?.dispose?.();
    this.app.world?.removeEntityById(id);
    this.entities.delete(id);
  }
  blockSound(id) {
    return Block.getById(id)?.sound?.getStepSound() || "step.stone";
  }
  refreshHotbar() {
    const app = this.app;
    for (let index = 0; index < 9; index++) {
      const value = this.slots.get(`0:${36 + index}`);
      app.player.inventory.setItem(
        index,
        value?.count ? itemToBlock.get(value.item) || 0 : 0,
      );
    }
    app.itemRenderer.scheduleDirty("hotbar");
  }
  creative(block, slot = this.app.player.inventory.selectedSlotIndex) {
    if (this.mode !== 1) return;
    const item = blockToItem.get(block);
    if (item)
      this.send(0x37, (p) =>
        p
          .u16(36 + slot)
          .vi(64)
          .vi(item)
          .vi(0)
          .vi(0),
      );
  }
}
