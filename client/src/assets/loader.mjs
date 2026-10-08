import {textures,textureRecord} from './registry.mjs';
export function fallbackPixels(){
 const data=new Uint8Array(16*16*4);
 for(let y=0;y<16;y++)for(let x=0;x<16;x++)data.set((x>>2&1)!==(y>>2&1)?[198,162,255,255]:[56,38,65,255],(y*16+x)*4);
 return data;
}
// One cached atlas for all terrain, entities and particles. Failures are visible
// and deterministic; unavailable tiles never trigger a proprietary download.
export class TextureAtlas {
 constructor({createCanvas=()=>document.createElement('canvas'),loadImage=path=>new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=()=>reject(Error(path));image.src=path;})}={}){
  this.canvas=createCanvas();this.canvas.width=this.canvas.height=128;
  this.ids=Object.keys(textures);this.indices=new Map(this.ids.map((id,i)=>[id,i]));
  this.cache=new Map();this.failed=[];this.loadImage=loadImage;
  this.ready=this.load();
 }
 image(id){
  const path=textureRecord(id).path;
  if(!this.cache.has(path))this.cache.set(path,this.loadImage(path));
  return this.cache.get(path);
 }
 async load(){
  const ctx=this.canvas.getContext('2d');ctx.imageSmoothingEnabled=false;
  await Promise.all(this.ids.map(async(id,i)=>{
   const x=i%8*16,y=Math.floor(i/8)*16;
   try{ctx.drawImage(await this.image(id),x,y,16,16);}
   catch{this.failed.push(id);ctx.putImageData(new ImageData(fallbackPixels(),16,16),x,y);}
  }));return this;
 }
 uv(id){const i=this.indices.get(id)??this.indices.get('fallback');const x=i%8*16,y=Math.floor(i/8)*16;return [(x+.5)/128,(y+.5)/128,(x+15.5)/128,(y+15.5)/128];}
}
