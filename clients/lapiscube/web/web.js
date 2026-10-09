/* Browser shell for the C client. No local inventory or game simulation. */
'use strict';
const canvas=document.getElementById('canvas'),gate=document.getElementById('gate');
const touch=document.getElementById('touch'),message=document.getElementById('logmsg');
const touchDevice=matchMedia('(pointer: coarse)').matches || /Android|iPhone|iPad/.test(navigator.userAgent);
let started=false,ready=false,blocked=true,menu=false,failed=false;
const pointers=new Map(),held=new Map();
const fullscreen=()=>!!(document.fullscreenElement || document.webkitFullscreenElement ||
  matchMedia('(display-mode: fullscreen)').matches || matchMedia('(display-mode: standalone)').matches || navigator.standalone);
const landscape=()=>innerWidth>innerHeight;
function reset(){pointers.clear();held.clear();if(ready)Module._LapisWeb_Reset();}
function resize(){canvas.width=innerWidth;canvas.height=innerHeight;updateGate();}
function updateGate(){
  const next=failed || !started || document.hidden || (touchDevice && (!landscape() || !fullscreen()));
  if(next && !blocked)reset();blocked=next;
  gate.hidden=!blocked;touch.hidden=!ready || blocked || !touchDevice;
  document.getElementById('join').hidden=started;
  document.getElementById('resume').hidden=!started || failed;
  document.getElementById('instruction').textContent=touchDevice && !landscape() ?
    'Rotate your device to landscape to play.' : touchDevice && !fullscreen() ?
    'Touch play requires fullscreen. Tap Play, or open this page from your home screen.' :
    started?'Resume your world.':'Explore, build and survive together.';
}
async function enterFullscreen(){
  if(touchDevice && !fullscreen()){
    const target=document.getElementById('canvas_wrapper');
    const request=target.requestFullscreen || target.webkitRequestFullscreen;
    if(!request){message.textContent='Add this page to your home screen, open it there, then rotate to landscape.';return false;}
    try{await request.call(target);}catch(e){message.textContent='Fullscreen was declined. Tap again or open as a home-screen app.';return false;}
  }
  if(touchDevice && screen.orientation && screen.orientation.lock){
    try{await screen.orientation.lock('landscape');}catch(e){/* Manual rotation remains available. */}
  }
  updateGate();return !touchDevice || (fullscreen() && landscape());
}
function resumeAudio(){if(window.AUDIO && AUDIO.context)AUDIO.context.resume().catch(()=>{});}
document.getElementById('join').addEventListener('submit',async event=>{
  event.preventDefault();
  if(started || !document.getElementById('join').reportValidity())return;
  if(!await enterFullscreen())return;
  started=true;updateGate();
  const endpoint=new URL('ws',location.href);endpoint.protocol=location.protocol==='https:'?'wss:':'ws:';
  window.Module={canvas,lapisSocketURL:endpoint.href,lapisTouch:touchDevice,
    arguments:['--lapis',document.getElementById('username').value,location.hostname,String(location.port || (location.protocol==='https:'?443:80))],
    print:console.log,printErr:console.error,
    setStatus:text=>{message.textContent=text;},
    onAbort:reason=>{failed=true;message.textContent='The client stopped: '+reason;reset();updateGate();},
    onRuntimeInitialized:()=>{ready=true;resize();resumeAudio();}
  };
  const script=document.createElement('script');script.src='LapisCube.js';
  script.onerror=()=>{failed=true;message.textContent='Client download failed. Reload to retry.';updateGate();};
  document.body.append(script);canvas.focus();
});
document.getElementById('resume').addEventListener('click',async()=>{
  await enterFullscreen();resumeAudio();updateGate();if(!blocked)canvas.focus();
});
function action(id,down){if(ready && !blocked)Module._LapisWeb_Action(id,down?1:0);}
// Count presses per action so two fingers never release one another's keys.
function hold(id,down){const n=held.get(id)||0;held.set(id,Math.max(0,n+(down?1:-1)));if(down && !n)action(id,true);if(!down && n===1)action(id,false);}
function coordinates(event){
  const r=canvas.getBoundingClientRect();
  return {x:(event.clientX-r.left)*canvas.width/r.width,y:(event.clientY-r.top)*canvas.height/r.height};
}
for(const button of document.querySelectorAll('[data-action]')){
  button.addEventListener('pointerdown',event=>{
    event.preventDefault();event.stopPropagation();if(blocked)return;
    resumeAudio();const id=Number(button.dataset.action);button.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId,{action:id});hold(id,true);
  });
  for(const name of ['pointerup','pointercancel','lostpointercapture'])button.addEventListener(name,event=>{
    const p=pointers.get(event.pointerId);if(!p)return;pointers.delete(event.pointerId);hold(p.action,false);
  });
}
for(const button of document.querySelectorAll('[data-mode]'))button.addEventListener('click',()=>{
  if(!ready || blocked)return;
  Module._LapisWeb_ClickMode(Number(button.dataset.mode));
  for(const other of document.querySelectorAll('[data-mode]'))other.setAttribute('aria-pressed',String(other===button));
});
canvas.addEventListener('pointerdown',event=>{
  if(event.pointerType!=='touch')return;
  event.preventDefault();if(!ready || blocked)return;resumeAudio();canvas.setPointerCapture(event.pointerId);
  // The C screen can change between shell updates (for example, Close then a
  // quick hotbar tap). Route against its current state, never the cached layout.
  const p={...coordinates(event),gui:!!(Module._LapisWeb_State()&2)};pointers.set(event.pointerId,p);
  if(p.gui)Module._LapisWeb_Pointer(p.x,p.y,1);
  else {
    const size=Math.min(40,Math.floor((canvas.width-16)/9)),left=(canvas.width-9*size)/2;
    if(p.y>=canvas.height-size-6 && p.x>=left && p.x<left+size*9){action(20+Math.floor((p.x-left)/size),true);p.hotbar=true;}
  }
});
canvas.addEventListener('pointermove',event=>{
  const p=pointers.get(event.pointerId);if(!p || blocked)return;
  const at=coordinates(event);
  if(p.gui)Module._LapisWeb_Pointer(at.x,at.y,1);
  else if(!p.hotbar)Module._LapisWeb_Look(at.x-p.x,at.y-p.y);
  p.x=at.x;p.y=at.y;
});
for(const name of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(name,event=>{
  const p=pointers.get(event.pointerId);if(!p)return;pointers.delete(event.pointerId);
  if(p.gui && ready)Module._LapisWeb_Pointer(p.x,p.y,0);
});
// Own touch events; prevent duplicate emulated mouse taps and engine touch gestures.
for(const name of ['touchstart','touchmove','touchend','touchcancel'])canvas.addEventListener(name,e=>{e.preventDefault();e.stopImmediatePropagation();},{capture:true,passive:false});
canvas.addEventListener('contextmenu',e=>e.preventDefault());
window.addEventListener('keydown',e=>{if(blocked){if(e.target===canvas)e.preventDefault();e.stopImmediatePropagation();}},{capture:true});
window.addEventListener('blur',reset);
document.addEventListener('visibilitychange',()=>{reset();updateGate();});
for(const name of ['resize','orientationchange'])window.addEventListener(name,resize);
for(const name of ['fullscreenchange','webkitfullscreenchange'])document.addEventListener(name,()=>{reset();resize();});
window.addEventListener('pointerdown',resumeAudio,{passive:true});
setInterval(()=>{
  if(!ready)return;
  const state=Module._LapisWeb_State();
  const next=!!(state&2);if(next!==menu){reset();menu=next;}
  touch.classList.toggle('menu',menu);document.getElementById('inventory-tools').hidden=!menu;
  document.getElementById('respawn').hidden=!(state&4);
  updateGate();
},150);
resize();
