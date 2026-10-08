// Original mapping of factual server identifiers to upstream renderer block IDs.
// SPDX-License-Identifier: MIT
import { blocks, items } from "../catalog.mjs";
import { tileNames } from "./resources.mjs";
import Block from "../src/js/net/minecraft/client/world/block/Block.js";
import BlockWater from "../src/js/net/minecraft/client/world/block/type/BlockWater.js";
const legacy = {
  air: 0,
  stone: 1,
  grass_block: 2,
  dirt: 3,
  cobblestone: 4,
  oak_planks: 5,
  bedrock: 7,
  water: 9,
  water_1: 9,
  water_2: 9,
  water_3: 9,
  water_4: 9,
  water_5: 9,
  water_6: 9,
  water_7: 9,
  sand: 12,
  gravel: 13,
  oak_log: 17,
  oak_leaves: 18,
  glass: 20,
  torch: 50,
};
export const nameToBlock = new Map(Object.entries(legacy));
for (const name of Object.values(blocks))
  if (!nameToBlock.has(name)) {
    const id = 256 + nameToBlock.size;
    nameToBlock.set(name, id);
    tileNames.push(name);
  }
export const stateToBlock = new Map(
  Object.entries(blocks).map(([state, name]) => [
    Number(state),
    nameToBlock.get(name),
  ]),
);
export const itemToBlock = new Map(
  Object.entries(items).map(([id, name]) => [
    Number(id),
    nameToBlock.get(name) || 0,
  ]),
);
export const blockToItem = new Map(
  Object.entries(items)
    .filter(([, name]) => nameToBlock.has(name))
    .map(([id, name]) => [nameToBlock.get(name), Number(id)]),
);
export function registerBlocks() {
  for (const [name, id] of nameToBlock) {
    if (id < 256) continue;
    const tile = Math.min(255, tileNames.indexOf(name));
    const block = /^lava/.test(name)
      ? new BlockWater(id, tile)
      : new Block(id, tile);
    if (/sapling|flower|fern|grass$|rail|air/.test(name)) {
      block.isSolid = () => false;
      block.getTransparency = () => 0.4;
    }
    if (/glass|ice|leaves/.test(name)) block.getTransparency = () => 0.3;
    if (/^lava/.test(name)) block.getLightValue = () => 15;
    if (/log|wood|planks|chest|crafting/.test(name))
      block.sound = Block.sounds.wood;
    if (/grass|leaves|plant|wheat/.test(name)) block.sound = Block.sounds.grass;
    if (/sand/.test(name)) block.sound = Block.sounds.sand;
  }
}
export { blocks, items };
