import {originalTiles} from './original-tiles.mjs';
const terrain=['loam','meadow','meadow-edge','rock','pebble','dune','bark','rings','boards','foliage','frost','void','pane'];
export const textures=Object.freeze(Object.fromEntries([
 ...originalTiles.map(id=>[id,{path:`/assets/textures/original/${id}.svg`}]),
 ...terrain.map(id=>[`terrain-${id}`,{path:`/assets/textures/terrain/${id}.png`}])
]));
// Wire identifiers are compatibility data; independent filenames describe art.
export const blockRules=[
 ['grass_block','terrain-meadow-edge'],['dirt|mud|podzol|farmland','terrain-loam'],
 ['gravel','terrain-pebble'],['sand','terrain-dune'],['snow|diorite','terrain-frost'],
 ['obsidian','terrain-void'],['coal|bedrock','soot'],['lapis','blue'],['iron','copper'],
 ['gold','amber'],['diamond|emerald','crystal'],['redstone','ember'],
 ['water','water'],['lava','magma'],['glass|ice','terrain-pane'],
 ['leaves','terrain-foliage'],['log|wood','terrain-bark'],['planks','terrain-boards'],
 ['door|trapdoor','gateway'],['sign','plaque'],['chest','box'],['crafting|furnace|jukebox|note_block','bench'],
 ['bricks|terracotta','clay'],['wheat','grain'],['flower|poppy|dandelion|orchid|tulip|daisy|lily|allium|cornflower','bloom'],
 ['sapling|grass|fern|bush|torch|lever|pressure_plate|moss|cactus','sprout'],
 ['stone|andesite|granite','terrain-rock']
];
const rules=blockRules.map(([pattern,id])=>[new RegExp(pattern),id]);
export function blockTexture(name,face=2) {
 name=name?.replace(/^minecraft:/,'')||'unknown';
 if(name==='grass_block'&&face===1)return 'terrain-meadow';
 if(name==='grass_block'&&face===0)return 'terrain-loam';
 if(/log|wood/.test(name)&&(face===0||face===1))return 'terrain-rings';
 return rules.find(([r])=>r.test(name))?.[1]||'fallback';
}
export const itemRules=[['pickaxe|axe|shovel|hoe','tool'],['sword|arrow','blade'],['bow','bow'],['bucket','vessel'],['apple|bread|porkchop|beef|chicken|mutton|stew|wheat','morsel'],['ingot|diamond|emerald|coal|lapis|bone|string|gunpowder','mineral']];
export function itemTexture(name,isBlock=false){
 if(isBlock)return blockTexture(name,1);
 return itemRules.find(([p])=>new RegExp(p).test(name||''))?.[1]||'mineral';
}
export const entityModels={
 149:{texture:'explorer',shape:'biped'},69:{texture:'mineral',shape:'item'},
 95:{texture:'grazer',shape:'quadruped'},28:{texture:'grazer',shape:'quadruped'},
 106:{texture:'grazer',shape:'quadruped'},25:{texture:'grazer',shape:'bird'},
 145:{texture:'crawler',shape:'biped'},110:{texture:'crawler',shape:'biped'},
 119:{texture:'crawler',shape:'crawler'},30:{texture:'spirit',shape:'biped'},
 55:{texture:'spirit',shape:'floating'},148:{texture:'crawler',shape:'biped'},
 4:{texture:'blade',shape:'item'},50:{texture:'magma',shape:'item'}
};
export function entityModel(type){return entityModels[type]||{texture:'fallback',shape:'biped'};}
export function textureRecord(id){return textures[id]||textures.fallback;}
