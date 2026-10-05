// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import { Worker as NodeWorker } from "node:worker_threads";
import { WebSocket } from "ws";
import { Game } from "../web/game.js";
import { LapisClientTransport } from "../web/transport.js";
import { startServer, until } from "./helpers.mjs";

test("browser game controller and real worker install, mesh, predict, synchronize and reconnect",{timeout:45000},async()=>{
  const server=await startServer();
  const old={Worker:globalThis.Worker,WebSocket:globalThis.WebSocket,location:globalThis.location};
  globalThis.location={protocol:"http:"};
  globalThis.WebSocket=class extends WebSocket{constructor(url,protocol){super(url,protocol,{origin:"http://127.0.0.1:8080"});}};
  globalThis.Worker=class{
    constructor(){this.worker=new NodeWorker(new URL("./world-worker.mjs",import.meta.url));this.worker.on("message",data=>this.onmessage?.({data}));this.worker.on("error",e=>this.onerror?.(e));}
    postMessage(data,transfer){this.worker.postMessage(data,transfer);}
    terminate(){return this.worker.terminate();}
  };
  const chunks=new Map(), statuses=[], errors=[];
  const renderer={chunks, clear:()=>chunks.clear(),update:m=>chunks.set(`${m.x},${m.z}`,m),remove:(x,z)=>chunks.delete(`${x},${z}`)};
  const transport=new LapisClientTransport(s=>statuses.push(s));transport.onError=e=>errors.push(e.message);
  let game;
  try{
    game=new Game(transport,renderer,{distance:2},(type,value)=>{if(type==="error")errors.push(value);});
    await game.connect(server.url,"GameWorker","");
    await until(()=>game.playing && game.world.chunks.size===25 && chunks.size===25,"worker world ready");
    assert.ok([...chunks.values()].some(c=>c.opaque.length>0));
    const start={...game.player};
    for(let i=0;i<25;i++){
      game.update(.05,{forward:1,strafe:0,lookX:0,lookY:0,jump:false,sneak:false,primary:false,secondary:false});
      await new Promise(r=>setTimeout(r,50));
    }
    assert.ok(Math.hypot(game.player.x-start.x,game.player.z-start.z)>.1);
    await until(()=>game.authoritative,"server position acknowledgement");
    assert.ok(Math.abs(game.authoritative.z-game.player.z)<.5);
    game.select(3);await until(()=>game.slot===3,"hotbar selection");
    game.disconnect();await new Promise(r=>setTimeout(r,100));
    await game.connect(server.url,"GameWorker","");
    await until(()=>game.playing && game.world.chunks.size===25,"reconnected worker world");
    assert.equal(game.pendingChunks,0);assert.deepEqual(errors,[]);
    assert.ok(statuses.includes("Negotiating lapisclient"));
  } finally {
    game?.disconnect();game?.protocol.dispose();await game?.worker.terminate();transport.close();await server.stop();Object.assign(globalThis,old);
  }
});
