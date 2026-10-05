// SPDX-License-Identifier: GPL-3.0-only
import { materials } from "./world/blocks.js";
import { key } from "./world/world.js";
export function direction(yaw, pitch) {
  const y = (yaw * Math.PI) / 180,
    p = (pitch * Math.PI) / 180;
  return {
    x: -Math.sin(y) * Math.cos(p),
    y: -Math.sin(p),
    z: Math.cos(y) * Math.cos(p),
  };
}
function multiply(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++)
    for (let r = 0; r < 4; r++)
      for (let k = 0; k < 4; k++) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
  return out;
}
function cameraMatrix(p, aspect, far) {
  const f = direction(p.yaw, p.pitch),
    y = (p.yaw * Math.PI) / 180,
    right = [-Math.cos(y), 0, -Math.sin(y)];
  const up = [
    right[1] * f.z - right[2] * f.y,
    right[2] * f.x - right[0] * f.z,
    right[0] * f.y - right[1] * f.x,
  ];
  const eye = [p.x, p.y + (p.sneaking ? 1.35 : 1.62), p.z];
  const view = new Float32Array([
    right[0],
    up[0],
    -f.x,
    0,
    right[1],
    up[1],
    -f.y,
    0,
    right[2],
    up[2],
    -f.z,
    0,
    -right.reduce((s, v, i) => s + v * eye[i], 0),
    -up.reduce((s, v, i) => s + v * eye[i], 0),
    f.x * eye[0] + f.y * eye[1] + f.z * eye[2],
    1,
  ]);
  const n = 0.05,
    t = 1 / Math.tan((75 * Math.PI) / 360);
  const projection = new Float32Array([
    t / aspect,
    0,
    0,
    0,
    0,
    t,
    0,
    0,
    0,
    0,
    (far + n) / (n - far),
    -1,
    0,
    0,
    (2 * far * n) / (n - far),
    0,
  ]);
  return multiply(projection, view);
}
const vertex = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition; layout(location=1) in vec2 aUV; layout(location=2) in float aLight; layout(location=3) in float aAlpha;
uniform mat4 uMatrix; uniform vec3 uEye; uniform vec3 uOffset; uniform vec3 uScale;
out vec2 vUV; out float vLight; out float vAlpha; out float vDistance;
void main(){vec3 p=aPosition*uScale+uOffset;gl_Position=uMatrix*vec4(p,1.0);vUV=aUV;vLight=aLight;vAlpha=aAlpha;vDistance=length(p-uEye);}`;
const fragment = `#version 300 es
precision mediump float;
in vec2 vUV; in float vLight; in float vAlpha; in float vDistance;
uniform sampler2D uAtlas; uniform float uFog; out vec4 color;
void main(){vec4 texel=texture(uAtlas,vUV);if(texel.a<.5)discard;vec3 base=texel.rgb*vLight;float fog=smoothstep(uFog*.5,uFog,vDistance);color=vec4(mix(base,vec3(.49,.70,.85),fog),vAlpha);}`;
export class Renderer {
  constructor(canvas, settings, onError) {
    this.canvas = canvas;
    this.settings = settings;
    this.chunks = new Map();
    this.drawn = 0;
    this.onError = onError;
    const gl = (this.gl = canvas.getContext("webgl2", {
      alpha: false,
      antialias: false,
      preserveDrawingBuffer: false,
    }));
    if (!gl)
      throw Error(
        "WebGL2 is unavailable. Enable graphics acceleration or use a supported browser.",
      );
    const compile = (kind, source) => {
      const s = gl.createShader(kind);
      gl.shaderSource(s, source);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
        throw Error(gl.getShaderInfoLog(s));
      return s;
    };
    this.program = gl.createProgram();
    gl.attachShader(this.program, compile(gl.VERTEX_SHADER, vertex));
    gl.attachShader(this.program, compile(gl.FRAGMENT_SHADER, fragment));
    gl.linkProgram(this.program);
    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(this.program));
    this.u = Object.fromEntries(
      ["Matrix", "Eye", "Offset", "Scale", "Fog"].map((n) => [
        n,
        gl.getUniformLocation(this.program, "u" + n),
      ]),
    );
    gl.useProgram(this.program);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.clearColor(0.49, 0.7, 0.85, 1);
    // Original deterministic procedural atlas. No external/proprietary assets.
    const pixels = new Uint8Array(256 * 256 * 4);
    for (const m of materials)
      for (let y = 0; y < 16; y++)
        for (let x = 0; x < 16; x++) {
          const edge = x === 0 || y === 0 || x === 15 || y === 15,
            noise = ((x * 13 + y * 31 + m.index * 17) % 19) / 110 - 0.08;
          const shade = edge ? 0.84 : 1 + noise,
            at =
              ((Math.floor(m.index / 16) * 16 + y) * 256 +
                (m.index % 16) * 16 +
                x) *
              4;
          for (let i = 0; i < 3; i++)
            pixels[at + i] = Math.min(255, m.color[i] * 255 * shade);
          const stem = Math.abs(x - 7.5) < 1.2;
          const blade =
            y > 3 && Math.abs(Math.abs(x - 7.5) - (15 - y) * 0.45) < 1.6;
          pixels[at + 3] = !m.plant || stem || blade ? 255 : 0;
        }
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      256,
      256,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      pixels,
    );
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    const unit = [];
    for (const f of [
      [
        [0, 0, 0],
        [1, 0, 0],
        [1, 1, 0],
        [0, 1, 0],
      ],
      [
        [1, 0, 1],
        [0, 0, 1],
        [0, 1, 1],
        [1, 1, 1],
      ],
      [
        [0, 0, 1],
        [0, 0, 0],
        [0, 1, 0],
        [0, 1, 1],
      ],
      [
        [1, 0, 0],
        [1, 0, 1],
        [1, 1, 1],
        [1, 1, 0],
      ],
      [
        [0, 1, 0],
        [1, 1, 0],
        [1, 1, 1],
        [0, 1, 1],
      ],
      [
        [0, 0, 1],
        [1, 0, 1],
        [1, 0, 0],
        [0, 0, 0],
      ],
    ])
      for (const i of [0, 1, 2, 0, 2, 3]) unit.push(...f[i], 0.2, 0.2, 0.9, 1);
    this.entityMesh = this.upload(new Float32Array(unit));
    canvas.addEventListener("webglcontextlost", (e) => {
      e.preventDefault();
      onError("Graphics context was lost. Reload the client to reconnect.");
    });
  }
  upload(data) {
    const gl = this.gl,
      vao = gl.createVertexArray(),
      buffer = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    for (const [index, size, offset] of [
      [0, 3, 0],
      [1, 2, 12],
      [2, 1, 20],
      [3, 1, 24],
    ]) {
      gl.enableVertexAttribArray(index);
      gl.vertexAttribPointer(index, size, gl.FLOAT, false, 28, offset);
    }
    return { vao, buffer, count: data.length / 7 };
  }
  disposeMesh(m) {
    this.gl.deleteVertexArray(m.vao);
    this.gl.deleteBuffer(m.buffer);
  }
  update(m) {
    this.remove(m.x, m.z);
    this.chunks.set(key(m.x, m.z), {
      ...m,
      opaque: this.upload(m.opaque),
      transparent: this.upload(m.transparent),
    });
  }
  remove(x, z) {
    const k = key(x, z),
      c = this.chunks.get(k);
    if (c) {
      this.disposeMesh(c.opaque);
      this.disposeMesh(c.transparent);
      this.chunks.delete(k);
    }
  }
  clear() {
    for (const c of this.chunks.values()) {
      this.disposeMesh(c.opaque);
      this.disposeMesh(c.transparent);
    }
    this.chunks.clear();
  }
  render(p, entities) {
    const gl = this.gl,
      ratio = Math.min(
        devicePixelRatio || 1,
        this.settings.quality === "low"
          ? 1
          : this.settings.quality === "medium"
            ? 1.5
            : 2,
      ),
      w = Math.round(this.canvas.clientWidth * ratio),
      h = Math.round(this.canvas.clientHeight * ratio);
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
      gl.viewport(0, 0, w, h);
    }
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const far = this.settings.distance * 16 + 24,
      matrix = cameraMatrix(p, w / Math.max(1, h), far + 32);
    gl.useProgram(this.program);
    gl.uniformMatrix4fv(this.u.Matrix, false, matrix);
    gl.uniform3f(this.u.Eye, p.x, p.y + 1.62, p.z);
    gl.uniform1f(this.u.Fog, far);
    gl.uniform3f(this.u.Offset, 0, 0, 0);
    gl.uniform3f(this.u.Scale, 1, 1, 1);
    const visible = [];
    this.drawn = 0;
    for (const c of this.chunks.values()) {
      const distance = Math.hypot(c.x * 16 + 8 - p.x, c.z * 16 + 8 - p.z);
      if (distance > far + 12 || !this.inFrustum(c, matrix)) continue;
      visible.push({ c, distance });
      gl.bindVertexArray(c.opaque.vao);
      gl.drawArrays(gl.TRIANGLES, 0, c.opaque.count);
      this.drawn += c.opaque.count;
    }
    gl.bindVertexArray(this.entityMesh.vao);
    for (const e of entities.values()) {
      if (Math.hypot(e.x - p.x, e.z - p.z) > far) continue;
      const item = e.type === 69;
      gl.uniform3f(
        this.u.Offset,
        e.x - (item ? 0.13 : 0.3),
        e.y,
        e.z - (item ? 0.13 : 0.3),
      );
      gl.uniform3f(
        this.u.Scale,
        item ? 0.26 : 0.6,
        item ? 0.26 : e.type === 149 ? 1.8 : 1.1,
        item ? 0.26 : 0.6,
      );
      gl.drawArrays(gl.TRIANGLES, 0, this.entityMesh.count);
    }
    gl.uniform3f(this.u.Offset, 0, 0, 0);
    gl.uniform3f(this.u.Scale, 1, 1, 1);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);
    for (const { c } of visible.sort((a, b) => b.distance - a.distance)) {
      gl.bindVertexArray(c.transparent.vao);
      gl.drawArrays(gl.TRIANGLES, 0, c.transparent.count);
      this.drawn += c.transparent.count;
    }
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    this.canvas.dataset.rendered = String(this.drawn > 0);
  }
  inFrustum(c, m) {
    if (c.minY > c.maxY) return false;
    const corners = [];
    for (const x of [c.x * 16, c.x * 16 + 16])
      for (const y of [c.minY, c.maxY])
        for (const z of [c.z * 16, c.z * 16 + 16])
          corners.push([
            m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14],
            m[3] * x + m[7] * y + m[11] * z + m[15],
          ]);
    for (let a = 0; a < 3; a++)
      for (const sign of [-1, 1])
        if (corners.every((v) => sign * v[a] > v[3])) return false;
    return true;
  }
}
