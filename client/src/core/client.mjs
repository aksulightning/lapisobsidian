import {packet, PacketStream, readChunk, readKnownPacks, readRegistry, movementPacket, PROTOCOL_VERSION} from '../network/protocol.mjs';
import {Renderer, direction} from '../rendering/renderer.mjs';
import {items, blocks} from '/catalog.mjs';

import {World} from '../world/world.mjs';
import {material,setBlockCatalog} from '../rendering/material.mjs';

import {textures,itemTexture} from '../assets/registry.mjs';
import {AudioSystem,soundEvent} from '../audio/audio.mjs';
import {validateSettings,serverAddress,connectionFailure} from '../ui/settings.mjs';

const $ = id => document.getElementById(id);
setBlockCatalog(blocks);
const world = new World(), entities = new Map(), keys = new Set(), slots = new Map();
const audio=new AudioSystem(), registries=new Map();
let settings=validateSettings(),lastStep=0,lastAmbient=0,lastInput=-1,lastSprint=false,lastFootPosition=[0,0];
const player = {x:8.5,y:80,z:8.5,yaw:0,pitch:0,vy:0,grounded:false,health:20,food:20,mode:0,hotbar:0};
let renderer, socket, phase='login', ready=false, sequence=0, windowId=0, stateId=0;
let target=null, mining=null, leftDown=false, lastSent=0, lastFrame=performance.now(), myId=-1;
let loginTimer, heldUse=false, hasPosition=false, lastReceived=0, loadingStarted=0, receivedBytes=0, lastMovement='';
let touchMode=false,hadPointerLock=false;
const touch={forward:0,right:0,jump:false,move:null,look:null,buttons:new Map()};
const storage = {
  get(key) { try { return localStorage.getItem(key); } catch { return null; } },
  set(key,value) { try { localStorage.setItem(key,value); } catch { /* Private storage may be unavailable. */ } }
};
try{settings=validateSettings(JSON.parse(storage.get('lapis.web.settings')||'{}'));}catch{}
$('server-address').value=location.origin;
$('name').value=storage.get('lapis.web.name')||'WebPlayer';
$('touch-mode').checked=storage.get('lapis.web.touch')===null ? navigator.maxTouchPoints>0 : storage.get('lapis.web.touch')==='true';
function applySettings(){
 audio.volume=settings.volume;if(renderer)renderer.distance=settings.renderDistance;
 $('volume').value=settings.volume;$('distance').value=settings.renderDistance;$('sensitivity').value=settings.sensitivity;$('ambience').checked=settings.ambience;
 storage.set('lapis.web.settings',JSON.stringify(settings));
}
function openSettings(){resetInput();document.exitPointerLock?.();$('settings').showModal();}
$('menu-settings').onclick=openSettings;$('pause-settings').onclick=openSettings;
$('close-settings').onclick=()=>{$('settings').close();};
for(const id of ['volume','distance','sensitivity','ambience'])$(id).oninput=()=>{settings=validateSettings({volume:$('volume').value,renderDistance:$('distance').value,sensitivity:$('sensitivity').value,ambience:$('ambience').checked});applySettings();};
applySettings();
document.addEventListener('click',event=>{if(event.target.closest('button')){audio.unlock();audio.play('click');}});
function pause(){if(!ready)return;resetInput();document.exitPointerLock?.();$('pause-menu').showModal();}
$('pause').onclick=pause;$('pause-resume').onclick=()=>{$('pause-menu').close();lockMouse();};$('pause-leave').onclick=()=>disconnect();
function icon(button,stack){
 if(!stack?.count)return;
 const name=items[stack.item],isBlock=Object.values(blocks).includes(name),id=itemTexture(name,isBlock);
 const image=document.createElement('img');image.className='item-icon';image.alt='';image.src=textures[id]?.path||textures.fallback.path;
 image.onerror=()=>{image.onerror=null;image.src=textures.fallback.path;};button.prepend(image);
}
function setTouchMode() {
  touchMode=$('touch-mode').checked; document.body.classList.toggle('touch-mode',touchMode);
  document.querySelector('.touch-help').hidden=!touchMode;
  document.querySelector('.desktop-help').hidden=touchMode;
  $('touch-controls').hidden=!touchMode||!ready;
  $('resume').hidden=touchMode||!ready||document.pointerLockElement===$('world');
  resetInput();
}
$('touch-mode').onchange=()=>{storage.set('lapis.web.touch',String($('touch-mode').checked));setTouchMode();};
setTouchMode();
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
    icon(button,slots.get(`0:${36+i}`)); button.onclick=()=>selectSlot(i); $('hotbar').append(button);
  }
  if (!$('inventory').open) return;
  $('slots').replaceChildren();
  const count=windowId===2 ? 63 : windowId===14 ? 39 : 46;
  for (let i=0;i<count;i++) {
    const button=document.createElement('button'); button.className='slot';
    const label=windowId===0 && i<=4 ? (i===0?'Output':`Craft ${i}`) : `Slot ${i}`;
    button.textContent=`${label} · ${itemLabel(slots.get(`${windowId}:${i}`))}`;
    icon(button,slots.get(`${windowId}:${i}`));
    button.onclick=event=>send(0x11,w=>w.varint(windowId).varint(stateId).short(i).byte($('split-stack').checked?1:0).varint(event.shiftKey?1:0).varint(0).byte(0));
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
function clearWorld() { renderer?.clear(); entities.clear(); ready=false; hasPosition=false; resetInput(); player.vy=0;lastInput=-1;lastSprint=false; lastMovement=''; }
function showReady() {
  if (ready || !hasPosition || !world.chunks.has(world.key(Math.floor(player.x/16),Math.floor(player.z/16)))) return;
  ready=true; clearInterval(loginTimer); send(0x2b);
  $('menu').hidden=true; $('hud').hidden=false;
  $('touch-controls').hidden=!touchMode;
  $('resume').hidden=touchMode||document.pointerLockElement===$('world');
  message(touchMode?'Connected. Left stick: move · Drag: look · Mine / Use / Jump':'Connected. Click to capture the mouse. T: chat · E: inventory · /help: commands');
}
function receive(id,r) {
  if (phase==='login') {
    if (id!==2) throw Error('Server rejected login');
    phase='configuration'; registries.clear(); send(3);
    send(0,w=>w.string('en_US').byte(2).varint(0).byte(1).byte(127).varint(1).byte(0).byte(1).varint(0));
    return;
  }
  if (phase==='configuration') {
    if(id===0x0e){const pack=readKnownPacks(r);send(7,w=>{w.varint(1);for(const part of pack)w.string(part);});}
    if(id===7){const registry=readRegistry(r);registries.set(registry.name,registry.entries);}
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
      player.vy=0; hasPosition=true; send(0,w=>w.varint(teleport)); showReady();
      break;
    }
    case 0x27: world.add(readChunk(r)); showReady(); break;
    case 0x57: world.center=[r.varint(),r.varint()]; break;
    case 0x08: {const p=r.position(),old=world.get(...p),state=r.varint();world.set(...p,state);
      if(ready&&Math.hypot(p[0]-player.x,p[1]-player.y,p[2]-player.z)<12){if(old&&!state){renderer.burst(p,old);audio.play('break');}else if(!old&&state)audio.play('place');}
      break; }
    case 0x26: { const token=r.long(); send(0x1b,w=>w.long(token)); break; }
    case 0x14: {
      const win=r.varint(); stateId=r.varint(); const slot=r.short(),stack=readStack(r);
      slots.set(`${win}:${slot}`,stack); refreshInventory(); break;
    }
    case 0x59: $('cursor').textContent=`Holding: ${itemLabel(readStack(r))}`; break;
    case 0x62: player.hotbar=r.byte(); refreshInventory(); break;
    case 0x61:
      player.health=r.float(); player.food=r.varint();
      $('respawn').hidden=player.health>0;
      if (player.health<=0) { resetInput(); message('You died. Tap Respawn or press R.'); }
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
      const entity={x:r.double(),y:r.double(),z:r.double(),item:type===69,type};
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
    case 0x5c: {
      const entity=entities.get(r.varint());
      while(r.pos<r.bytes.length){const index=r.byte();if(index===255)break;const type=r.varint();
        if(type===0||type===8)r.byte();else if(type===1)r.varint();else if(type===7){const stack=readStack(r);if(entity&&index===8){entity.itemId=stack.item;entity.itemName=items[stack.item];}}else break;
      }break;
    }
    case 0x75: {r.varint();const collector=r.varint();r.varint();if(collector===myId)audio.play('collect');break;}
    case 0x6a: {r.long();renderer.time=Number(r.long()%24000n);break;}
    case 0x6e: {
      if(r.varint()!==0)break;const name=r.string();if(r.byte())r.float();r.varint();
      const p=[r.int()/8,r.int()/8,r.int()/8],volume=r.float(),pitch=r.float(),event=soundEvent(name);
      const distance=Math.hypot(p[0]-player.x,p[1]-player.y,p[2]-player.z);
      if(event)audio.play(event,{gain:volume*Math.max(0,1-distance/32),pitch});break;
    }
    // Recipes/skins/sign NBT remain compatibility-only; see client-protocol.md.
  }
}
function disconnected(text) {
  clearInterval(loginTimer); clearWorld(); document.exitPointerLock?.();
  $('inventory').close(); $('chat').hidden=true; $('hud').hidden=true; $('menu').hidden=false;
  $('pause-menu').close();$('settings').close();$('play').disabled=false; $('status').textContent=text;
}
function disconnect(text='Disconnected. You can rejoin the world.') {
  const previous=socket; socket=null; previous?.close(); disconnected(text);
}
$('join').onsubmit=event=>{
  event.preventDefault();
  if($('play').disabled)return;
  try {
    const origin=serverAddress($('server-address').value,location.origin);
    if(origin!==location.origin){location.assign(origin);return;}
    audio.unlock();
    renderer ||= new Renderer($('world'),world);
    applySettings();renderer.assetsReady.then(failed=>{if(failed.length)message(`Missing textures: ${failed.join(', ')}. Original fallback art is in use.`);});
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
    lastReceived=loadingStarted=performance.now(); receivedBytes=0;
    const stream=new PacketStream(receive);
    ws.onopen=()=>{
      if(socket!==ws)return;
      send(0,w=>w.varint(PROTOCOL_VERSION).string(location.hostname).short(Number(location.port)||(location.protocol==='https:'?443:80)).varint(2));
      send(0,w=>w.string(name).raw(uuid));
    };
    ws.onmessage=event=>{
      if(socket!==ws)return;
      lastReceived=performance.now(); receivedBytes+=event.data.byteLength;
      try {
        stream.push(new Uint8Array(event.data));
        if(!ready&&phase==='play') $('status').textContent=`Loading terrain… ${world.chunks.size} chunks · ${Math.round(receivedBytes/1024)} KiB received`;
      } catch(error) { disconnect(error.message); }
    };
    ws.onclose=()=>{if(socket===ws){socket=null;disconnected('Connection closed. The server may be full or unavailable.');}};
    ws.onerror=()=>{if(socket===ws)disconnect('Could not connect to this server.');};
    loginTimer=setInterval(()=>{
      if(socket!==ws||ready)return;
      const now=performance.now();
      const failure=connectionFailure(now,lastReceived,loadingStarted);if(failure)disconnect(failure);
    },1000);
  } catch(error) { disconnected(error.message); }
};
$('leave').onclick=()=>disconnect();
function lockMouse() {
  if(!ready||touchMode)return;
  try { const promise=$('world').requestPointerLock(); promise?.catch(()=>message('Click the world again to capture the mouse.')); }
  catch { message('Pointer lock is unavailable in this browser.'); }
}
$('resume').onclick=lockMouse;
$('world').onclick=()=>{if(document.pointerLockElement!==$('world'))lockMouse();};
document.addEventListener('pointerlockchange',()=>{
  const locked=document.pointerLockElement===$('world');
  if(hadPointerLock&&!locked&&ready&&!touchMode&&!$('inventory').open&&$('chat').hidden&&!$('settings').open&&!$('pause-menu').open)pause();
  hadPointerLock=locked;
  $('resume').hidden=touchMode||!ready||document.pointerLockElement===$('world');
  resetInput();
});
document.addEventListener('mousemove',event=>{
  if(document.pointerLockElement!==$('world'))return;
  player.yaw+=event.movementX*.0025*settings.sensitivity; player.pitch=Math.max(-1.55,Math.min(1.55,player.pitch+event.movementY*.0025*settings.sensitivity));
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
function startAction(button) {
  if(!ready||player.health<=0||$('inventory').open||!$('chat').hidden)return;
  audio.play('tool');
  if(button===0) {
    const entity=entityTarget();
    if(entity!==null)send(0x19,w=>w.varint(entity).varint(1).byte(0)); else leftDown=true;
    send(0x3c,w=>w.varint(0));
  }
  if(button===2) {
    const item=items[slots.get(`0:${36+player.hotbar}`)?.item]||'';
    if(!target || /bucket|apple|bread|porkchop|beef|chicken|mutton|stew/.test(item)) {
      send(0x40,w=>w.varint(0).varint(++sequence).float(player.yaw*180/Math.PI).float(player.pitch*180/Math.PI)); heldUse=true;
    } else send(0x3f,w=>w.varint(0).position(...target.p).varint(target.face).float(.5).float(.5).float(.5).byte(0).byte(0).varint(++sequence));
  }
}
function stopAction(button) {
  if(button===0){leftDown=false;cancelMine();}
  if(button===2&&heldUse){sendAction(5);heldUse=false;}
}
document.addEventListener('mousedown',event=>{if(document.pointerLockElement===$('world'))startAction(event.button);});
document.addEventListener('mouseup',event=>stopAction(event.button));
$('world').oncontextmenu=event=>event.preventDefault();
function openInventory() { document.exitPointerLock?.(); resetInput(); $('inventory').showModal(); refreshInventory(); }
function closeInventory() { send(0x12,w=>w.varint(windowId)); windowId=0; $('inventory').close(); lockMouse(); }
$('close-inventory').onclick=closeInventory;
$('inventory').oncancel=event=>{event.preventDefault();closeInventory();};
const blockNames=new Set(Object.values(blocks));
for(const [id,name] of Object.entries(items)) if(blockNames.has(name)&&Number(id)!==0) {
  const option=document.createElement('option'); option.value=id; option.textContent=name.replaceAll('_',' '); $('block').append(option);
}
$('give').onclick=()=>send(0x37,w=>w.short(36+player.hotbar).varint(64).varint(Number($('block').value)).varint(0).varint(0));
document.addEventListener('keydown',event=>{
  if(!ready || event.target.matches('input,select') || $('inventory').open||$('pause-menu').open||$('settings').open)return;
  if(['Space','KeyW','KeyA','KeyS','KeyD','KeyT','KeyE'].includes(event.code))event.preventDefault();
  if(event.repeat)return;
  if(event.code==='Escape') {event.preventDefault();pause();return;}
  if(event.code==='KeyT') { openChat(); return; }
  if(event.code==='KeyE') {windowId=0;openInventory();return;}
  if(event.code==='KeyR'&&player.health<=0){respawn();return;}
  if(document.pointerLockElement!==$('world'))return;
  keys.add(event.code);
  if(/^Digit[1-9]$/.test(event.code))selectSlot(Number(event.code.at(-1))-1);
  if(event.code==='KeyQ')sendAction(event.ctrlKey?3:4);
});
document.addEventListener('keyup',event=>keys.delete(event.code));
window.addEventListener('blur',resetInput);
document.addEventListener('visibilitychange',()=>{if(document.hidden)resetInput();});
function openChat() { document.exitPointerLock?.(); resetInput(); $('chat').hidden=false; $('message').focus(); }
$('close-chat').onclick=()=>{$('chat').hidden=true;lockMouse();};
function respawn() { if(player.health>0)return;clearWorld();$('respawn').hidden=true;send(0x0b,w=>w.varint(0)); }
$('respawn').onclick=respawn;
$('chat').onsubmit=event=>{
  event.preventDefault(); const text=$('message').value.trim();
  if(new TextEncoder().encode(text).length>224) {message('Message is too long (maximum 224 UTF-8 bytes).');return;}
  if(text.startsWith('/'))send(6,w=>w.string(text.slice(1)));
  else if(text)send(8,w=>w.string(text).long(Date.now()).long(0).byte(0).varint(0).int(0));
  $('message').value=''; $('chat').hidden=true; lockMouse();
};
$('message').onkeydown=event=>{if(event.key==='Escape'){event.preventDefault();$('chat').hidden=true;lockMouse();}};
function resetInput() {
  lastInput=-1;player.sneaking=player.sprinting=false;keys.clear(); stopAction(0); stopAction(2);
  touch.forward=touch.right=0; touch.jump=false; touch.move=touch.look=null;
  touch.buttons.clear(); $('move-stick').style.transform='';
  document.querySelectorAll('#touch-actions button').forEach(b=>b.classList.remove('pressed'));
}
function touchPlayable() { return !$('pause-menu').open&&!$('settings').open&&touchMode&&ready&&player.health>0&&!$('inventory').open&&$('chat').hidden; }
function moveStick(event) {
  const box=$('move-pad').getBoundingClientRect(), radius=box.width*.34;
  let x=event.clientX-box.left-box.width/2, y=event.clientY-box.top-box.height/2;
  const length=Math.max(radius,Math.hypot(x,y)); x=x/length*radius; y=y/length*radius;
  touch.right=x/radius; touch.forward=-y/radius;
  $('move-stick').style.transform=`translate(${x}px,${y}px)`;
}
$('move-pad').onpointerdown=event=>{
  if(!touchPlayable()||touch.move!==null)return;
  event.preventDefault(); touch.move=event.pointerId;
  $('move-pad').setPointerCapture(event.pointerId); moveStick(event);
};
$('move-pad').onpointermove=event=>{if(touch.move===event.pointerId)moveStick(event);};
const releaseStick=event=>{
  if(touch.move!==event.pointerId)return;
  touch.move=null; touch.forward=touch.right=0; $('move-stick').style.transform='';
};
for(const type of ['pointerup','pointercancel','lostpointercapture'])$('move-pad').addEventListener(type,releaseStick);
$('world').onpointerdown=event=>{
  if(!touchPlayable()||touch.look!==null)return;
  event.preventDefault(); touch.look={id:event.pointerId,x:event.clientX,y:event.clientY};
  $('world').setPointerCapture(event.pointerId);
};
$('world').onpointermove=event=>{
  if(touch.look?.id!==event.pointerId)return;
  player.yaw+=(event.clientX-touch.look.x)*.005*settings.sensitivity;
  player.pitch=Math.max(-1.55,Math.min(1.55,player.pitch+(event.clientY-touch.look.y)*.005*settings.sensitivity));
  touch.look.x=event.clientX; touch.look.y=event.clientY;
};
const releaseLook=event=>{if(touch.look?.id===event.pointerId)touch.look=null;};
for(const type of ['pointerup','pointercancel','lostpointercapture'])$('world').addEventListener(type,releaseLook);
for(const [id,button] of [['touch-mine',0],['touch-use',2],['touch-jump',null]]) {
  const element=$(id);
  element.onpointerdown=event=>{
    if(!touchPlayable()||touch.buttons.has(id))return;
    event.preventDefault(); touch.buttons.set(id,event.pointerId); element.setPointerCapture(event.pointerId);
    element.classList.add('pressed');
    if(button===null)touch.jump=true;else startAction(button);
  };
  const release=event=>{
    if(touch.buttons.get(id)!==event.pointerId)return;
    touch.buttons.delete(id); element.classList.remove('pressed');
    if(button===null)touch.jump=false;else stopAction(button);
  };
  for(const type of ['pointerup','pointercancel','lostpointercapture'])element.addEventListener(type,release);
}
$('touch-inventory').onclick=()=>{if(ready){windowId=0;openInventory();}};
$('touch-chat').onclick=openChat;
$('touch-drop').onclick=()=>{if(touchPlayable())sendAction(4);};
function physics(dt) {
  if(!ready || player.health<=0||$('pause-menu').open||$('settings').open)return;
  const swimming=material(world.get(Math.floor(player.x),Math.floor(player.y),Math.floor(player.z))||0).water;
  const forward=Number(keys.has('KeyW'))-Number(keys.has('KeyS'))+touch.forward, right=Number(keys.has('KeyD'))-Number(keys.has('KeyA'))+touch.right;
  const length=Math.max(1,Math.hypot(forward,right)), speed=swimming?2.5:player.sneaking?1.3:player.sprinting?5.6:4.3;
  const dx=(-Math.sin(player.yaw)*forward-Math.cos(player.yaw)*right)/length*speed*dt;
  const dz=(Math.cos(player.yaw)*forward-Math.sin(player.yaw)*right)/length*speed*dt;
  if(!world.collides(player.x+dx,player.y,player.z))player.x+=dx;
  if(!world.collides(player.x,player.y,player.z+dz))player.z+=dz;
  if((keys.has('Space')||touch.jump)&&(player.grounded||swimming))player.vy=swimming?3:7;
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
  player.sneaking=keys.has('ShiftLeft')||keys.has('ShiftRight');player.sprinting=keys.has('ControlLeft')||keys.has('ControlRight');
  const movement=[player.x,player.y,player.z,player.yaw,player.pitch,player.grounded].join(',');
  if(ready && now-lastSent>=50 && (movement!==lastMovement||now-lastSent>=1000)){
    if(socket?.readyState===WebSocket.OPEN&&socket.bufferedAmount<65536)socket.send(movementPacket(player));
    lastSent=now; lastMovement=movement;
  }
  if(ready) {
    const label=`Health ${player.health}/20 · Food ${player.food}/20 · ${['Survival','Creative','Adventure','Spectator'][player.mode]||''}`;
    if($('vitals').dataset.label!==label){$('vitals').dataset.label=label;$('vitals').textContent=label;for(const [id,count] of [['heart',Math.ceil(player.health/2)],['food',Math.ceil(player.food/2)]]){const bar=document.createElement('span');bar.className='vital-bar';for(let i=0;i<count;i++){const image=document.createElement('img');image.src=textures[id].path;image.alt='';bar.append(image);}$('vitals').append(bar);}}
    $('location').textContent=`${player.x.toFixed(1)} / ${player.y.toFixed(1)} / ${player.z.toFixed(1)}${target?' · '+material(target.state).name.replaceAll('_',' '):''}`;
  }
  const input=(keys.has('KeyW')?1:0)|(keys.has('KeyS')?2:0)|(keys.has('KeyA')?4:0)|(keys.has('KeyD')?8:0)|(keys.has('Space')||touch.jump?16:0)|(player.sneaking?32:0)|(player.sprinting?64:0);
  if(ready&&input!==lastInput){send(0x2a,w=>w.byte(input));lastInput=input;}
  if(ready&&player.sprinting!==lastSprint){send(0x29,w=>w.varint(myId).byte(player.sprinting?1:2).varint(0));lastSprint=player.sprinting;}
  if(ready&&player.grounded&&Math.hypot(player.x-lastFootPosition[0],player.z-lastFootPosition[1])>.6&&now-lastStep>380){audio.play('step');lastStep=now;lastFootPosition=[player.x,player.z];}
  if(ready&&settings.ambience&&now-lastAmbient>20000){audio.play('ambient');lastAmbient=now;}
  renderer.draw(player,entities,target);
}
requestAnimationFrame(animate);
