/* Dedicated singleplayer server; compiled C owns all world and player state. */
'use strict';
let loaded=false,exited=false,exitOK=false;
var Module={
  arguments:[], pendingBytes:0, saving:false,
  print:text=>console.log('[server]',text), printErr:text=>console.error('[server]',text),
  onAbort:reason=>postMessage({type:'error',message:'Server stopped: '+reason}),
  preRun:[function(){
    FS.mkdir('/save'); FS.mount(IDBFS,{},'/save'); FS.chdir('/save');
    addRunDependency('saved-world');
    FS.syncfs(true,error=>{
      if(error){postMessage({type:'error',message:'Cannot open your saved world: '+error});return;}
      removeRunDependency('saved-world');
    });
  }],
  onRuntimeInitialized:()=>{loaded=true;},
  saveWorld(ok,quit){
    if(!ok){postMessage({type:'error',message:'Server could not save the world. Last completed save retained.'});return;}
    Module.saving=true;
    FS.syncfs(false,error=>{
      Module.saving=false;
      postMessage(error ? {type:'error',message:'World save failed. Keep this tab open and retry.'} : {type:'saved',quit});
    });
  },
  onExit:status=>{exited=true;exitOK=status===0;Module.saveWorld(exitOK,true);}
};
onmessage=event=>{
  const m=event.data;
  if(m.type==='ack'){Module.pendingBytes=Math.max(0,Module.pendingBytes-m.size);return;}
  if(!loaded)return;
  if(exited){
    if((m.type==='save' || m.type==='stop') && !Module.saving)Module.saveWorld(exitOK,true);
    return;
  }
  if(m.type==='data'){
    const data=new Uint8Array(m.data),ptr=Module._malloc(data.length);
    if(!ptr){postMessage({type:'error',message:'Server memory exhausted.'});return;}
    Module.HEAPU8.set(data,ptr);
    const ok=Module._standalone_input(ptr,data.length);Module._free(ptr);
    postMessage({type:'input-ack',size:data.length});
    if(!ok)postMessage({type:'error',message:'Local connection receive limit exceeded.'});
  } else if(m.type==='save')Module._standalone_save();
  else if(m.type==='stop')Module._standalone_stop();
};
importScripts('LapisServer.js');
