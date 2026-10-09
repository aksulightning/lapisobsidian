let blocks={};
export function setBlockCatalog(catalog){blocks=catalog;properties.clear();}

const properties = new Map();
export function material(state) {
  if (properties.has(state)) return properties.get(state);
  let name = blocks[state];
  if(state>=4342&&state<=4349)name='wheat';
  if(state>=4350&&state<=4357)name='farmland';
  if(state>=5826&&state<=5827)name='stone_pressure_plate';
  if(state>=5892&&state<=5893)name='oak_pressure_plate';
  if(state>=5918&&state<=5925)name='redstone_wall_torch';
  if(state>=6140&&state<=6203)name='oak_trapdoor';
  if(state>=11288&&state<=11351)name='iron_trapdoor';
  if(state>=4367&&state<=4397)name='oak_sign';
  if(state>=4859&&state<=4865)name='oak_wall_sign';
  if(state>=4686&&state<=4749)name='oak_door';
  name ||= 'unknown';
  const water = name.startsWith('water'), lava = name.startsWith('lava');
  const plant = /sapling|short_grass|dead_bush|dandelion|poppy|orchid|allium|tulip|daisy|cornflower|lily|wheat|fern|torch|lever|pressure_plate|redstone_wire|moss_carpet|^snow$/.test(name);
  let color = [.52,.55,.57];
  if (/dirt|mud|podzol/.test(name)) color = [.45,.30,.18];
  if (/grass_block|leaves|moss|cactus|wheat|sapling|short_grass/.test(name)) color = [.35,.58,.23];
  if (/log|wood|planks|chest|crafting|door|sign/.test(name)) color = [.57,.40,.22];
  if (/sand/.test(name)) color = [.80,.73,.49];
  if (/snow|diorite/.test(name)) color = [.86,.90,.89];
  if (/coal|bedrock|obsidian/.test(name)) color = [.21,.23,.28];
  if (/lapis/.test(name)) color = [.20,.36,.68];
  if (/iron/.test(name)) color = [.62,.55,.47];
  if (/gold/.test(name)) color = [.84,.66,.22];
  if (/diamond/.test(name)) color = [.24,.74,.73];
  if (/glass|ice/.test(name)) color = [.57,.77,.83];
  if (/poppy|redstone/.test(name)) color = [.72,.19,.15];
  if (/dandelion|torch/.test(name)) color = [.97,.79,.24];
  if (water) color = [.20,.43,.65];
  if (lava) color = [.95,.34,.07];
  const transparent=water||/glass|ice/.test(name);
  const shape=plant||/door|sign|slab|farmland/.test(name);
  let scale=plant?[.35,.55,.35]:[1,1,1],offset=plant?[.325,0,.325]:[0,0,0];
  if(/trapdoor/.test(name))scale=[1,.1875,1];
  else if(/door/.test(name)){const facing=Math.floor((state-4686)/16),open=((state-4686)%16&2)===0,rotated=(facing+(open?1:0))&1;scale=rotated?[1,1,.1875]:[.1875,1,1];}
  else if(/sign/.test(name)){scale=[1,.7,.15];offset=[0,.3,.425];}
  else if(/slab/.test(name))scale=[1,.5,1];
  else if(name==='farmland')scale=[1,.9375,1];
  const result = {name, color, solid: state !== 0 && !water && !lava && !plant, water, plant,transparent,shape,scale,offset};
  properties.set(state, result); return result;
}
