// Original mapping of factual server identifiers to upstream renderer block IDs.
// SPDX-License-Identifier: MIT
import {blocks,items} from '../catalog.mjs';
import {tileNames} from './resources.mjs';
import Block from '../src/js/net/minecraft/client/world/block/Block.js';
import BlockWater from '../src/js/net/minecraft/client/world/block/type/BlockWater.js';
const legacy={air:0,stone:1,grass_block:2,dirt:3,cobblestone:4,oak_planks:5,bedrock:7,water:9,sand:12,gravel:13,oak_log:17,oak_leaves:18,glass:20,torch:50};
export const nameToBlock=new Map(Object.entries(legacy));
for(const name of Object.values(blocks))if(!nameToBlock.has(name)){const id=256+nameToBlock.size;nameToBlock.set(name,id);tileNames.push(name);}
export const stateToBlock=new Map(Object.entries(blocks).map(([state,name])=>[Number(state),nameToBlock.get(name)]));
export const itemToBlock=new Map(Object.entries(items).map(([id,name])=>[Number(id),nameToBlock.get(name)||0]));
export const blockToItem=new Map(Object.entries(items).filter(([,name])=>nameToBlock.has(name)).map(([id,name])=>[nameToBlock.get(name),Number(id)]));
export function registerBlocks(){for(const [name,id]of nameToBlock){if(id<256)continue;const tile=Math.min(255,tileNames.indexOf(name));const block=name==='lava'?new BlockWater(id,tile):new Block(id,tile);if(/sapling|flower|fern|grass$|rail|air/.test(name)){block.isSolid=()=>false;block.getTransparency=()=>.4;}if(/glass|ice|leaves/.test(name))block.getTransparency=()=>.3;if(name==='lava')block.getLightValue=()=>15;} }
export {blocks,items};
