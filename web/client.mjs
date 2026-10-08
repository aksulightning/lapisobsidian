import {packet, PacketStream, readChunk} from './protocol.mjs';
import {World, Renderer, direction, material} from './renderer.mjs';
import {items, blocks} from './catalog.mjs';

const $ = id => document.getElementById(id);
const world = new World(), entities = new Map(), keys = new Set(), slots = new Map();
const player = {x:8.5,y:80,z:8.5,yaw:0,pitch:0,vy:0,grounded:false,health:20,food:20,mode:0,hotbar:0};
let renderer, socket, phase='login', ready=false, sequence=0, windowId=0, stateId=0;
let target=null, mining=null, leftDown=false, lastSent=0, lastFrame=performance.now(), myId=-1;
let loginTimer, heldUse=false;
const storage = {
  get(key) { try { return localStorage.getItem(key); } catch { return null; } },
  set(key,value) { try { localStorage.setItem(key,value); } catch { /* Private storage may be unavailable. */ } }
};
$('name').value=storage.get('lapis.web.name')||'WebPlayer';
function send(id,write) {
  if (socket?.readyState===WebSocket.OPEN) {
    if (socket.bufferedAmount>65536) { disconnect('Connection is too slow. Please reconnect.'); return; }
    socket.send(packet(id,write));
  }
}
function message(text) {
  const div=document.createElement('div'); div.textContent=text; $('messages').append(div);
  while ($('messages').children.length>8) $('messages').firstChild.remove();
}
function itemLabel(stack) { return stack?.count ? `${(items[stack.item]||`Item ${stack.item}`).replaceAll('_',' ')} ×${stack.count}` : 'Empty'; }
function refreshInventory() {
  $('hotbar').replaceChildren();
  for (let i=0;i<9;i++) {
    const button=document.createElement('button'); button.className=`slot${i===player.hotbar?' selected':''}`;
    button.textContent=`${i+1} · ${itemLabel(slots.get(`0:${36+i}`))}`;
    button.onclick=()=>selectSlot(i); $('hotbar').append(button);
  }
  if (!$('inventory').open) return;
  $('slots').replaceChildren();
  const count=windowId===2 ? 63 : windowId===14 ? 39 : 46;
  for (let i=0;i<count;i++) {
    const button=document.createElement('button'); button.className='slot';
    const label=windowId===0 && i<=4 ? (i===0?'Output':`Craft ${i}`) : `Slot ${i}`;
    button.textContent=`${label} · ${itemLabel(slots.get(`${windowId}:${i}`))}`;
    button.onclick=event=>send(0x11,w=>w.varint(windowId).varint(stateId).short(i).byte(0).varint(event.shiftKey?1:0).varint(0).byte(0));
    button.oncontextmenu=event=>{ event.preventDefault(); send(0x11,w=>w.varint(windowId).varint(stateId).short(i).byte(1).varint(0).varint(0).byte(0)); };
    $('slots').append(button);
  }
  $('creative').hidden=player.mode!==1 || windowId!==0;
}
function selectSlot(i) { player.hotbar=i; send(0x34,w=>w.short(i)); cancelMine(); refreshInventory(); }
function readStack(r) {
  const count=r.varint(); if (!count) return {count:0,item:0};
  const item=r.varint(), added=r.varint(), removed=r.varint();
  if (added || removed) throw Error('Unsupported item components');
  return {count,item};
}
function clearWorld() { renderer?.clear(); entities.clear(); ready=false; keys.clear(); cancelMine(); player.vy=0; }
function showReady() {
  if (ready) return;
  ready=true; clearTimeout(loginTimer); send(0x2b);
  $('menu').hidden=true; $('hud').hidden=false; $('resume').hidden=document.pointerLockElement===$('world');
  message('Connected. Click to capture the mouse. T: chat · E: inventory · /help: commands');
}
function receive(id,r) {
  if (phase==='login') {
    if (id!==2) throw Error('Server rejected login');
    phase='configuration'; send(3);
    send(0,w=>w.string('en_US').byte(2).varint(0).byte(1).byte(127).varint(1).byte(0).byte(1).varint(0));
    return;
  }
  if (phase==='configuration') {
    if (id===0x0e) send(7,w=>w.varint(1).string('minecraft').string('core').string('1.21.8'));
    if (id===3) { phase='play'; send(3); $('status').textContent='Loading terrain…'; }
    return;
  }
  switch (id) {
    case 0x2b: { // Login Play
      myId=r.int(); r.byte(); const n=r.varint(); for(let i=0;i<n;i++)r.string();
      r.varint(); r.varint(); r.varint(); r.take(3); r.varint(); r.string(); r.long(); player.mode=r.byte(); break;
    }
    case 0x41: {
      const teleport=r.varint(); player.x=r.double(); player.y=r.double(); player.z=r.double();
      r.take(24); player.yaw=r.float()*Math.PI/180; player.pitch=r.float()*Math.PI/180;
      if (r.int()!==0) throw Error('Unsupported relative teleport');
      player.vy=0; send(0,w=>w.varint(teleport));
      if (world.chunks.has(world.key(Math.floor(player.x/16),Math.floor(player.z/16)))) showReady();
      break;
    }
    case 0x27: world.add(readChunk(r)); break;
    case 0x57: world.center=[r.varint(),r.varint()]; break;
    case 0x08: { const p=r.position(); world.set(...p,r.varint()); break; }
    case 0x26: { const token=r.long(); send(0x1b,w=>w.long(token)); break; }
    case 0x14: {
      const win=r.varint(); stateId=r.varint(); const slot=r.short(),stack=readStack(r);
      slots.set(`${win}:${slot}`,stack); refreshInventory(); break;
    }
    case 0x59: $('cursor').textContent=`Holding: ${itemLabel(readStack(r))}`; break;
    case 0x62: player.hotbar=r.byte(); refreshInventory(); break;
    case 0x61:
      player.health=r.float(); player.food=r.varint();
      if (player.health<=0) { keys.clear(); cancelMine(); message('You died. Press R to respawn.'); }
      break;
    case 0x72: {
      if (r.byte()===8) message(new TextDecoder().decode(r.take(r.short()))); break;
    }
    case 0x22: { const event=r.byte(),value=r.float(); if(event===3) {player.mode=value; refreshInventory();} break; }
    case 0x4b:
      clearWorld(); r.varint(); r.string(); r.long(); player.mode=r.byte(); break;
    case 0x34: {
      windowId=r.varint(); r.varint(); openInventory(); break;
    }
    case 0x01: {
      const entityId=r.varint(); r.take(16); const type=r.varint();
      const entity={x:r.double(),y:r.double(),z:r.double(),item:type===69};
      if (entityId!==myId && entities.size<512) entities.set(entityId,entity); break;
    }
    case 0x1f: {
      const entity=entities.get(r.varint()); const x=r.double(),y=r.double(),z=r.double();
      if(entity) Object.assign(entity,{x,y,z}); break;
    }
    case 0x2e:
    case 0x2f: {
      const entity=entities.get(r.varint()); const delta=[r.short(),r.short(),r.short()].map(n=>(n>=32768?n-65536:n)/4096);
      if(entity) {entity.x+=delta[0];entity.y+=delta[1];entity.z+=delta[2];} break;
    }
    case 0x46: { const n=r.varint(); for(let i=0;i<n;i++)entities.delete(r.varint()); break; }
    // Other packets (sound, particles, skins, recipes, sign text) are deliberately ignored.
  }
}
function disconnected(text) {
  clearTimeout(loginTimer); clearWorld(); document.exitPointerLock?.();
  $('inventory').close(); $('chat').hidden=true; $('hud').hidden=true; $('menu').hidden=false;
  $('play').disabled=false; $('status').textContent=text;
}
function disconnect(text='Disconnected. You can rejoin the world.') {
  const previous=socket; socket=null; previous?.close(); disconnected(text);
}
$('join').onsubmit=event=>{
  event.preventDefault();
  try {
    renderer ||= new Renderer($('world'),world);
    clearWorld(); slots.clear(); $('messages').replaceChildren(); windowId=0; phase='login'; sequence=0;
    const name=$('name').value;
    if(!/^[A-Za-z0-9_]{1,15}$/.test(name)) throw Error('Use 1–15 letters, numbers, or underscores.');
    storage.set('lapis.web.name',name);
    let identity=storage.get(`lapis.web.uuid.${name}`);
    if(!/^[a-f0-9]{32}$/.test(identity||'')) {
      const bytes=crypto.getRandomValues(new Uint8Array(16)); bytes[6]=(bytes[6]&15)|64; bytes[8]=(bytes[8]&63)|128;
      identity=Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join(''); storage.set(`lapis.web.uuid.${name}`,identity);
    }
    const uuid=Uint8Array.from(identity.match(/../g),n=>parseInt(n,16));
    const ws=new WebSocket(`${location.protocol==='https:'?'wss:':'ws:'}//${location.host}/ws`);
    socket=ws; ws.binaryType='arraybuffer'; $('play').disabled=true; $('status').textContent='Connecting…';
    const stream=new PacketStream(receive);
    ws.onopen=()=>{
      if(socket!==ws)return;
      send(0,w=>w.varint(772).string(location.hostname).short(Number(location.port)||(location.protocol==='https:'?443:80)).varint(2));
      send(0,w=>w.string(name).raw(uuid));
    };
    ws.onmessage=event=>{
      if(socket!==ws)return;
      try { stream.push(new Uint8Array(event.data)); } catch(error) { disconnect(error.message); }
    };
    ws.onclose=()=>{if(socket===ws){socket=null;disconnected('Connection closed. The server may be full or unavailable.');}};
    ws.onerror=()=>{if(socket===ws)disconnect('Could not connect to this server.');};
    loginTimer=setTimeout(()=>{if(socket===ws)disconnect('Login timed out. Please reconnect.');},60000);
  } catch(error) { disconnected(error.message); }
};
$('leave').onclick=()=>disconnect();
function lockMouse() {
  if(!ready)return;
  try { const promise=$('world').requestPointerLock(); promise?.catch(()=>message('Click the world again to capture the mouse.')); }
  catch { message('Pointer lock is unavailable in this browser.'); }
}
$('resume').onclick=lockMouse;
$('world').onclick=()=>{if(document.pointerLockElement!==$('world'))lockMouse();};
document.addEventListener('pointerlockchange',()=>{
  $('resume').hidden=!ready||document.pointerLockElement===$('world');
  keys.clear(); cancelMine(); leftDown=false;
  if(heldUse){sendAction(5);heldUse=false;}
});
document.addEventListener('mousemove',event=>{
  if(document.pointerLockElement!==$('world'))return;
  player.yaw+=event.movementX*.0025; player.pitch=Math.max(-1.55,Math.min(1.55,player.pitch+event.movementY*.0025));
});
function sendAction(action,hit=target) {
  const p=hit?.p||[0,0,0]; send(0x28,w=>w.varint(action).position(...p).byte(hit?.face||0).varint(++sequence));
}
function cancelMine() { if(mining)sendAction(1,mining.hit); mining=null; }
function entityTarget() {
  const eye=[player.x,player.y+1.62,player.z], d=direction(player.yaw,player.pitch);
  let best=null, distance=5;
  if(target)distance=Math.hypot(...target.p.map((v,i)=>v+.5-eye[i]));
  for(const [id,e] of entities) {
    if(e.item)continue;
    const delta=[e.x-eye[0],e.y+.9-eye[1],e.z-eye[2]], along=delta.reduce((n,v,i)=>n+v*d[i],0);
    const off=Math.hypot(...delta.map((v,i)=>v-along*d[i]));
    if(along>0 && along<distance && off<.65){best=id;distance=along;}
  }
  return best;
}
document.addEventListener('mousedown',event=>{
  if(document.pointerLockElement!==$('world')||!ready||player.health<=0)return;
  if(event.button===0) {
    const entity=entityTarget();
    if(entity!==null)send(0x19,w=>w.varint(entity).varint(1).byte(0)); else leftDown=true;
    send(0x3c,w=>w.varint(0));
  }
  if(event.button===2) {
    const item=items[slots.get(`0:${36+player.hotbar}`)?.item]||'';
    if(!target || /bucket|apple|bread|porkchop|beef|chicken|mutton|stew/.test(item)) {
      send(0x40,w=>w.varint(0).varint(++sequence).float(player.yaw*180/Math.PI).float(player.pitch*180/Math.PI)); heldUse=true;
    } else send(0x3f,w=>w.varint(0).position(...target.p).varint(target.face).float(.5).float(.5).float(.5).byte(0).byte(0).varint(++sequence));
  }
});
document.addEventListener('mouseup',event=>{
  if(event.button===0){leftDown=false;cancelMine();}
  if(event.button===2&&heldUse){sendAction(5);heldUse=false;}
});
$('world').oncontextmenu=event=>event.preventDefault();
function openInventory() { document.exitPointerLock?.(); keys.clear(); $('inventory').showModal(); refreshInventory(); }
function closeInventory() { send(0x12,w=>w.varint(windowId)); windowId=0; $('inventory').close(); lockMouse(); }
$('close-inventory').onclick=closeInventory;
$('inventory').oncancel=event=>{event.preventDefault();closeInventory();};
const blockNames=new Set(Object.values(blocks));
for(const [id,name] of Object.entries(items)) if(blockNames.has(name)&&Number(id)!==0) {
  const option=document.createElement('option'); option.value=id; option.textContent=name.replaceAll('_',' '); $('block').append(option);
}
$('give').onclick=()=>send(0x37,w=>w.short(36+player.hotbar).varint(64).varint(Number($('block').value)).varint(0).varint(0));
document.addEventListener('keydown',event=>{
  if(!ready || event.target.matches('input,select') || $('inventory').open)return;
  if(['Space','KeyW','KeyA','KeyS','KeyD','KeyT','KeyE'].includes(event.code))event.preventDefault();
  if(event.repeat)return;
  if(event.code==='KeyT') { document.exitPointerLock?.(); $('chat').hidden=false; $('message').focus(); return; }
  if(event.code==='KeyE') {windowId=0;openInventory();return;}
  if(event.code==='KeyR'&&player.health<=0){clearWorld();send(0x0b,w=>w.varint(0));return;}
  if(document.pointerLockElement!==$('world'))return;
  keys.add(event.code);
  if(/^Digit[1-9]$/.test(event.code))selectSlot(Number(event.code.at(-1))-1);
  if(event.code==='KeyQ')sendAction(event.ctrlKey?3:4);
});
document.addEventListener('keyup',event=>keys.delete(event.code));
window.addEventListener('blur',()=>{keys.clear();leftDown=false;cancelMine();});
$('chat').onsubmit=event=>{
  event.preventDefault(); const text=$('message').value.trim();
  if(new TextEncoder().encode(text).length>224) {message('Message is too long (maximum 224 UTF-8 bytes).');return;}
  if(text.startsWith('/'))send(6,w=>w.string(text.slice(1)));
  else if(text)send(8,w=>w.string(text).long(Date.now()).long(0).byte(0).varint(0).int(0));
  $('message').value=''; $('chat').hidden=true; lockMouse();
};
$('message').onkeydown=event=>{if(event.key==='Escape'){event.preventDefault();$('chat').hidden=true;lockMouse();}};
function physics(dt) {
  if(!ready || player.health<=0)return;
  const swimming=material(world.get(Math.floor(player.x),Math.floor(player.y),Math.floor(player.z))||0).water;
  const forward=Number(keys.has('KeyW'))-Number(keys.has('KeyS')), right=Number(keys.has('KeyD'))-Number(keys.has('KeyA'));
  const length=Math.hypot(forward,right)||1, speed=swimming?2.5:4.3;
  const dx=(-Math.sin(player.yaw)*forward-Math.cos(player.yaw)*right)/length*speed*dt;
  const dz=(Math.cos(player.yaw)*forward-Math.sin(player.yaw)*right)/length*speed*dt;
  if(!world.collides(player.x+dx,player.y,player.z))player.x+=dx;
  if(!world.collides(player.x,player.y,player.z+dz))player.z+=dz;
  if(keys.has('Space')&&(player.grounded||swimming))player.vy=swimming?3:7;
  player.vy=Math.max(swimming?-3:-35,player.vy-(swimming?7:22)*dt);
  const dy=player.vy*dt;
  if(!world.collides(player.x,player.y+dy,player.z)) {player.y+=dy;player.grounded=false;}
  else {
    // Advance to the collision surface to avoid hovering above the ground.
    let low=0,high=1;
    for(let i=0;i<10;i++){const mid=(low+high)/2;if(world.collides(player.x,player.y+dy*mid,player.z))high=mid;else low=mid;}
    player.y+=dy*low; player.grounded=dy<0;player.vy=0;
  }
  player.y=Math.max(0,Math.min(254.1,player.y));
}
function animate(now) {
  requestAnimationFrame(animate);
  let dt=Math.min((now-lastFrame)/1000,.1); lastFrame=now;
  while(dt>0){const step=Math.min(dt,.02);physics(step);dt-=step;}
  if(!renderer)return;
  target=ready?world.ray([player.x,player.y+1.62,player.z],direction(player.yaw,player.pitch)):null;
  if(ready && leftDown && target && material(target.state).name!=='bedrock') {
    const key=target.p.join(',');
    if(!mining||mining.key!==key){cancelMine();mining={key,hit:target,start:now};sendAction(0);}
    else if(now-mining.start>(player.mode===1?180:700)){sendAction(2,mining.hit);mining=null;}
  } else cancelMine();
  if(ready && now-lastSent>=50){
    send(0x1e,w=>w.double(player.x).double(player.y).double(player.z).float(player.yaw*180/Math.PI).float(player.pitch*180/Math.PI).byte(player.grounded?1:0));
    lastSent=now;
  }
  if(ready) {
    $('vitals').textContent=`Health ${player.health}/20 · Food ${player.food}/20 · ${['Survival','Creative','Adventure','Spectator'][player.mode]||''}`;
    $('location').textContent=`${player.x.toFixed(1)} / ${player.y.toFixed(1)} / ${player.z.toFixed(1)}${target?' · '+material(target.state).name.replaceAll('_',' '):''}`;
  }
  renderer.draw(player,entities,target);
}
requestAnimationFrame(animate);
