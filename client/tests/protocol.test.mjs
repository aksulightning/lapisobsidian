// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import { WebSocket } from "ws";
import { once } from "node:events";
import { startServer } from "./helpers.mjs";
import { decodeChunk, World } from "../web/world/world.js";
import { meshChunk } from "../web/world/mesh.js";
const origin = "http://127.0.0.1:8080";
async function connection(url, options = {}) {
  const ws = new WebSocket(url, "lapisclient", { origin, ...options });
  const queue = [], seen = []; let closed = false;
  ws.on("message", (data, binary) => { const m=binary ? new Uint8Array(data) : JSON.parse(data); queue.push(m); seen.push(binary ? {type:"binary_chunk",x:data.readInt32LE(2),z:data.readInt32LE(6)} : m); });
  ws.on("close", () => closed = true);
  await once(ws,"open");
  assert.equal(ws.protocol, "lapisclient");
  return { ws, seen, send:m=>ws.send(JSON.stringify(m)), async next(type) {
    const end=Date.now()+15000;
    while(Date.now()<end){if(queue.length){const m=queue.shift();if(!type||m.type===type)return m;}else if(closed)throw Error("Closed before "+type);await new Promise(r=>setTimeout(r,5));}
    throw Error("Timed out awaiting "+type);
  }};
}
async function hello(c, version=1){c.send({type:"hello",protocol:"lapisclient",version});return c.next();}
async function login(c,username,token){
  await hello(c);c.send({type:"login",username,...(token?{token}:{})});
  const chunks=[]; let position;
  while(chunks.length<25){const m=await c.next();if(m instanceof Uint8Array)chunks.push(decodeChunk(m));else if(m.type==="player_position")position=m;else if(m.type==="error"||m.type==="login_error")throw Error(JSON.stringify(m));}
  c.send({type:"ready"});await c.next("ready");return {chunks,position};
}
test("direct server: world, authoritative movement, interaction, two players, reconnect and validation", {timeout:90000}, async()=>{
  const server=await startServer();const peers=[];
  const open=async()=>{const c=await connection(server.url);peers.push(c);return c;};
  try{
    const a=await open(), initial=await login(a,"One");
    const world=new World();initial.chunks.forEach(c=>world.add(c));
    const mesh=meshChunk(world,initial.chunks[0]);assert.ok(mesh.opaque.length>0,"server chunks produce visible geometry");
    assert.throws(()=>decodeChunk(new Uint8Array(10)),/length/);
    const corrupt=new Uint8Array(98830);assert.throws(()=>decodeChunk(corrupt),/format/);
    const b=await open();await login(b,"Two");
    const spawned=await a.next("entity_spawn");assert.equal(spawned.kind,149);assert.equal(spawned.x,8.5);
    const p=initial.position;
    a.send({type:"player_move",x:p.x+.3,y:p.y,z:p.z,yaw:10,pitch:0,onGround:true});
    assert.equal((await a.next("player_update")).x,p.x+.3);
    a.send({type:"player_move",x:p.x+.4,y:p.y,z:p.z,yaw:10,pitch:0,onGround:true});
    assert.ok((await b.next("entity_move")).x>=p.x+.3);
    a.send({type:"player_move",x:1000,y:p.y,z:p.z,yaw:0,pitch:0,onGround:true});
    assert.ok((await a.next("player_position")).x<10,"teleport rejected");
    a.send({type:"chat_send",text:'Hello "world" 🪨'});assert.match((await b.next("chat_message")).text,/Hello "world" 🪨/);
    a.send({type:"selected_slot",slot:2});assert.equal((await a.next("selected_slot")).slot,2);
    a.send({type:"creative_slot",slot:38,item:1});assert.equal((await a.next("inventory_slot")).count,64);
    a.send({type:"inventory_click",window:0,slot:38,button:0,shift:false});assert.equal((await a.next("inventory_cursor")).count,64);
    a.send({type:"inventory_click",window:0,slot:38,button:0,shift:false});
    await a.next("inventory_slot");
    const target={x:Math.floor(p.x),y:Math.floor(p.y)-1,z:Math.floor(p.z),face:1};
    a.send({type:"action",action:"break_start",...target});const update=await b.next("block_update");assert.equal(update.state,0);
    // Replace the block by using the block immediately below it with the top face.
    a.send({type:"interact",...target,y:target.y-1});const placed=await b.next("block_update");assert.notEqual(placed.state,0);
    // Move across a chunk edge with a realistic time budget. The server supplies
    // the new edge and unloads the old edge without a chunk request from us.
    for(let step=1;step<=38;step++){
      await new Promise(r=>setTimeout(r,55));
      a.send({type:"player_move",x:p.x+.4+step*.25,y:p.y,z:p.z,yaw:0,pitch:0,onGround:true});
      await a.next("player_update");
    }
    assert.ok(a.seen.some(m=>m.type==="chunk_unload"));
    assert.ok(a.seen.some(m=>m.type==="binary_chunk" && m.x===3));
    a.send({type:"disconnect"});await once(a.ws,"close");assert.ok((await b.next("entity_remove")).id>0);
    const again=await open();const rejoined=await login(again,"One");assert.ok(rejoined.position.x>16);
    const duplicate=await open();await hello(duplicate);duplicate.send({type:"login",username:"One"});assert.equal((await duplicate.next()).code,"already_online");
    const version=await open();assert.equal((await hello(version,2)).code,"protocol_mismatch");
    const state=await open();await hello(state);state.send({type:"player_move"});assert.equal((await state.next()).code,"invalid_state");
    again.send({type:"selected_slot",slot:9});assert.equal((await again.next("error")).code,"invalid_message");
    const malformed=await open();malformed.ws.send('{"type":"hello","type":"hello","protocol":"lapisclient","version":1}');assert.equal((await malformed.next()).code,"invalid_message");
    const fragments=await open();fragments.ws.send('{"type":"hello",',{fin:false});fragments.ws.send('"protocol":"lapisclient","version":1}',{fin:true});assert.equal((await fragments.next()).type,"welcome");
    const binary=await open();binary.ws.send(Buffer.from([0,1]));assert.equal((await binary.next()).code,"invalid_frame");
    const large=await open();large.ws.send("x".repeat(4097));assert.equal((await large.next()).code,"message_too_large");
    for (const opts of [{origin:"https://evil.example"},{origin:undefined}]){
      const bad=new WebSocket(server.url,"lapisclient",opts);await assert.rejects(once(bad,"open"),/403/);
    }
    const unmasked=await open();unmasked.ws.send('{"type":"hello"}',{mask:false});assert.equal((await unmasked.next()).code,"invalid_frame");
    const unknown=await open();await login(unknown,"Three");unknown.send({type:"connect",host:"127.0.0.1",port:22});assert.equal((await unknown.next()).code,"unknown_type");
    for(let i=0;i<101;i++)b.send({type:"player_input",flags:0});assert.equal((await b.next("error")).code,"rate_limit");
    const timeout=await open();assert.equal((await timeout.next()).code,"timeout");
    const wrong=new WebSocket(server.url,"other",{origin});await assert.rejects(once(wrong,"open"),/403/);
    const path=new WebSocket(server.url.replace("/lapisclient","/bridge"),"lapisclient",{origin});await assert.rejects(once(path,"open"),/403/);
  } finally {for(const c of peers)c.ws.terminate();await server.stop();}
});
test("optional access token fails closed and survives reconnection",{timeout:30000},async()=>{
 const token="a".repeat(32), server=await startServer("survival",origin,{LAPIS_WS_TOKEN:token});const peers=[];
 try{
  const c=await connection(server.url);peers.push(c);assert.equal((await hello(c)).tokenRequired,true);
  c.send({type:"login",username:"Private"});assert.equal((await c.next()).code,"authentication_failed");
  const valid=await connection(server.url);peers.push(valid);const state=await login(valid,"Private",token);assert.equal(state.chunks.length,25);
  valid.send({type:"creative_slot",slot:36,item:1});valid.send({type:"ping"});assert.equal((await valid.next("pong")).type,"pong");
 }finally{for(const c of peers)c.ws.terminate();await server.stop();}
});
