// Original authoritative inventory UI using upstream GUI. SPDX-License-Identifier: MIT
import GuiScreen from "../src/js/net/minecraft/client/gui/GuiScreen.js";
import GuiButton from "../src/js/net/minecraft/client/gui/widgets/GuiButton.js";
import Keyboard from "../src/js/net/minecraft/util/Keyboard.js";
import { items } from "./registry.mjs";
import { drawItem } from "./item-icons.mjs";
export function canonicalSlot(window, slot) {
  if (window === 0) return slot;
  const first = { 2: 27, 12: 10, 14: 3 }[window];
  if (first === undefined || slot < first || slot >= first + 36) return null;
  return slot - first < 27 ? slot - first + 9 : slot - first - 27 + 36;
}
export default class Inventory extends GuiScreen {
  constructor(connection) {
    super();
    this.connection = connection;
    this.page = 0;
  }
  init() {
    super.init();
    this.connection.actions.cancel();
    this.layout();
  }
  layout() {
    const c = this.connection;
    this.cells = [];
    this.buttonList = [];
    this.left = Math.floor(this.width / 2) - 90;
    this.top = Math.max(30, Math.floor((this.height - 218) / 2));
    const add = (slot, col, row) =>
      this.cells.push({
        slot,
        x: this.left + col * 20,
        y: this.top + row * 20,
      });
    if (c.mode === 1 && c.windowId === 0) {
      const entries = Object.entries(items).map(([item, name]) => ({
        item: Number(item),
        name,
      }));
      this.entries = entries;
      entries.slice(this.page * 45, (this.page + 1) * 45).forEach((e, i) => {
        add(null, i % 9, Math.floor(i / 9));
        Object.assign(this.cells.at(-1), {
          value: { item: e.item, count: 64 },
        });
      });
      this.buttonList.push(
        new GuiButton("Previous", this.left, this.top + 110, 88, 20, () => {
          this.page = Math.max(0, this.page - 1);
          this.layout();
        }),
      );
      this.buttonList.push(
        new GuiButton("Next", this.left + 92, this.top + 110, 88, 20, () => {
          this.page = Math.min(
            Math.floor((entries.length - 1) / 45),
            this.page + 1,
          );
          this.layout();
        }),
      );
    } else {
      const w = c.windowId;
      if (w === 0) {
        for (let n = 0; n < 4; n++) {
          add(1 + n, 3 + (n % 2), Math.floor(n / 2));
          add(5 + n, n, 3);
        }
        add(0, 6, 1);
        add(45, 8, 3);
      } else if (w === 12) {
        for (let n = 0; n < 9; n++) add(1 + n, 2 + (n % 3), Math.floor(n / 3));
        add(0, 6, 1);
      } else if (w === 14) {
        add(0, 3, 0);
        add(1, 3, 2);
        add(2, 6, 1);
      } else if (w === 2)
        for (let n = 0; n < 27; n++) add(n, n % 9, Math.floor(n / 9));
      const first = { 0: 9, 2: 27, 12: 10, 14: 3 }[w] ?? 9;
      for (let n = 0; n < 27; n++) add(first + n, n % 9, 4 + Math.floor(n / 9));
      for (let n = 0; n < 9; n++) add(first + 27 + n, n, 7.5);
    }
  }
  value(cell) {
    if (cell.value) return cell.value;
    const c = this.connection,
      w = c.windowId,
      canonical = canonicalSlot(w, cell.slot);
    if (canonical !== null)
      return c.slots.get(`0:${canonical}`) || { count: 0, item: 0 };
    return c.slots.get(`${w}:${cell.slot}`) || { count: 0, item: 0 };
  }
  clickSlot(slot, button = 0, shift = false) {
    const c = this.connection;
    c.send(0x11, (p) =>
      p
        .vi(c.windowId)
        .vi(c.stateId)
        .u16(slot)
        .u8(button)
        .vi(shift ? 1 : 0)
        .vi(0)
        .u8(0),
    );
  }
  mouseClicked(x, y, button) {
    const cell = this.cells.find(
      (s) => x >= s.x && x < s.x + 18 && y >= s.y && y < s.y + 18,
    );
    if (cell) {
      if (cell.value) {
        const c = this.connection;
        c.send(0x37, (p) =>
          p
            .u16(36 + c.app.player.inventory.selectedSlotIndex)
            .vi(64)
            .vi(cell.value.item)
            .vi(0)
            .vi(0),
        );
      } else
        this.clickSlot(
          cell.slot,
          button === 2 ? 1 : 0,
          Keyboard.isKeyDown("ShiftLeft") || Keyboard.isKeyDown("ShiftRight"),
        );
    } else super.mouseClicked(x, y, button);
  }
  drawScreen(ctx, x, y, ticks) {
    this.drawDefaultBackground(ctx);
    const c = this.connection,
      creative = c.mode === 1 && c.windowId === 0;
    this.drawCenteredString(
      ctx,
      creative
        ? "Creative items"
        : {
            0: "Inventory · 2×2 crafting",
            2: "Chest",
            12: "Crafting · 3×3",
            14: "Furnace",
          }[c.windowId] || "Inventory",
      this.width / 2,
      this.top - 17,
    );
    for (const cell of this.cells) {
      ctx.fillStyle = "#243249";
      ctx.fillRect(cell.x, cell.y, 18, 18);
      ctx.strokeStyle = "#8195a8";
      ctx.strokeRect(cell.x + 0.5, cell.y + 0.5, 17, 17);
      const value = this.value(cell);
      drawItem(ctx, value, cell.x + 1, cell.y + 1);
      if (value.count > 1)
        this.drawRightString(
          ctx,
          String(value.count),
          cell.x + 17,
          cell.y + 10,
        );
    }
    const hovered = this.cells.find(
      (s) => x >= s.x && x < s.x + 18 && y >= s.y && y < s.y + 18,
    );
    const value = hovered && this.value(hovered);
    const label = value?.count
      ? (items[value.item] || `Item ${value.item}`).replaceAll("_", " ")
      : "Left: stack · Right: split / one · Shift: transfer";
    this.drawCenteredString(ctx, label, this.width / 2, this.top + 177);
    this.drawCenteredString(
      ctx,
      "E / Esc: close",
      this.width / 2,
      this.top + 190,
    );
    super.drawScreen(ctx, x, y, ticks);
    if (c.cursor?.count) {
      drawItem(ctx, c.cursor, x + 5, y + 5);
      this.drawString(ctx, String(c.cursor.count), x + 17, y + 18);
    }
  }
  keyTyped(key) {
    if (key === "KeyE" || key === "Escape") {
      this.minecraft.displayScreen(null);
      return true;
    }
    return super.keyTyped(key);
  }
  onClose() {
    if (!this.connection) return;
    this.connection.send(0x12, (p) => p.vi(this.connection.windowId));
    this.connection.windowId = 0;
  }
}
