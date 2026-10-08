import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {Reader,Writer,packet,PacketStream,readKnownPacks,readRegistry,movementPacket} from '../src/network/protocol.mjs';
import {textures,blockTexture,itemTexture,entityModel,textureRecord} from '../src/assets/registry.mjs';
import {TextureAtlas,fallbackPixels} from '../src/assets/loader.mjs';
import {resolveSound,soundEvent,noiseSamples} from '../src/audio/audio.mjs';
import {serverAddress,connectionFailure,validateSettings} from '../src/ui/settings.mjs';
import {audit,validateRecord} from '../scripts/audit.mjs';

test('asset manifest verifies file hashes, source evidence and complete registry',async()=>{
 const manifest=await audit();assert.equal(manifest.assets.length,47);
 assert.throws(()=>validateRecord({...manifest.assets[0],license:'CC-BY-NC-4.0'}),/Unapproved/);
 assert.throws(()=>validateRecord({...manifest.assets[0],commit:null}),/Unapproved/);
 assert.throws(()=>validateRecord({...manifest.assets[0],creator:''}),/Missing/);
});
test('texture lookup and atlas use deterministic fallback and cache failed loads',async()=>{
 assert.equal(textureRecord('missing'),textures.fallback);
 assert.equal(fallbackPixels().length,1024);assert.deepEqual(fallbackPixels(),fallbackPixels());
 const calls=new Map(),tiles=[];globalThis.ImageData=class{constructor(data,width,height){Object.assign(this,{data,width,height});}};
 const atlas=new TextureAtlas({createCanvas:()=>({getContext:()=>({drawImage(){},putImageData(image,x,y){tiles.push([image.data,x,y]);}})}),loadImage:async path=>{calls.set(path,(calls.get(path)||0)+1);throw Error('missing');}});
 await atlas.ready;assert.equal(atlas.failed.length,Object.keys(textures).length);assert.equal(tiles.length,Object.keys(textures).length);
 await assert.rejects(atlas.image('fallback'));assert.equal(calls.get(textures.fallback.path),1);
 assert.deepEqual(atlas.uv('unknown'),atlas.uv('fallback'));
 for(const uv of atlas.uv('terrain-rock'))assert.ok(uv>=0&&uv<=1);
});
test('missing sound is silent and noise synthesis is reproducible',async()=>{
 const events=JSON.parse(await readFile(new URL('../public/assets/audio/events.json',import.meta.url)));
 assert.equal(resolveSound(events,'missing'),null);assert.equal(soundEvent('unmapped'),null);
 assert.equal(soundEvent('minecraft:entity.cow.ambient'),'entity');assert.equal(soundEvent('minecraft:block.note_block.harp'),'note');
 assert.deepEqual(noiseSamples(128),noiseSamples(128));assert.notDeepEqual(noiseSamples(128,1),noiseSamples(128,2));
 for(const e of Object.values(events)){assert.ok(e.duration>0&&e.duration<=2);assert.ok(e.gain>0&&e.gain<=1);}
});
test('server registry identifiers map to independent assets',()=>{
 assert.equal(blockTexture('minecraft:grass_block',1),'terrain-meadow');assert.equal(blockTexture('grass_block',0),'terrain-loam');
 assert.equal(blockTexture('oak_log',1),'terrain-rings');assert.equal(blockTexture('diamond_ore'),'crystal');
 assert.equal(blockTexture('unrecognized'),'fallback');assert.equal(itemTexture('iron_pickaxe'),'tool');assert.equal(itemTexture('dirt',true),'terrain-loam');
 for(const [,id] of Object.entries(textures))assert.ok(id.path.startsWith('/assets/'));
 for(const type of [25,28,30,55,69,95,106,110,119,145,148,149])assert.notEqual(entityModel(type).texture,'fallback');
});
test('pack negotiation rejects wrong versions and configuration parses compact registries',()=>{
 const bytes=version=>new Writer().varint(1).string('minecraft').string('core').string(version).finish();
 assert.deepEqual(readKnownPacks(new Reader(bytes('1.21.8'))),['minecraft','core','1.21.8']);
 assert.throws(()=>readKnownPacks(new Reader(bytes('1.21.7'))),/772/);
 const r=new Reader(new Writer().string('minecraft:worldgen/biome').varint(2).string('plains').byte(0).string('desert').byte(0).finish());
 assert.deepEqual(readRegistry(r),{name:'minecraft:worldgen/biome',entries:['plains','desert']});
 assert.throws(()=>readRegistry(new Reader(new Writer().string('test').varint(1).string('entry').byte(1).finish())),/NBT/);
});
test('movement preserves real protocol field widths and signed positions',()=>{
 const player={x:-18.25,y:64.75,z:31.5,yaw:Math.PI/2,pitch:-Math.PI/4,grounded:true};let checked=false;
 new PacketStream((id,r)=>{assert.equal(id,0x1e);assert.equal(r.double(),player.x);assert.equal(r.double(),player.y);assert.equal(r.double(),player.z);assert.equal(r.float(),90);assert.equal(r.float(),-45);assert.equal(r.byte(),1);assert.equal(r.pos,r.bytes.length);checked=true;}).push(movementPacket(player));assert.ok(checked);
});
test('connection failures and safe direct-connect origins are explicit',()=>{
 assert.equal(connectionFailure(59999,0,0),null);assert.match(connectionFailure(60001,0,0),/60 seconds/);
 assert.match(connectionFailure(300001,290000,0),/5 minutes/);
 assert.equal(serverAddress('http://localhost:8080','http://localhost:8081'),'http://localhost:8080');
 for(const value of ['javascript:alert(1)','http://user:pass@localhost:8080','http://localhost:8080/path'])assert.throws(()=>serverAddress(value,'http://localhost'));
 assert.deepEqual(validateSettings({volume:4,renderDistance:60,sensitivity:-1,ambience:false}),{volume:1,renderDistance:3,sensitivity:.25,ambience:false});
});
test('truncated and hostile packet streams fail without interpreting following data',()=>{
 assert.throws(()=>new PacketStream(()=>{}).push(Uint8Array.of(255,255,255,255)),/length/);
 assert.throws(()=>new Reader(Uint8Array.of(128)).varint(),/Truncated/);
 const ids=[],stream=new PacketStream(id=>ids.push(id));const bytes=packet(7,w=>w.string('registry'));
 for(const b of bytes)stream.push(Uint8Array.of(b));assert.deepEqual(ids,[7]);
});

test('world collision and block updates use server geometry and chunk coordinates',async()=>{
 const {World}=await import('../src/world/world.mjs');const {setBlockCatalog}=await import('../src/rendering/material.mjs');
 setBlockCatalog({0:'air',1:'stone',2:'water',3:'oak_slab'});
 const world=new World(),data=new Uint16Array(81920);world.add({x:-1,z:-1,data,dirty:true});
 world.set(-1,5,-1,1);assert.equal(world.get(-1,5,-1),1);assert.ok(world.collides(-.5,5,-.5));
 world.set(-1,5,-1,2);assert.equal(world.collides(-.5,5,-.5),false);
 world.set(-1,5,-1,3);assert.ok(world.collides(-.5,5.1,-.5));assert.equal(world.collides(-.5,5.5,-.5),false);
 world.set(-1,5,-1,0);assert.equal(world.get(-1,5,-1),0);assert.ok(world.collides(16,5,16));
});
