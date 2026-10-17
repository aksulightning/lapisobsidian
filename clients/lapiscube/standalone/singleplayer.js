/* WebSocket-shaped LOCAL stream for the engine interop; never opens a socket. */
'use strict';
window.LapisSingleplayer={
  worker:null, stream:null, release:null,
  async start(){
    if(!navigator.locks)throw Error('Singleplayer saves require Web Locks. Open this file in a current desktop browser, or serve it over HTTPS or localhost.');
    await new Promise((resolve,reject)=>{
      navigator.locks.request('lapiscube-singleplayer-world',{ifAvailable:true},lock=>{
        if(!lock){reject(Error('This world is already open in another tab. Close it before playing here.'));return;}
        return new Promise(release=>{this.release=release;resolve();});
      }).catch(reject);
    });
    return new Promise((resolve,reject)=>{
      try{this.worker=window.LapisEmbedded?LapisEmbedded.worker():new Worker('server-worker.js');}
      catch(error){this.release();reject(error);return;}
      let booted=false;
      const timeout=setTimeout(()=>fail('The local server did not start. Reload to retry.'),90000);
      const fail=message=>{
        clearTimeout(timeout);
        if(!booted){this.worker.terminate();this.release();reject(Error(message));}
        else {document.getElementById('save-status').textContent=message;}
      };
      this.worker.onerror=e=>fail(e.message || 'Local server failed.');
      this.worker.onmessage=event=>{
        const m=event.data;
        if(m.type==='ready'){clearTimeout(timeout);booted=true;resolve();}
        else if(m.type==='data'){
          if(this.stream && this.stream.onmessage)this.stream.onmessage({data:m.data});
          this.worker.postMessage({type:'ack',size:m.data.byteLength});
        } else if(m.type==='input-ack' && this.stream){
          this.stream.bufferedAmount=Math.max(0,this.stream.bufferedAmount-m.size);
        } else if(m.type==='saved'){
          document.getElementById('save-status').textContent=m.quit?'World saved.':'Saved on this device.';
          if(m.quit){this.worker.terminate();this.release();location.reload();}
        } else if(m.type==='error')fail(m.message);
      };
    });
  },
  socket(){
    if(this.stream || !this.worker)throw Error('Singleplayer allows one local client.');
    const worker=this.worker;
    this.stream={CONNECTING:0,OPEN:1,CLOSING:2,CLOSED:3,readyState:0,bufferedAmount:0,binaryType:'arraybuffer',
      send(data){
        if(this.readyState!==1)throw Error('Local connection closed.');
        const copy=new Uint8Array(data).slice();this.bufferedAmount+=copy.length;
        worker.postMessage({type:'data',data:copy.buffer},[copy.buffer]);
      },
      close(){if(this.readyState===3)return;this.readyState=3;worker.postMessage({type:'stop'});if(this.onclose)this.onclose();}
    };
    setTimeout(()=>{this.stream.readyState=1;if(this.stream.onopen)this.stream.onopen();},0);
    return this.stream;
  }
};
const panel=document.createElement('aside');panel.id='singleplayer-save';
panel.innerHTML='<button id="save-world">Save world</button> <button id="quit-world">Save &amp; quit</button> <span id="save-status" role="status">Singleplayer · saved on this device</span>';
document.body.append(panel);
document.getElementById('save-world').onclick=()=>{
  if(LapisSingleplayer.worker){document.getElementById('save-status').textContent='Saving…';LapisSingleplayer.worker.postMessage({type:'save'});}
};
document.getElementById('quit-world').onclick=()=>{
  if(LapisSingleplayer.worker){document.getElementById('save-status').textContent='Saving…';LapisSingleplayer.worker.postMessage({type:'stop'});}
};
document.addEventListener('visibilitychange',()=>{
  if(document.hidden && LapisSingleplayer.worker)LapisSingleplayer.worker.postMessage({type:'save'});
});
