// Node 22+, no npm dependencies. Exercise the real binary and browser codec.
import assert from 'node:assert/strict';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {mkdir,mkdtemp,writeFile,rm} from 'node:fs/promises';
import {resolve} from 'node:path';
import {EventEmitter,once} from 'node:events';
import {packet,PacketStream,readChunk,readRegistry,readKnownPacks} from '../client/src/network/protocol.mjs';
if(process.argv.includes('--client2'))process.env.LAPIS_OBSIDIAN_CLIENT2_DIR=resolve('client2/dist');
const binary=resolve(process.argv[2]||'lapis-obsidian'), disabled=process.argv.includes('--disabled');
const testTimeout=Number(process.env.WEB_TEST_TIMEOUT_MS||20000);
assert.ok(Number.isFinite(testTimeout)&&testTimeout>=1000&&testTimeout<=600000);
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate,label) {
  const deadline=Date.now()+testTimeout;
  while(!predicate()) { if(Date.now()>deadline)throw Error(`Timed out: ${label}`); await delay(10); }
}
async function reservePort() {
  const listener=net.createServer();listener.listen(0,'127.0.0.1');await once(listener,'listening');return listener;
}
const nativeReservation=await reservePort(),webReservation=await reservePort();
const port=nativeReservation.address().port,webPort=webReservation.address().port;
await Promise.all([nativeReservation,webReservation].map(s=>new Promise(resolve=>s.close(resolve))));
await mkdir(resolve('.tests'),{recursive:true});
const directory=await mkdtemp(resolve('.tests/web-world-'));
await writeFile(`${directory}/server.txt`,`port=${port}\nweb-address=127.0.0.1\nweb-port=${webPort}\ngamemode=creative\n`);
const server=spawn(binary,[],{cwd:directory,env:{...process.env,LAPIS_OBSIDIAN_WEB_CLIENT:disabled?'1':'0'},stdio:['pipe','pipe','pipe']});
let logs='';server.stdout.on('data',b=>logs+=b);server.stderr.on('data',b=>logs+=b);
const connections=[];let clientPort=webPort,proxy;
class Client extends EventEmitter {
  constructor(uuidByte) {
    super(); this.phase='login';this.registries=new Map();this.chunks=[];this.updates=[];this.chat=[];this.slots=new Map();this.entities=new Set();this.error=null;
    this.stream=new PacketStream((id,r)=>this.packet(id,r)); this.buffer=Buffer.alloc(0);this.upgraded=false;
    this.socket=net.connect(clientPort,'127.0.0.1');connections.push(this.socket);
    this.socket.on('error',error=>this.error=error);
    this.socket.on('data',bytes=>{
      try {
        this.buffer=Buffer.concat([this.buffer,bytes]);
        if(!this.upgraded) {
          const end=this.buffer.indexOf('\r\n\r\n');if(end<0)return;
          assert.match(this.buffer.subarray(0,end).toString(),/101 Switching Protocols/);
          this.buffer=this.buffer.subarray(end+4);this.upgraded=true;
          this.send(0,w=>w.varint(772).string('localhost').short(webPort).varint(2));
          this.send(0,w=>w.string(`WebTest${uuidByte}`).raw(new Uint8Array(16).fill(uuidByte)));
        }
        while(this.buffer.length>=2) {
          let size=this.buffer[1]&127,header=2;
          assert.equal(this.buffer[1]&128,0);
          if(size===126){if(this.buffer.length<4)return;size=this.buffer.readUInt16BE(2);header=4;}
          assert.notEqual(size,127);
          if(this.buffer.length<header+size)return;
          const op=this.buffer[0]&15, payload=this.buffer.subarray(header,header+size);
          this.buffer=this.buffer.subarray(header+size);
          if(op===2)this.stream.push(payload);
        }
      }catch(error){this.error=error;}
    });
    this.socket.on('connect',()=>this.socket.write(`GET /ws HTTP/1.1\r\nHost: 127.0.0.1:${clientPort}\r\nOrigin: http://127.0.0.1:${clientPort}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n`));
  }
  send(id,write) {
    const p=packet(id,write), h=Buffer.alloc(p.length<126?6:8),mask=Buffer.from([7,11,17,23]);
    h[0]=130;h[1]=128|(p.length<126?p.length:126);
    if(p.length>=126)h.writeUInt16BE(p.length,2);mask.copy(h,h.length-4);
    const b=Buffer.from(p);for(let i=0;i<b.length;i++)b[i]^=mask[i%4];this.socket.write(Buffer.concat([h,b]));
  }
  packet(id,r) {
    if(this.phase==='login') {
      assert.equal(id,2);this.phase='configuration';this.send(3);
      this.send(0,w=>w.string('en_US').byte(2).varint(0).byte(1).byte(127).varint(1).byte(0).byte(1).varint(0));return;
    }
    if(this.phase==='configuration') {
      if(id===14){const pack=readKnownPacks(r);this.send(7,w=>{w.varint(1);for(const part of pack)w.string(part);});}
      if(id===7){const registry=readRegistry(r);this.registries.set(registry.name,registry.entries);}
      if(id===3){this.phase='play';this.send(3);}return;
    }
    if(id===0x27)this.chunks.push(readChunk(r));
    if(id===0x41){r.varint();this.position=[r.double(),r.double(),r.double()];if(this.chunks.length>=25){this.ready=true;this.send(0x2b);}}
    if(id===0x08)this.updates.push([...r.position(),r.varint()]);
    if(id===0x14){const window=r.varint();r.varint();const slot=r.short(),count=r.varint();this.slots.set(`${window}:${slot}`,{count,item:count?r.varint():0});}
    if(id===0x72){assert.equal(r.byte(),8);this.chat.push(new TextDecoder().decode(r.take(r.short())));}
    if(id===0x26){const token=r.long();this.send(0x1b,w=>w.long(token));}
    if(id===1)this.entities.add(r.varint());
    if(id===0x2b)this.id=r.int();
  }
  async wait(predicate,label) {await until(()=>{if(this.error)throw this.error;return predicate();},label);}
}
async function rejectedStartup(config,expected) {
  const cwd=await mkdtemp(resolve('.tests/web-invalid-'));
  await writeFile(`${cwd}/server.txt`,config);
  const child=spawn(binary,[],{cwd,stdio:['ignore','pipe','pipe']});let output='';
  child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);
  const timer=setTimeout(()=>child.kill(),5000);
  try {const [code]=await once(child,'exit');assert.notEqual(code,0);assert.match(output,expected);}
  finally {clearTimeout(timer);await rm(cwd,{recursive:true,force:true});}
}
async function refused(host,targetPort) {
  const socket=net.connect(targetPort,host);connections.push(socket);
  await new Promise((resolve,reject)=>{
    socket.once('connect',()=>reject(Error(`Unexpected listener on ${host}:${targetPort}`)));
    socket.once('error',error=>error.code==='ECONNREFUSED'?resolve():reject(error));
  });
}
async function noHttpOnNativePort() {
  const s=net.connect(port,'127.0.0.1');connections.push(s);await once(s,'connect');
  let reply='';s.on('error',error=>{if(error.code!=='ECONNRESET')throw error;});s.on('data',b=>reply+=b);s.write('GET / HTTP/1.1\r\nHost: localhost\r\n\r\n');await delay(150);
  assert.doesNotMatch(reply,/HTTP\/1\.[01]/);s.destroy();
}
async function nativeStatus() {
  const socket=net.connect(port,'127.0.0.1');connections.push(socket);await once(socket,'connect');
  let response;
  const stream=new PacketStream((id,r)=>{assert.equal(id,0);response=JSON.parse(r.string());});socket.on('data',b=>stream.push(b));
  socket.write(packet(0,w=>w.varint(772).string('localhost').short(port).varint(1)));socket.write(packet(0));
  await until(()=>response,'native status');assert.equal(response.version.protocol,772);socket.destroy();
}
try {
  await until(()=>logs.includes('Server listening'),'server startup');
  await nativeStatus();
  await noHttpOnNativePort();
  if(disabled) {
    assert.doesNotMatch(logs,/HTML5 client/);
    await refused('127.0.0.1',webPort);
    console.log('default binary: native protocol works; runtime environment cannot enable HTTP');
  } else {
    assert.ok(logs.includes(`HTML5 client listening on 127.0.0.1:${webPort}`));
    await refused('127.0.0.2',webPort); // Binding is restricted to the configured address.
    await rejectedStartup(`port=${port}\nweb-port=${port}\n`,/web-port must differ/);
    const free=await reservePort(),freePort=free.address().port;await new Promise(resolve=>free.close(resolve));
    await rejectedStartup(`port=${freePort}\nweb-address=127.0.0.1\nweb-port=${webPort}\n`,/Cannot listen for the web client/);
    for(const path of process.argv.includes('--client2') ? ['/','/style.css','/src/js/Start.js','/adapter/wire.mjs','/libraries/three.module.js','/provenance.json'] : ['/','/style.css','/src/network/protocol.mjs','/src/rendering/renderer.mjs','/src/core/client.mjs','/catalog.mjs','/assets/textures/terrain/rock.png','/assets/textures/original/tool.svg','/assets/audio/events.json','/assets/manifest.json']) {
      const response=await fetch(`http://127.0.0.1:${webPort}${path}`);assert.equal(response.status,200);
      assert.match(response.headers.get('content-security-policy'),/frame-ancestors 'none'/);assert.ok((await response.text()).length>100);
    }
    assert.equal((await fetch(`http://127.0.0.1:${webPort}/missing`)).status,404);
    if(process.argv.includes('--dev-proxy')){
      const reservation=await reservePort();clientPort=reservation.address().port;await new Promise(resolve=>reservation.close(resolve));
      let proxyLogs='';proxy=spawn(process.execPath,['client/scripts/dev.mjs'],{env:{...process.env,CLIENT_DEV_PORT:String(clientPort),CLIENT_SERVER_ORIGIN:`http://127.0.0.1:${webPort}`},stdio:['ignore','pipe','pipe']});
      proxy.stdout.on('data',b=>proxyLogs+=b);proxy.stderr.on('data',b=>proxyLogs+=b);
      await until(()=>{if(proxy.exitCode!==null)throw Error(proxyLogs);return proxyLogs.includes('fixed HTTP/WebSocket backend');},'development server');
      const response=await fetch(`http://127.0.0.1:${clientPort}/`);assert.equal(response.status,200);assert.match(await response.text(),/<title>Lapis Obsidian Client<\/title>/);
    }
    const a=new Client(1);await a.wait(()=>a.ready,'browser login and 25 chunks');
    assert.equal(a.chunks.length,25);assert.equal(a.registries.get("worldgen/biome")?.length,10);const [x,y,z]=a.position.map(Math.floor);
    const center=a.chunks.find(c=>c.x===Math.floor(x/16)&&c.z===Math.floor(z/16));
    assert.ok(center.data[(y-1)*256+((z%16+16)%16)*16+(x%16+16)%16]>0,'spawn ground matches decoded chunk coordinates');
    a.send(6,w=>w.string('tps'));await a.wait(()=>a.chat.some(s=>s.includes('TPS')),'chat command');
    a.send(0x37,w=>w.short(36).varint(64).varint(28).varint(0).varint(0));
    await a.wait(()=>a.slots.get('0:36')?.item===28,'creative inventory');
    const b=new Client(2);await b.wait(()=>b.ready,'second browser login');
    await a.wait(()=>a.entities.has(b.id),'other player spawn');
    a.send(0x28,w=>w.varint(0).position(x,y-1,z+1).byte(1).varint(1));
    await a.wait(()=>a.updates.some(p=>p[0]===x&&p[1]===y-1&&p[2]===z+1&&p[3]===0),'mining update');
    await b.wait(()=>b.updates.some(p=>p[0]===x&&p[1]===y-1&&p[2]===z+1&&p[3]===0),'shared world mining');
    a.send(0x3f,w=>w.varint(0).position(x,y-1,z).varint(3).float(.5).float(.5).float(.5).byte(0).byte(0).varint(2));
    await a.wait(()=>a.updates.some(p=>p[0]===x&&p[1]===y-1&&p[2]===z+1&&p[3]===10),'placement update');
    a.send(0x1e,w=>w.double(x+.5).double(y).double(z+.5).float(90).float(0).byte(1));
    a.socket.end();await delay(100);
    const reconnected=new Client(1);await reconnected.wait(()=>reconnected.ready,'reconnect');
    assert.equal(reconnected.slots.get('0:36')?.item,28);
    console.log('web binary: configured address/port, isolated listeners, embedded assets, native coexistence, login, chunks, commands, inventory, two players, mining, placement, movement and reconnect passed');
  }
} finally {
  for(const socket of connections)socket.destroy();
  if(proxy){proxy.kill();if(proxy.exitCode===null)await once(proxy,'exit');}
  server.stdin.end('stop\n');
  const timer=setTimeout(()=>server.kill(),5000);
  if(server.exitCode===null)await once(server,'exit');clearTimeout(timer);
  if(server.exitCode!==0)console.error(logs);
  await rm(directory,{recursive:true,force:true});
}
