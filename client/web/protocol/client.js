// SPDX-License-Identifier: GPL-3.0-only
// Dedicated lapisclient v1 application protocol. No TCP packet framing.
export class ProtocolClient {
  constructor(transport, emit) {
    this.transport = transport; this.emit = emit; this.sent = Object.create(null);
    transport.onMessage = m => this.receive(m);
    transport.onBinary = bytes => emit("chunk", bytes);
  }
  send(type, fields = {}) {
    this.transport.send({ type, ...fields });
    this.sent[type] = (this.sent[type] || 0) + 1;
  }
  login(username, token) {
    this.send("login", token ? { username, token } : { username });
    this.emit("status", "Authenticating");
    this.deadline = setTimeout(() => this.transport.fail(Error("World loading timed out")), 20000);
  }
  receive(m) {
    const fields = { ...m }; delete fields.type;
    const mapped = {
      world_info: "join", chunk_center: "center", chunk_unload: "unload",
      block_update: "block", inventory_slot: "slot", inventory_cursor: "cursor",
      health: "health", abilities: "abilities", entity_move: "entityMove",
      entity_look: "entityMove", entity_delta: "entityRelative", player_update: "authoritative",
      world_time: "time",
    };
    if (mapped[m.type]) {
      if (m.type === "world_info" && (m.registry !== "lapis-v1" || m.minY !== -64 || m.height !== 384)) throw Error("Protocol mismatch");
      this.emit(mapped[m.type], fields); return;
    }
    switch (m.type) {
      case "login_ok": this.emit("status", "Loading world"); break;
      case "player_position":
        for (const key of ["x", "y", "z", "yaw", "pitch"]) if (!Number.isFinite(m[key])) throw Error("Invalid player position");
        this.emit("position", { ...fields, flags: 0, vy: 0 }); break;
      case "entity_spawn": this.emit("entity", { ...fields, type: m.kind }); break;
      case "entity_remove": this.emit("entityRemove", m.id); break;
      case "selected_slot": this.emit("held", m.slot); break;
      case "chat_message": this.emit("chat", m.text); break;
      case "inventory_open": this.emit("inventory", { ...fields, title: "Container" }); break;
      case "respawn": this.emit("respawn", fields); this.emit("mode", m.mode); break;
      case "ready": clearTimeout(this.deadline); this.emit("sessionReady"); break;
      case "entity_update": this.emit("entityMove", fields); break;
      default: throw Error(`Unsupported lapisclient message: ${m.type}`);
    }
  }
  loaded() { this.send("ready"); }
  move(p) { this.send("player_move", { x:p.x, y:p.y, z:p.z, yaw:p.yaw, pitch:p.pitch, onGround:p.grounded }); }
  input(flags) { this.send("player_input", { flags }); }
  select(slot) { this.send("selected_slot", { slot }); }
  action(id, p) {
    const action = ["break_start", "break_cancel", "break_finish", "drop_stack", "drop_item", "release_use", "swap_hands"][id];
    this.send("action", { action, ...(id < 3 ? { x:p.x, y:p.y, z:p.z, face:p.face } : {}) });
  }
  swing() { this.send("swing"); }
  use(p, yaw, pitch) { if (p) this.send("interact", { x:p.x, y:p.y, z:p.z, face:p.face }); else this.send("item_use", { yaw, pitch }); }
  attack(entity, sneak = false) { this.send("entity_interact", { entity, action:"attack", sneak }); }
  chat(text) {
    if (!text || new TextEncoder().encode(text).length > 224) throw Error("Chat is limited to 224 UTF-8 bytes.");
    this.send("chat_send", { text });
  }
  click(window, slot, button = 0, shift = false) { this.send("inventory_click", { window, slot, button, shift }); }
  closeInventory(window) { this.send("inventory_close", { window }); }
  creative(slot, item) { this.send("creative_slot", { slot, item }); }
  respawn() { this.send("respawn"); }
  dispose() { clearTimeout(this.deadline); }
}
