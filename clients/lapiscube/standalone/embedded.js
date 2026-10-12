/* All runtime bytes come from this HTML. Blob URLs never contact a server. */
'use strict';
(() => {
  const payload=document.getElementById('lapis-bundle');
  const files=JSON.parse(payload.textContent),urls=new Map();
  payload.remove();
  const bytes=name=>{
    if(!Object.hasOwn(files,name))throw Error('Missing embedded file: '+name);
    return Uint8Array.from(atob(files[name]),c=>c.charCodeAt(0));
  };
  const text=name=>new TextDecoder().decode(bytes(name));
  const url=name=>{
    if(!urls.has(name)){
      const type=name.endsWith('.js')?'text/javascript':name.endsWith('.wav')?'audio/wav':'application/octet-stream';
      urls.set(name,URL.createObjectURL(new Blob([bytes(name)],{type})));
    }
    return urls.get(name);
  };
  window.LapisEmbedded={
    configure(module){
      module.wasmBinary=bytes('LapisCube.wasm');
      module.lapisAssetURL=url;
    },
    clientURL:()=>url('LapisCube.js'),
    worker(){
      const source=text('server-worker.js')+'\nModule.wasmBinary=Uint8Array.from(atob('+JSON.stringify(files['LapisServer.wasm'])+'),c=>c.charCodeAt(0));\n'+text('LapisServer.js');
      const address=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
      try{return new Worker(address);}finally{URL.revokeObjectURL(address);}
    }
  };
  const credits=document.getElementById('embedded-credits');
  document.getElementById('show-credits').onclick=()=>credits.showModal();
  document.getElementById('close-credits').onclick=()=>credits.close();
  document.getElementById('download-notices').href=url('notices.zip');
})();
