export function soundEvent(name){
 if(/note_block/.test(name))return 'note';
 if(/bucket|splash|swim|water|lava/.test(name))return 'water';
 if(/door|chest|trapdoor/.test(name))return 'door';
 if(/eat|burp/.test(name))return 'eat';
 if(/hurt|death|explode/.test(name))return 'hurt';
 if(/entity\./.test(name))return 'entity';
 return null; // Unknown sounds deliberately remain silent.
}
export function resolveSound(events,id){return events[id]||null;}
export function noiseSamples(length,seed=0x4c415049){
 const samples=new Float32Array(length);let n=seed>>>0;
 for(let i=0;i<length;i++){n^=n<<13;n^=n>>>17;n^=n<<5;samples[i]=(n>>>0)/2147483648-1;}
 return samples;
}
export class AudioSystem {
 constructor(){this.volume=.5;this.events={};this.ready=fetch('/assets/audio/events.json').then(r=>{if(!r.ok)throw Error('Audio definitions unavailable');return r.json();}).then(events=>this.events=events).catch(()=>{});this.active=0;}
 unlock(){try{this.context ||= new (window.AudioContext||window.webkitAudioContext)();this.context.resume().catch(()=>{});}catch{/* Silence on browsers without Web Audio. */}}
 play(id,{gain=1,pitch=1}={}){
  const e=resolveSound(this.events,id),c=this.context;
  if(!e||!c||c.state!=='running'||this.volume<=0||this.active>=24)return;
  const amplitude=c.createGain(),at=c.currentTime,duration=e.duration;
  amplitude.gain.setValueAtTime(Math.max(.0001,Math.min(1,e.gain*gain*this.volume)),at);
  amplitude.gain.exponentialRampToValueAtTime(.0001,at+duration);amplitude.connect(c.destination);
  let source;
  if(e.wave==='noise'){
   const buffer=c.createBuffer(1,Math.ceil(c.sampleRate*duration),c.sampleRate);
   buffer.copyToChannel(noiseSamples(buffer.length),0);source=c.createBufferSource();source.buffer=buffer;
   const filter=c.createBiquadFilter();filter.type='lowpass';filter.frequency.value=e.frequency*pitch;
   source.connect(filter);filter.connect(amplitude);source.onended=()=>{filter.disconnect();amplitude.disconnect();this.active--;};
  }else{source=c.createOscillator();source.type=e.wave;source.frequency.value=e.frequency*pitch;source.connect(amplitude);source.onended=()=>{amplitude.disconnect();this.active--;};}
  this.active++;source.start(at);source.stop(at+duration);
 }
}
