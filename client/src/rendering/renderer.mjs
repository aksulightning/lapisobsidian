import {TextureAtlas} from '../assets/loader.mjs';
import {blockTexture,itemTexture,entityModel} from '../assets/registry.mjs';
import {material} from './material.mjs';

export function direction(yaw,pitch) { return [-Math.sin(yaw)*Math.cos(pitch),-Math.sin(pitch),Math.cos(yaw)*Math.cos(pitch)]; }
const faces = [
  {n:[0,-1,0], shade:.50, points:[[0,0,0],[1,0,0],[1,0,1],[0,0,1]]},
  {n:[0,1,0], shade:1, points:[[0,1,1],[1,1,1],[1,1,0],[0,1,0]]},
  {n:[0,0,-1], shade:.72, points:[[1,0,0],[0,0,0],[0,1,0],[1,1,0]]},
  {n:[0,0,1], shade:.82, points:[[0,0,1],[1,0,1],[1,1,1],[0,1,1]]},
  {n:[-1,0,0], shade:.65, points:[[0,0,0],[0,0,1],[0,1,1],[0,1,0]]},
  {n:[1,0,0], shade:.88, points:[[1,0,1],[1,0,0],[1,1,0],[1,1,1]]}
];
function cube(vertices,x,y,z,color,visible=()=>true,scale=[1,1,1],offset=[0,0,0],texture=()=>[0,0,1,1],alpha=1) {
  for (let f=0;f<faces.length;f++) {
    const face=faces[f];if(!visible(face.n))continue;
    const [u0,v0,u1,v1]=texture(f),uv=[[u0,v1],[u1,v1],[u1,v0],[u0,v0]];
    for(const i of [0,1,2,0,2,3]){
      const p=face.points[i];vertices.push(x+p[0]*scale[0]+offset[0],y+p[1]*scale[1]+offset[1],z+p[2]*scale[2]+offset[2],...color.map(v=>v*face.shade),...uv[i],alpha);
    }
  }
}
export class Renderer {
  constructor(canvas,world) {
    this.canvas=canvas;this.world=world;this.distance=3;this.time=6000;this.particles=[];
    const gl=canvas.getContext('webgl',{antialias:false,alpha:false});
    if(!gl)throw Error('WebGL is unavailable. Enable hardware acceleration or try another browser.');
    this.gl=gl;
    const compile=(type,source)=>{const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));return shader;};
    const vertex=compile(gl.VERTEX_SHADER,`
      attribute vec3 position,color; attribute vec2 uv; attribute float opacity;
      uniform vec3 eye,right,up,forward;uniform float aspect;
      varying vec3 tint;varying vec2 texcoord;varying float distance,alpha;
      void main(){vec3 p=position-eye;float z=dot(p,forward);
        gl_Position=vec4(dot(p,right)/(0.7*aspect),dot(p,up)/0.7,1.00078*z-0.10004,z);
        tint=color;texcoord=uv;alpha=opacity;distance=length(p);}`);
    const fragment=compile(gl.FRAGMENT_SHADER,`
      precision mediump float;uniform sampler2D atlas;uniform vec3 sky;uniform float daylight,far;
      varying vec3 tint;varying vec2 texcoord;varying float distance,alpha;
      void main(){vec4 tex=texture2D(atlas,texcoord);if(tex.a<0.1)discard;
        gl_FragColor=vec4(mix(tex.rgb*tint*daylight,sky,smoothstep(far*0.5,far,distance)),tex.a*alpha);}`);
    this.program=gl.createProgram();gl.attachShader(this.program,vertex);gl.attachShader(this.program,fragment);gl.linkProgram(this.program);
    if(!gl.getProgramParameter(this.program,gl.LINK_STATUS))throw Error('Cannot initialize renderer');
    gl.deleteShader(vertex);gl.deleteShader(fragment);gl.useProgram(this.program);gl.enable(gl.DEPTH_TEST);
    this.uniforms=Object.fromEntries(['eye','right','up','forward','aspect','sky','daylight','far'].map(n=>[n,gl.getUniformLocation(this.program,n)]));
    this.attributes=['position','color','uv','opacity'].map(n=>gl.getAttribLocation(this.program,n));
    this.entities=gl.createBuffer();this.texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,this.texture);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([198,162,255,255]));
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
    this.atlas=new TextureAtlas();this.assetsReady=this.atlas.ready.then(()=>{
      gl.bindTexture(gl.TEXTURE_2D,this.texture);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,this.atlas.canvas);
      return this.atlas.failed;
    });
  }
  release(mesh){if(mesh)for(const buffer of [mesh.buffer,mesh.transparent])if(buffer)this.gl.deleteBuffer(buffer);}
  clear(){for(const c of this.world.chunks.values())this.release(c.mesh);this.world.chunks.clear();this.particles=[];}
  burst(p,state){
    const name=material(state).name;
    for(let i=0;i<16&&this.particles.length<256;i++)this.particles.push({p:p.map(v=>v+.5),v:[Math.sin(i*2.4)*2,1+(i%4)*.7,Math.cos(i*2.4)*2],life:.6,texture:i%5===0?'mote':blockTexture(name)});
  }
  mesh(chunk,deadline){
    const build=chunk.build ||= {vertices:[],transparent:[],y:0,min:256,max:0};
    const {world,gl}=this;
    while(build.y<256){
      const y=build.y++;
      for(let z=0;z<16;z++)for(let x=0;x<16;x++){
        const state=chunk.data[y*256+z*16+x];if(!state)continue;
        const m=material(state),wx=chunk.x*16+x,wz=chunk.z*16+z;
        const vertices=m.transparent?build.transparent:build.vertices;
        const biome=chunk.biomes?.[Math.floor(y/16)]||0;
        const tint=/grass_block|leaves/.test(m.name)?(biome===2?[1,.90,.73]:biome===7?[.82,1,.9]:[1,1,1]):[1,1,1];
        const before=vertices.length;
        cube(vertices,wx,y,wz,tint,n=>{
          const next=world.get(wx+n[0],y+n[1],wz+n[2]);
          return next!==undefined&&(!next||(!material(next).solid&&next!==state)||m.shape);
        },m.scale||[1,1,1],m.offset||[0,0,0],f=>this.atlas.uv(blockTexture(m.name,f)),m.transparent ? .68 : 1);
        if(vertices.length>before){build.min=Math.min(build.min,y);build.max=Math.max(build.max,y+1);}
      }
      if(performance.now()>=deadline)return;
    }
    chunk.mesh ||= {buffer:gl.createBuffer(),transparent:gl.createBuffer()};
    for(const [key,data] of [['buffer',build.vertices],['transparent',build.transparent]]){
      gl.bindBuffer(gl.ARRAY_BUFFER,chunk.mesh[key]);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(data),gl.STATIC_DRAW);
      chunk.mesh[key+'Count']=data.length/9;
    }
    chunk.mesh.min=build.min;chunk.mesh.max=build.max;chunk.dirty=false;chunk.build=null;
  }
  drawBuffer(buffer,count){
    if(!count)return;const gl=this.gl;gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
    let offset=0;for(const [i,size] of [3,3,2,1].entries()){gl.enableVertexAttribArray(this.attributes[i]);gl.vertexAttribPointer(this.attributes[i],size,gl.FLOAT,false,36,offset);offset+=size*4;}
    gl.drawArrays(gl.TRIANGLES,0,count);
  }
  visible(chunk,eye,forward,right,up,aspect){
    if(!chunk.mesh)return false;
    const m=chunk.mesh,p=[chunk.x*16+8-eye[0],(m.min+m.max)/2-eye[1],chunk.z*16+8-eye[2]],radius=Math.hypot(12,(m.max-m.min)/2);
    const dot=v=>p.reduce((a,n,i)=>a+n*v[i],0),z=dot(forward);
    return z+radius>0&&Math.abs(dot(right))<z*.7*aspect+radius*Math.hypot(1,.7*aspect)&&Math.abs(dot(up))<z*.7+radius*1.23;
  }
  draw(player,entities,target){
    const {gl,canvas,world}=this;const pixelRatio=Math.min(devicePixelRatio||1,1.5);
    const width=Math.max(1,Math.round(canvas.clientWidth*pixelRatio)),height=Math.max(1,Math.round(canvas.clientHeight*pixelRatio));
    if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
    const daylight=.24+.76*Math.max(0,Math.cos((this.time-6000)/24000*Math.PI*2));
    const underwater=material(world.get(Math.floor(player.x),Math.floor(player.y+1.62),Math.floor(player.z))||0).water;
    const sky=(underwater?[.16,.32,.47]:[.56,.73,.81]).map(v=>v*daylight);
    gl.viewport(0,0,width,height);gl.clearColor(...sky,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    const forward=direction(player.yaw,player.pitch),right=[-Math.cos(player.yaw),0,-Math.sin(player.yaw)],up=[-Math.sin(player.yaw)*Math.sin(player.pitch),Math.cos(player.pitch),Math.cos(player.yaw)*Math.sin(player.pitch)];
    const eye=[player.x,player.y+(player.sneaking?1.4:1.62),player.z];
    for(const [name,value] of [['eye',eye],['right',right],['up',up],['forward',forward],['sky',sky]])gl.uniform3fv(this.uniforms[name],value);
    gl.uniform1f(this.uniforms.aspect,width/height);gl.uniform1f(this.uniforms.daylight,daylight);gl.uniform1f(this.uniforms.far,underwater?22:16*this.distance+18);
    const deadline=performance.now()+5,chunks=[...world.chunks].sort(([,a],[,b])=>Math.hypot(a.x*16-player.x,a.z*16-player.z)-Math.hypot(b.x*16-player.x,b.z*16-player.z));
    const visible=[];gl.disable(gl.BLEND);gl.depthMask(true);
    for(const [key,c] of chunks){
      if(Math.abs(c.x-world.center[0])>3||Math.abs(c.z-world.center[1])>3){this.release(c.mesh);world.chunks.delete(key);continue;}
      if(Math.abs(c.x-Math.floor(player.x/16))>this.distance||Math.abs(c.z-Math.floor(player.z/16))>this.distance)continue;
      if(c.dirty&&performance.now()<deadline)this.mesh(c,deadline);
      if(this.visible(c,eye,forward,right,up,width/height)){visible.push(c);this.drawBuffer(c.mesh.buffer,c.mesh.bufferCount);}
    }
    const vertices=[];
    for(const e of entities.values()){
      const model=entityModel(e.type),texture=e.itemId?itemTexture(e.itemName,false):model.texture;
      const tex=()=>this.atlas.uv(texture),color=[1,1,1];
      const part=(offset,scale)=>cube(vertices,e.x,e.y,e.z,color,()=>true,scale,offset,tex);
      if(model.shape==='item'){part([-.125,.12,-.125],[.25,.25,.25]);continue;}
      if(model.shape==='quadruped'||model.shape==='crawler'){
        part([-.4,.35,-.5],[.8,.5,1]);part([-.25,.7,-.6],[.5,.4,.4]);
        for(const x of [-.32,.2])for(const z of [-.35,.3])part([x,0,z],[.14,.4,.14]);
      }else if(model.shape==='floating'){part([-.7,.7,-.7],[1.4,1.2,1.4]);}
      else{part([-.3,.65,-.18],[.6,.7,.36]);part([-.25,1.35,-.25],[.5,.5,.5]);for(const x of [-.26,.06])part([x,0,-.15],[.2,.65,.3]);for(const x of [-.48,.3])part([x,.6,-.14],[.18,.7,.28]);}
    }
    if(target){const [x,y,z]=target.p;for(const dx of [0,.96])for(const dy of [0,.96])for(const dz of [0,.96])cube(vertices,x+dx-.015,y+dy-.015,z+dz-.015,[1,1,1],()=>true,[.07,.07,.07],[0,0,0],()=>this.atlas.uv('frost'));}
    const now=performance.now(),dt=Math.min(.1,(now-(this.lastDraw||now))/1000);this.lastDraw=now;
    this.particles=this.particles.filter(p=>{p.life-=dt;if(p.life<=0)return false;p.v[1]-=5*dt;p.p=p.p.map((v,i)=>v+p.v[i]*dt);cube(vertices,...p.p,[1,1,1],()=>true,[.06,.06,.06],[0,0,0],()=>this.atlas.uv(p.texture));return true;});
    gl.bindBuffer(gl.ARRAY_BUFFER,this.entities);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.DYNAMIC_DRAW);this.drawBuffer(this.entities,vertices.length/9);
    gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.depthMask(false);
    for(const c of visible.reverse())this.drawBuffer(c.mesh.transparent,c.mesh.transparentCount);
    gl.depthMask(true);gl.disable(gl.BLEND);
  }
}
