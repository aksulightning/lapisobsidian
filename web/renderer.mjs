import {blocks} from './catalog.mjs';

const properties = new Map();
export function material(state) {
  if (properties.has(state)) return properties.get(state);
  const name = blocks[state] || 'unknown';
  const water = name.startsWith('water'), lava = name.startsWith('lava');
  const plant = /sapling|short_grass|dead_bush|dandelion|poppy|orchid|allium|tulip|daisy|cornflower|lily|wheat|fern|torch|lever|pressure_plate|redstone_wire|moss_carpet|^snow$/.test(name);
  let color = [.52,.55,.57];
  if (/dirt|mud|podzol/.test(name)) color = [.45,.30,.18];
  if (/grass_block|leaves|moss|cactus|wheat|sapling|short_grass/.test(name)) color = [.35,.58,.23];
  if (/log|wood|planks|chest|crafting|door|sign/.test(name)) color = [.57,.40,.22];
  if (/sand/.test(name)) color = [.80,.73,.49];
  if (/snow|diorite/.test(name)) color = [.86,.90,.89];
  if (/coal|bedrock|obsidian/.test(name)) color = [.21,.23,.28];
  if (/lapis/.test(name)) color = [.20,.36,.68];
  if (/iron/.test(name)) color = [.62,.55,.47];
  if (/gold/.test(name)) color = [.84,.66,.22];
  if (/diamond/.test(name)) color = [.24,.74,.73];
  if (/glass|ice/.test(name)) color = [.57,.77,.83];
  if (/poppy|redstone/.test(name)) color = [.72,.19,.15];
  if (/dandelion|torch/.test(name)) color = [.97,.79,.24];
  if (water) color = [.20,.43,.65];
  if (lava) color = [.95,.34,.07];
  const result = {name, color, solid: state !== 0 && !water && !lava && !plant, water, plant};
  properties.set(state, result); return result;
}
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
  dirty(x,z) { const c=this.chunks.get(this.key(x,z)); if (c) c.dirty=true; }
  add(chunk) {
    const old = this.chunks.get(this.key(chunk.x,chunk.z));
    if (old) chunk.mesh = old.mesh;
    this.chunks.set(this.key(chunk.x,chunk.z),chunk);
    for (const [dx,dz] of [[0,1],[0,-1],[1,0],[-1,0]]) this.dirty(chunk.x+dx,chunk.z+dz);
  }
  set(x,y,z,state) {
    const cx=Math.floor(x/16), cz=Math.floor(z/16), c=this.chunks.get(this.key(cx,cz));
    if (!c || y<0 || y>=320) return;
    c.data[y*256+((z%16+16)%16)*16+(x%16+16)%16]=state; c.dirty=true;
    for (const [dx,dz] of [[0,1],[0,-1],[1,0],[-1,0]]) this.dirty(cx+dx,cz+dz);
  }
  collides(x,y,z) {
    for (const px of [x-.29,x+.29]) for (const pz of [z-.29,z+.29])
      for (let py=Math.floor(y+.001);py<=Math.floor(y+1.79);py++) {
        const b=this.get(Math.floor(px),py,Math.floor(pz));
        if (b===undefined || material(b).solid) return true;
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
export function direction(yaw,pitch) { return [-Math.sin(yaw)*Math.cos(pitch),-Math.sin(pitch),Math.cos(yaw)*Math.cos(pitch)]; }
const faces = [
  {n:[0,-1,0], shade:.50, points:[[0,0,0],[1,0,0],[1,0,1],[0,0,1]]},
  {n:[0,1,0], shade:1, points:[[0,1,1],[1,1,1],[1,1,0],[0,1,0]]},
  {n:[0,0,-1], shade:.72, points:[[1,0,0],[0,0,0],[0,1,0],[1,1,0]]},
  {n:[0,0,1], shade:.82, points:[[0,0,1],[1,0,1],[1,1,1],[0,1,1]]},
  {n:[-1,0,0], shade:.65, points:[[0,0,0],[0,0,1],[0,1,1],[0,1,0]]},
  {n:[1,0,0], shade:.88, points:[[1,0,1],[1,0,0],[1,1,0],[1,1,1]]}
];
function cube(vertices,x,y,z,color,visible=()=>true,scale=[1,1,1],offset=[0,0,0]) {
  for (const face of faces) {
    if (!visible(face.n)) continue;
    for (const i of [0,1,2,0,2,3]) {
      const p=face.points[i];
      vertices.push(x+p[0]*scale[0]+offset[0],y+p[1]*scale[1]+offset[1],z+p[2]*scale[2]+offset[2],...color.map(v=>v*face.shade));
    }
  }
}
export class Renderer {
  constructor(canvas,world) {
    this.canvas=canvas; this.world=world;
    const gl=canvas.getContext('webgl',{antialias:false,alpha:false});
    if (!gl) throw Error('WebGL is unavailable. Enable hardware acceleration or try another browser.');
    this.gl=gl;
    const compile=(type,source)=>{
      const s=gl.createShader(type); gl.shaderSource(s,source); gl.compileShader(s);
      if (!gl.getShaderParameter(s,gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
      return s;
    };
    const vertex=compile(gl.VERTEX_SHADER,`
      attribute vec3 position, color;
      uniform vec3 eye, right, up, forward;
      uniform float aspect;
      varying vec3 tint; varying float distance;
      void main() {
        vec3 p=position-eye; float z=dot(p,forward);
        gl_Position=vec4(dot(p,right)/(0.7*aspect),dot(p,up)/0.7,1.00078*z-0.10004,z);
        tint=color; distance=length(p);
      }`);
    const fragment=compile(gl.FRAGMENT_SHADER,`
      precision mediump float;
      varying vec3 tint; varying float distance;
      void main() { gl_FragColor=vec4(mix(tint,vec3(.56,.73,.81),smoothstep(30.0,65.0,distance)),1.0); }`);
    this.program=gl.createProgram(); gl.attachShader(this.program,vertex); gl.attachShader(this.program,fragment); gl.linkProgram(this.program);
    if (!gl.getProgramParameter(this.program,gl.LINK_STATUS)) throw Error('Cannot initialize renderer');
    gl.deleteShader(vertex); gl.deleteShader(fragment);
    gl.useProgram(this.program); gl.enable(gl.DEPTH_TEST);
    this.uniforms=Object.fromEntries(['eye','right','up','forward','aspect'].map(n=>[n,gl.getUniformLocation(this.program,n)]));
    this.position=gl.getAttribLocation(this.program,'position'); this.color=gl.getAttribLocation(this.program,'color');
    this.entities=gl.createBuffer();
  }
  clear() {
    for (const chunk of this.world.chunks.values()) if (chunk.mesh) this.gl.deleteBuffer(chunk.mesh.buffer);
    this.world.chunks.clear();
  }
  mesh(chunk) {
    const vertices=[], {world,gl}=this;
    for (let y=0;y<256;y++) for (let z=0;z<16;z++) for (let x=0;x<16;x++) {
      const state=chunk.data[y*256+z*16+x]; if (!state) continue;
      const m=material(state), wx=chunk.x*16+x, wz=chunk.z*16+z;
      const jitter=((wx*13+y*7+wz*19)&7)*.006;
      cube(vertices,wx,y,wz,m.color.map(v=>v-jitter),n=>{
        const next=world.get(wx+n[0],y+n[1],wz+n[2]);
        return next!==undefined && (!next || (!material(next).solid && next!==state));
      },m.plant ? [.35,.55,.35] : [1,1,1],m.plant ? [.325,0,.325] : [0,0,0]);
    }
    if (!chunk.mesh) chunk.mesh={buffer:gl.createBuffer(),count:0};
    gl.bindBuffer(gl.ARRAY_BUFFER,chunk.mesh.buffer); gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.STATIC_DRAW);
    chunk.mesh.count=vertices.length/6; chunk.dirty=false;
  }
  drawBuffer(buffer,count) {
    const gl=this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
    gl.enableVertexAttribArray(this.position); gl.vertexAttribPointer(this.position,3,gl.FLOAT,false,24,0);
    gl.enableVertexAttribArray(this.color); gl.vertexAttribPointer(this.color,3,gl.FLOAT,false,24,12);
    gl.drawArrays(gl.TRIANGLES,0,count);
  }
  draw(player,entities,target) {
    const {gl,canvas,world}=this;
    const pixelRatio=Math.min(devicePixelRatio||1,1.5);
    const width=Math.round(canvas.clientWidth*pixelRatio),height=Math.round(canvas.clientHeight*pixelRatio);
    if (canvas.width!==width || canvas.height!==height) { canvas.width=width; canvas.height=height; }
    gl.viewport(0,0,width,height); gl.clearColor(.56,.73,.81,1); gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    const forward=direction(player.yaw,player.pitch),right=[-Math.cos(player.yaw),0,-Math.sin(player.yaw)];
    const up=[-Math.sin(player.yaw)*Math.sin(player.pitch),Math.cos(player.pitch),Math.cos(player.yaw)*Math.sin(player.pitch)];
    for (const [name,value] of [['eye',[player.x,player.y+1.62,player.z]],['right',right],['up',up],['forward',forward]]) gl.uniform3fv(this.uniforms[name],value);
    gl.uniform1f(this.uniforms.aspect,width/height);
    let rebuilt=0;
    for (const [key,chunk] of world.chunks) {
      if (Math.abs(chunk.x-world.center[0])>3 || Math.abs(chunk.z-world.center[1])>3) {
        if (chunk.mesh) gl.deleteBuffer(chunk.mesh.buffer); world.chunks.delete(key); continue;
      }
      if (chunk.dirty && rebuilt<2) { this.mesh(chunk); rebuilt++; }
      if (chunk.mesh) this.drawBuffer(chunk.mesh.buffer,chunk.mesh.count);
    }
    const vertices=[];
    for (const entity of entities.values()) {
      const item=entity.item;
      cube(vertices,entity.x,entity.y,entity.z,item ? [.9,.77,.45] : [.63,.38,.30],()=>true,item ? [.25,.25,.25] : [.6,1.6,.6],item ? [-.125,.12,-.125] : [-.3,0,-.3]);
    }
    if (target) {
      const [x,y,z]=target.p;
      // Small bright corner markers leave the target block itself visible.
      for (const dx of [0,.96]) for (const dy of [0,.96]) for (const dz of [0,.96])
        cube(vertices,x+dx-.015,y+dy-.015,z+dz-.015,[1,1,.85],()=>true,[.07,.07,.07]);
    }
    gl.bindBuffer(gl.ARRAY_BUFFER,this.entities); gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.DYNAMIC_DRAW);
    this.drawBuffer(this.entities,vertices.length/6);
  }
}
