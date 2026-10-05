// SPDX-License-Identifier: GPL-3.0-only
import type {Input} from './input';
import type {Settings} from './settings';
const vertex = `attribute vec3 position; attribute vec3 color; uniform mat4 projection; uniform mat4 view; varying vec3 shade; varying float distanceToCamera; void main(){vec4 p=view*vec4(position,1.0); distanceToCamera=length(p.xyz); shade=color; gl_Position=projection*p;}`;
const fragment = `precision mediump float; varying vec3 shade; varying float distanceToCamera; void main(){float fog=smoothstep(30.0,110.0,distanceToCamera); gl_FragColor=vec4(mix(shade,vec3(.085,.13,.21),fog),1.0);}`;
function perspective(aspect: number): Float32Array {
  const f = 1 / Math.tan(Math.PI / 6), near = .1, far = 160;
  return new Float32Array([f/aspect,0,0,0, 0,f,0,0, 0,0,(far+near)/(near-far),-1, 0,0,2*far*near/(near-far),0]);
}
function view(x: number, y: number, z: number, yaw: number, pitch: number): Float32Array {
  const sx = Math.cos(yaw), sz = Math.sin(yaw);
  const fx = Math.sin(yaw)*Math.cos(pitch), fy = Math.sin(pitch), fz = -Math.cos(yaw)*Math.cos(pitch);
  const ux = -Math.sin(yaw)*Math.sin(pitch), uy = Math.cos(pitch), uz = Math.cos(yaw)*Math.sin(pitch);
  return new Float32Array([sx,ux,-fx,0, 0,uy,-fy,0, sz,uz,-fz,0, -(sx*x+sz*z),-(ux*x+uy*y+uz*z),fx*x+fy*y+fz*z,1]);
}
function geometry(): Float32Array {
  const out: number[] = [];
  const faces = [ [0,1,2,0,2,3], [5,4,7,5,7,6], [4,0,3,4,3,7], [1,5,6,1,6,2], [3,2,6,3,6,7], [4,5,1,4,1,0] ];
  const lights = [.68,.8,.6,.9,1,.4];
  function box(x: number,y: number,z: number,w: number,h: number,d: number,c: number[]): void {
    const p = [[x,y,z],[x+w,y,z],[x+w,y+h,z],[x,y+h,z],[x,y,z+d],[x+w,y,z+d],[x+w,y+h,z+d],[x,y+h,z+d]];
    faces.forEach((face,i) => face.forEach(v => out.push(...p[v],...c.map(n => n*lights[i]))));
  }
  // Original, deterministic island geometry; no downloaded textures or models.
  for (let x=-14;x<14;x++) for (let z=-12;z<12;z++) {
    const radius = Math.hypot(x*.82,z);
    const edge = 10 + Math.sin(x*1.7+z*.8)*1.2;
    if (radius > edge) continue;
    const top = Math.max(0,Math.floor(4 - radius*.22 + Math.sin(x*.45)*.8 + Math.cos(z*.6)*.6));
    const depth = Math.max(2, Math.floor(9-radius*.6));
    box(x,top-depth,z,1,depth,1,[.19,.25,.35]);
    box(x,top,z,1,.32,1,[.32+.025*Math.sin(x),.46,.43]);
    if (radius<7 && (x*13+z*7)%37===0) { box(x+.1,top+.32,z+.1,.8,2.5+((x+z+30)%4),.8,[.24,.43,.85]); box(x+.3,top+3,z+.3,.4,2,.4,[.46,.65,1]); }
  }
  box(-2,3,-3,2,6,2,[.21,.4,.85]);box(-1.6,9,-2.6,1.2,1.8,1.2,[.52,.7,1]);box(1,3,-1,1.3,3,1.3,[.3,.5,.9]);
  for (let i=0;i<13;i++) { const a=i*2.4, r=17+i*.6;box(Math.sin(a)*r,-2-(i%4),Math.cos(a)*r,2+(i%3),2+(i%5),2+(i%2),[.2,.29,.43]); }
  // Distant plinths give the camera a horizon, without transparent sorting.
  for (let i=0;i<9;i++) box(-60+i*14,-14,-45-(i%3)*6,8,8+(i%4)*3,7,[.14,.21,.32]);
  return new Float32Array(out);
}
export class Renderer {
  private gl: WebGLRenderingContext;
  private program: WebGLProgram;
  private buffer: WebGLBuffer;
  private count: number;
  private projection: WebGLUniformLocation | null;
  private camera: WebGLUniformLocation | null;
  private frameId = 0;
  private last = 0;
  private stopped = false;
  private x = 22; private y = 17; private z = 28;
  private yaw = -.66; private pitch = -.35;
  constructor(private canvas: HTMLCanvasElement, private input: Input, private settings: Settings, private report: (message: string) => void) {
    const gl = canvas.getContext('webgl', {alpha:false,antialias:settings.quality !== 'low', powerPreference:'low-power'});
    if (!gl) throw new Error('WebGL is unavailable. Enable hardware acceleration or use a supported WebView.');
    this.gl = gl;
    const compile = (type: number, text: string): WebGLShader => {
      const shader = gl.createShader(type); if (!shader) throw new Error('Unable to create shader.');
      gl.shaderSource(shader,text);gl.compileShader(shader);
      if (!gl.getShaderParameter(shader,gl.COMPILE_STATUS)) { gl.deleteShader(shader); throw new Error('Unable to compile scene shaders.'); } return shader;
    };
    const program = gl.createProgram(); if (!program) throw new Error('Unable to create renderer.');
    const vs = compile(gl.VERTEX_SHADER,vertex), fs = compile(gl.FRAGMENT_SHADER,fragment);
    gl.attachShader(program,vs);gl.attachShader(program,fs);gl.linkProgram(program);gl.deleteShader(vs);gl.deleteShader(fs);
    if (!gl.getProgramParameter(program,gl.LINK_STATUS)) throw new Error('Unable to link scene shaders.');
    this.program = program; gl.useProgram(program);
    const buffer = gl.createBuffer(); if (!buffer) throw new Error('Unable to allocate scene.');
    this.buffer = buffer; gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
    const data = geometry(); this.count=data.length/6; gl.bufferData(gl.ARRAY_BUFFER,data,gl.STATIC_DRAW);
    for (const [name,offset] of [['position',0],['color',12]] as const) { const location=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(location);gl.vertexAttribPointer(location,3,gl.FLOAT,false,24,offset); }
    this.projection=gl.getUniformLocation(program,'projection');this.camera=gl.getUniformLocation(program,'view');
    gl.enable(gl.DEPTH_TEST);gl.clearColor(.085,.13,.21,1);
    canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();this.stopped=true;cancelAnimationFrame(this.frameId);this.report('Graphics context was lost. Restart the application to restore the scene.');});
    this.frameId=requestAnimationFrame(time=>this.draw(time));
  }
  private draw(time: number): void {
    if (this.stopped) return;
    this.frameId=requestAnimationFrame(t=>this.draw(t));
    if (document.hidden || time-this.last < 1000/this.settings.fps-.5) return;
    const dt=Math.min((time-this.last)/1000,.05);this.last=time;
    const gl=this.gl, dpr=Math.min(devicePixelRatio || 1,{low:1,medium:1.5,high:2}[this.settings.quality]);
    const w=Math.max(1,Math.floor(this.canvas.clientWidth*dpr)), h=Math.max(1,Math.floor(this.canvas.clientHeight*dpr));
    if(this.canvas.width!==w || this.canvas.height!==h){this.canvas.width=w;this.canvas.height=h;gl.viewport(0,0,w,h);}
    const input=this.input.frame();
    if(this.input.active){
      this.yaw+=input.lookX*.0025;this.pitch=Math.max(-1.4,Math.min(1.4,this.pitch-input.lookY*.0025));
      const length=Math.max(1,Math.hypot(input.forward,input.strafe)), step=dt*9/length;
      this.x+=(Math.sin(this.yaw)*input.forward+Math.cos(this.yaw)*input.strafe)*step;
      this.z+=(-Math.cos(this.yaw)*input.forward+Math.sin(this.yaw)*input.strafe)*step;
      this.y+=input.vertical*dt*9;
      this.x=Math.max(-65,Math.min(65,this.x));this.y=Math.max(-10,Math.min(65,this.y));this.z=Math.max(-65,Math.min(65,this.z));
    }
    gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.uniformMatrix4fv(this.projection,false,perspective(w/h));gl.uniformMatrix4fv(this.camera,false,view(this.x,this.y,this.z,this.yaw,this.pitch));gl.drawArrays(gl.TRIANGLES,0,this.count);
    this.canvas.dataset.rendered='true';
  }
  dispose(): void {this.stopped=true;cancelAnimationFrame(this.frameId);this.gl.deleteBuffer(this.buffer);this.gl.deleteProgram(this.program);}
}
