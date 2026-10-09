import {material} from '../rendering/material.mjs';

export class World {
  constructor() { this.chunks = new Map(); this.center = [0,0]; }
  key(x,z) { return `${x},${z}`; }
  get(x,y,z) {
    if (y < 0) return 85;
    if (y >= 320) return 0;
    const c = this.chunks.get(this.key(Math.floor(x/16),Math.floor(z/16)));
    if (!c) return undefined;
    return c.data[y*256 + ((z%16+16)%16)*16 + (x%16+16)%16];
  }
  dirty(x,z) { const c=this.chunks.get(this.key(x,z)); if (c) {c.dirty=true;c.build=null;} }
  add(chunk) {
    const old = this.chunks.get(this.key(chunk.x,chunk.z));
    if (old) chunk.mesh = old.mesh;
    this.chunks.set(this.key(chunk.x,chunk.z),chunk);
    for (const [dx,dz] of [[0,1],[0,-1],[1,0],[-1,0]]) this.dirty(chunk.x+dx,chunk.z+dz);
  }
  set(x,y,z,state) {
    const cx=Math.floor(x/16), cz=Math.floor(z/16), c=this.chunks.get(this.key(cx,cz));
    if (!c || y<0 || y>=320) return;
    c.data[y*256+((z%16+16)%16)*16+(x%16+16)%16]=state; this.dirty(cx,cz);
    for (const [dx,dz] of [[0,1],[0,-1],[1,0],[-1,0]]) this.dirty(cx+dx,cz+dz);
  }
  collides(x,y,z) {
    for(let bx=Math.floor(x-.29);bx<=Math.floor(x+.29);bx++)
      for(let bz=Math.floor(z-.29);bz<=Math.floor(z+.29);bz++)
        for(let by=Math.floor(y+.001);by<=Math.floor(y+1.79);by++){
          const state=this.get(bx,by,bz);if(state===undefined)return true;
          const m=material(state);if(!m.solid)continue;
          const [sx,sy,sz]=m.scale,[ox,oy,oz]=m.offset;
          if(x+.29>bx+ox&&x-.29<bx+ox+sx&&z+.29>bz+oz&&z-.29<bz+oz+sz&&y+1.79>by+oy&&y+.001<by+oy+sy)return true;
        }
    return false;
  }
  ray(eye,direction,reach=5) {
    let previous=eye.map(Math.floor);
    for (let t=0;t<=reach;t+=.035) {
      const p=eye.map((v,i)=>Math.floor(v+direction[i]*t));
      const b=this.get(...p);
      if (b && !/^water|^lava/.test(material(b).name)) {
        const delta=previous.map((v,i)=>v-p[i]);
        const face=delta[1]===-1 ? 0 : delta[1]===1 ? 1 : delta[2]===-1 ? 2 : delta[2]===1 ? 3 : delta[0]===-1 ? 4 : 5;
        return {p,face,state:b,previous};
      }
      previous=p;
    }
    return null;
  }
}
