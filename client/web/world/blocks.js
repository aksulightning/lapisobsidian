// SPDX-License-Identifier: GPL-3.0-only
import { registry } from "../protocol/registry.js";
export const names = Object.keys(registry.palette);
const byState = new Map(
  Object.entries(registry.palette).map(([name, state], i) => [state, i]),
);
export const materials = names.map((name, index) => {
  const air = name === "air",
    fluid = /^(water|lava)/.test(name),
    plant =
      /sapling|flower|tulip|dandelion|poppy|grass$|fern|bush|mushroom|wheat|torch|rail|redstone_wire|lever/.test(
        name,
      ) && !name.includes("block");
  const transparent = fluid || /glass|leaves|ice/.test(name) || plant;
  const height = /slab/.test(name)
    ? 0.5
    : /carpet|pressure_plate|redstone_wire/.test(name)
      ? 0.0625
      : plant
        ? 0.65
        : 1;
  let color = [0.5, 0.52, 0.55];
  if (/grass_block|leaves|moss|sapling|fern|bush|wheat|cactus/.test(name))
    color = [0.3, 0.55, 0.22];
  else if (/dirt|mud|podzol|farmland/.test(name)) color = [0.46, 0.31, 0.19];
  else if (/planks|wood|log|chest|crafting|door|composter/.test(name))
    color = [0.57, 0.41, 0.23];
  else if (/sand/.test(name)) color = [0.78, 0.71, 0.46];
  else if (/water/.test(name)) color = [0.18, 0.43, 0.72];
  else if (/lava|fire/.test(name)) color = [0.95, 0.35, 0.06];
  else if (/snow|white|diorite/.test(name)) color = [0.86, 0.88, 0.89];
  else if (/glass|ice/.test(name)) color = [0.65, 0.83, 0.88];
  else if (/obsidian|bedrock|coal/.test(name)) color = [0.17, 0.16, 0.22];
  else if (/lapis|blue/.test(name)) color = [0.18, 0.3, 0.67];
  else if (/redstone|red_/.test(name)) color = [0.68, 0.16, 0.12];
  else if (/gold|yellow/.test(name)) color = [0.83, 0.65, 0.18];
  else if (/diamond/.test(name)) color = [0.25, 0.77, 0.74];
  else if (/pink|magenta|purple/.test(name)) color = [0.66, 0.34, 0.64];
  return {
    name,
    index,
    air,
    fluid,
    plant,
    transparent,
    solid: !air && !fluid && !plant,
    height,
    color,
    alpha: fluid ? 0.68 : /glass|ice/.test(name) ? 0.38 : 1,
  };
});
const stone = names.indexOf("stone");
const shaped = new Map();
export function material(state) {
  // Oak-door state layout is taken from src/doors.c (four facings, two halves).
  if (state >= 4686 && state <= 4749) {
    if (shaped.has(state)) return shaped.get(state);
    const local = state - 4686,
      facing = Math.floor(local / 16),
      open = !(local & 2);
    const base = materials[names.indexOf("oak_door")];
    const alongX = facing < 2 !== open;
    const box = alongX ? [0, 0, 0, 1, 1, 0.1875] : [0, 0, 0, 0.1875, 1, 1];
    const m = { ...base, transparent: true, box };
    shaped.set(state, m);
    return m;
  }
  let id = byState.get(state);
  if (id === undefined) {
    let name = "stone";
    if (state >= 4342 && state <= 4349) name = "wheat";
    else if (state >= 4350 && state <= 4357) name = "farmland";
    else if (state >= 3042 && state <= 4337) name = "redstone_wire";
    else if (state >= 5916 && state <= 5925) name = "redstone_torch";
    else if (state >= 581 && state <= 2379) name = "note_block";
    id = names.indexOf(name);
    if (id < 0) id = stone;
    byState.set(state, id);
  }
  return materials[id];
}
export function itemName(id) {
  return itemNames.get(id) || `Item ${id}`;
}
const itemNames = new Map(
  Object.entries(registry.items).map(([n, id]) => [id, n.replaceAll("_", " ")]),
);
