// Original procedural item glyphs, CC0-1.0. No external images or font files.
import { items, itemToBlock } from "./registry.mjs";
import { color } from "./resources.mjs";
export function drawItem(ctx, value, x, y, size = 16) {
  if (!value?.count) return;
  const name = items[value.item] || "unknown";
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(size / 16, size / 16);
  ctx.fillStyle = `rgb(${color(name).join(",")})`;
  if (itemToBlock.get(value.item)) {
    ctx.fillRect(2, 2, 12, 12);
    ctx.fillStyle = "#ffffff44";
    ctx.fillRect(2, 2, 12, 3);
    ctx.fillStyle = "#00000033";
    ctx.fillRect(11, 5, 3, 9);
  } else if (/axe|shovel|hoe|sword/.test(name)) {
    ctx.fillStyle = "#b88e61";
    ctx.fillRect(7, 5, 2, 10);
    ctx.fillStyle = /golden/.test(name)
      ? "#e4c569"
      : /diamond/.test(name)
        ? "#72d8d1"
        : /wooden/.test(name)
          ? "#a0856c"
          : "#b9c9d5";
    if (/sword/.test(name)) {
      ctx.fillRect(7, 1, 3, 10);
      ctx.fillRect(4, 10, 9, 2);
    } else if (/shovel/.test(name)) ctx.fillRect(5, 1, 6, 5);
    else {
      ctx.fillRect(3, 2, 10, 3);
      if (/_axe$/.test(name)) ctx.fillRect(3, 4, 4, 4);
    }
  } else if (/bucket/.test(name)) {
    ctx.fillStyle = "#bdcbd3";
    ctx.fillRect(3, 4, 10, 10);
    ctx.fillStyle = "#263447";
    ctx.fillRect(5, 4, 6, 7);
    ctx.fillStyle = /water/.test(name)
      ? "#568fd0"
      : /lava/.test(name)
        ? "#e59b58"
        : "#5c6b79";
    ctx.fillRect(5, 6, 6, 4);
  } else if (/apple|bread|chicken|beef|porkchop|mutton|flesh/.test(name)) {
    ctx.fillStyle = /bread/.test(name) ? "#dfb877" : "#cd8773";
    ctx.fillRect(3, 4, 10, 8);
    ctx.fillRect(5, 2, 6, 12);
  } else {
    ctx.fillStyle = "#a5c7ce";
    ctx.fillRect(3, 3, 10, 10);
    ctx.fillStyle = "#293c55";
    ctx.font = "bold 8px monospace";
    ctx.textAlign = "center";
    ctx.fillText(name[0].toUpperCase(), 8, 11);
  }
  ctx.restore();
}
