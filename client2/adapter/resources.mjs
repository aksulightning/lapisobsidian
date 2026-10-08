// Original procedural artwork, dedicated to CC0-1.0. No external image requests.
import FontRenderer from "../src/js/net/minecraft/client/render/gui/FontRenderer.js";
export const tileNames = [
  "stone",
  "grass",
  "dirt",
  "grass_side",
  "log",
  "log_end",
  "leaves",
  "water",
  "sand",
  "torch",
  "planks",
  "bedrock",
  "glass",
  "gravel",
  "cobblestone",
];
export function color(name) {
  if (/water/.test(name)) return [58, 124, 207];
  if (/lava/.test(name)) return [244, 109, 42];
  if (/grass|leaves|sapling|fern/.test(name)) return [121, 180, 102];
  if (/dirt|soil/.test(name)) return [126, 99, 85];
  if (/sand/.test(name)) return [215, 202, 153];
  if (/log|planks|wood|chest|door/.test(name)) return [160, 122, 84];
  if (/ore/.test(name)) return [120, 134, 151];
  if (/obsidian/.test(name)) return [63, 59, 108];
  if (/glass|ice/.test(name)) return [132, 218, 235];
  return [125, 142, 162];
}
function canvas(width, height, paint) {
  const c = document.createElement("canvas");
  c.width = width;
  c.height = height;
  const ctx = c.getContext("2d");
  ctx.imageSmoothingEnabled = false;
  paint(ctx, c);
  return c;
}
export function createResources() {
  const resources = {};
  resources["terrain/terrain.png"] = canvas(256, 256, (ctx) => {
    for (let tile = 0; tile < 256; tile++) {
      const name = tileNames[tile] || "fallback",
        base = color(name),
        ox = (tile % 16) * 16,
        oy = (tile >> 4) * 16;
      for (let y = 0; y < 16; y++)
        for (let x = 0; x < 16; x++) {
          const noise =
            (((x * 13 + y * 19 + tile * 7) ^ (x * y * 11)) % 21) - 10;
          ctx.fillStyle = `rgb(${base.map((n) => Math.max(0, Math.min(255, n + noise))).join(",")})`;
          ctx.fillRect(ox + x, oy + y, 1, 1);
        }
      if (/ore/.test(name)) {
        ctx.fillStyle = name.includes("diamond")
          ? "#8af0e8"
          : name.includes("gold")
            ? "#edc96e"
            : "#4c779f";
        for (let k = 0; k < 5; k++)
          ctx.fillRect(ox + ((k * 7) % 13), oy + ((k * 11) % 13), 3, 2);
      }
      if (/log|planks/.test(name)) {
        ctx.fillStyle = "#755d50";
        for (let k = 3; k < 16; k += 5) ctx.fillRect(ox + k, oy, 1, 16);
      }
      if (name === "glass") {
        ctx.clearRect(ox + 1, oy + 1, 14, 14);
        ctx.fillStyle = "#9bd7e8";
        ctx.fillRect(ox + 3, oy + 3, 2, 2);
      }
      if (name === "torch") {
        ctx.clearRect(ox, oy, 16, 16);
        ctx.fillStyle = "#c49563";
        ctx.fillRect(ox + 7, oy + 5, 2, 11);
        ctx.fillStyle = "#ffc27c";
        ctx.fillRect(ox + 6, oy + 2, 4, 4);
      }
    }
  });
  resources["gui/font.png"] = canvas(128, 128, (ctx) => {
    ctx.fillStyle = "white";
    ctx.font = "7px monospace";
    ctx.textBaseline = "alphabetic";
    for (let i = 0; i < 256; i++) {
      const glyph = FontRenderer.CHAR_INDEX_LOOKUP[i];
      if (glyph === "\0" || glyph === " ") continue;
      const x = (i % 16) * 8, y = Math.floor(i / 16) * 8;
      ctx.save(); ctx.beginPath(); ctx.rect(x, y, 8, 8); ctx.clip();
      ctx.fillText(glyph, x, y + 7, 7); ctx.restore();
    }
  });
  resources["misc/grasscolor.png"] = canvas(256, 256, (ctx) => {
    for (let y = 0; y < 256; y++) {
      ctx.fillStyle = `rgb(${135 - y / 5},${190 - y / 8},${110 + y / 9})`;
      ctx.fillRect(0, y, 256, 1);
    }
  });
  resources["gui/gui.png"] = canvas(256, 256, (ctx) => {
    ctx.fillStyle = "#273247";
    ctx.fillRect(0, 0, 200, 22);
    ctx.strokeStyle = "#91a6c0";
    for (let i = 0; i < 9; i++) ctx.strokeRect(i * 20 + 1, 1, 19, 20);
    ctx.strokeStyle = "#60d4e5";
    ctx.lineWidth = 2;
    ctx.strokeRect(1, 23, 22, 22);
    for (const [y, c] of [
      [46, "#33415d"],
      [66, "#435976"],
      [86, "#386c86"],
    ]) {
      ctx.fillStyle = c;
      ctx.fillRect(0, y, 200, 20);
      ctx.strokeStyle = "#7ba2ba";
      ctx.strokeRect(0, y, 200, 20);
    }
  });
  resources["gui/icons.png"] = canvas(256, 256, (ctx) => {
    ctx.fillStyle = "#e6f5ff";
    ctx.fillRect(6, 1, 2, 13);
    ctx.fillRect(1, 6, 13, 2);
    for (let i = 0; i < 6; i++) {
      ctx.fillStyle = "#65bcc9";
      ctx.fillRect(0, 16 + i * 8, 8, 6);
    }
  });
  resources["gui/background.png"] = canvas(32, 32, (ctx) => {
    ctx.fillStyle = "#242940";
    ctx.fillRect(0, 0, 32, 32);
    ctx.fillStyle = "#2d3652";
    for (let i = 0; i < 32; i += 8) ctx.fillRect(i, i, 4, 4);
  });
  resources["gui/container/creative.png"] = canvas(256, 256, (ctx) => {
    ctx.fillStyle = "#27344c";
    ctx.fillRect(0, 0, 195, 136);
    ctx.strokeStyle = "#7aa3b6";
    for (let y = 0; y < 5; y++)
      for (let x = 0; x < 9; x++)
        ctx.strokeRect(8 + x * 18, 17 + y * 18, 17, 17);
    for (let x = 0; x < 9; x++) ctx.strokeRect(8 + x * 18, 111, 17, 17);
  });
  resources["char.png"] = canvas(64, 32, (ctx) => {
    ctx.fillStyle = "#354168";
    ctx.fillRect(0, 0, 64, 32);
    ctx.fillStyle = "#70cbd3";
    ctx.fillRect(0, 16, 64, 5);
    ctx.fillStyle = "#d6b78f";
    ctx.fillRect(8, 8, 8, 8);
    ctx.fillStyle = "#222c44";
    ctx.fillRect(9, 10, 2, 2);
    ctx.fillRect(13, 10, 2, 2);
  });
  for (const name of ["sun", "moon"])
    resources[`terrain/${name}.png`] = canvas(32, 32, (ctx) => {
      ctx.fillStyle = name === "sun" ? "#efc388" : "#9bbce3";
      ctx.beginPath();
      ctx.arc(16, 16, 10, 0, Math.PI * 2);
      ctx.fill();
    });
  resources["gui/title/lapis.png"] = canvas(310, 90, (ctx) => {
    ctx.fillStyle = "#93d8e4";
    ctx.font = "bold 30px sans-serif";
    ctx.textAlign = "center";
    ctx.fillText("LAPIS OBSIDIAN", 155, 35);
    ctx.font = "18px sans-serif";
    ctx.fillStyle = "#c7d1eb";
    ctx.fillText("CLIENT", 155, 62);
  });
  for (let side = 0; side < 6; side++)
    resources[`gui/title/background/panorama_${side}.png`] = canvas(
      128,
      128,
      (ctx) => {
        ctx.fillStyle = "#283d69";
        ctx.fillRect(0, 0, 128, 128);
        ctx.fillStyle = "#43557c";
        for (let x = 0; x < 128; x += 8) {
          const height = 20 + ((x * 13 + side * 19) % 44);
          ctx.fillRect(x, 128 - height, 8, height);
        }
        ctx.fillStyle = "#8caecb";
        for (let k = 0; k < 18; k++)
          ctx.fillRect((k * 37 + side * 5) % 128, (k * 19) % 60, 1, 1);
      },
    );
  return resources;
}
