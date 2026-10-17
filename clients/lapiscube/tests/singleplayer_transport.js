/* Exercise the local stream against the pinned engine's real socket interop. */
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.join(__dirname,'..'),workers=[];
let locked=false;
const locks={async request(name,options,callback){
  const lock=locked?null:{};if(lock)locked=true;
  try{return await callback(lock);}finally{if(lock)locked=false;}
}};
function shell(){
  const nodes=new Map();
  const context={console,setTimeout,clearTimeout,Uint8Array,TextEncoder,
    navigator:{locks},location:{reload(){}},
    Worker:class {
      constructor(){this.sent=[];workers.push(this);setTimeout(()=>this.onmessage({data:{type:'ready'}}),0);}
      postMessage(message){this.sent.push(message);}
      terminate(){this.stopped=true;}
    },
    document:{createElement:()=>({}),body:{append(){}},addEventListener(){},
      getElementById(id){if(!nodes.has(id))nodes.set(id,{});return nodes.get(id);}}
  };
  context.window=context;vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(root,'standalone/singleplayer.js'),'utf8'),context);
  return context;
}
(async()=>{
  const c=shell();await c.LapisSingleplayer.start();
  await assert.rejects(shell().LapisSingleplayer.start(),/already open/);
  const library={};c.LibraryManager={library};c.mergeInto=Object.assign;
  c.Module={lapisSocketFactory:()=>c.LapisSingleplayer.socket()};
  c.HEAPU8=new Uint8Array(64);c.HEAP8=new Int8Array(c.HEAPU8.buffer);
  c.UTF8ToString=()=> 'localhost';c._interop_IsHttpsOnly=()=>false;c.assert=assert;
  const upstream=fs.readFileSync(path.join(root,'engine/src/webclient/interop_web.js'),'utf8');
  assert.ok(upstream.includes("new WebSocket(url, 'ClassiCube')"));
  vm.runInContext(upstream.replace("new WebSocket(url, 'ClassiCube')",'Module.lapisSocketFactory()'),c);
  library.interop_InitSockets();const fd=library.interop_SocketCreate();
  assert.equal(library.interop_SocketConnect(fd,0,25565),c.SOCKETS.EINPROGRESS);
  library.interop_SocketWritable(fd,0);assert.equal(c.HEAPU8[0],0);
  assert.equal(library.interop_SocketSend(fd,8,3),c.SOCKETS.EAGAIN);
  await new Promise(resolve=>setTimeout(resolve,5));
  library.interop_SocketWritable(fd,0);assert.equal(c.HEAPU8[0],1,'Local OPEN must be writable');
  c.HEAPU8.set([1,2,3],8);
  assert.equal(library.interop_SocketSend(fd,8,3),3);
  assert.equal(c.LapisSingleplayer.stream.bufferedAmount,3);
  c.HEAPU8[8]=99;
  assert.deepEqual([...new Uint8Array(workers[0].sent[0].data)],[1,2,3],'Send owns a copy');
  workers[0].onmessage({data:{type:'input-ack',size:3}});
  assert.equal(c.LapisSingleplayer.stream.bufferedAmount,0);
  workers[0].onmessage({data:{type:'data',data:new Uint8Array([8,9,10]).buffer}});
  assert.equal(library.interop_SocketRecv(fd,12,2),2);
  assert.equal(library.interop_SocketRecv(fd,14,2),1);
  assert.deepEqual([...c.HEAPU8.slice(12,15)],[8,9,10]);
  assert.equal(library.interop_SocketRecv(fd,12,2),c.SOCKETS.EAGAIN);
  assert.throws(()=>c.LapisSingleplayer.socket(),/one local client/);
  c.LapisSingleplayer.stream.close();
  library.interop_SocketWritable(fd,0);assert.equal(c.HEAPU8[0],1,'Closed wakes connection polling');
  assert.equal(library.interop_SocketSend(fd,8,3),c.SOCKETS.ENOTCONN);
  assert.equal(library.interop_SocketRecv(fd,12,2),c.SOCKETS.ENOTCONN);
  assert.equal(workers[0].sent.at(-1).type,'stop');
  c.LapisSingleplayer.release();
  console.log('Singleplayer stream: engine connect/read/write/close, copied buffers, acknowledgements and tab lock passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
