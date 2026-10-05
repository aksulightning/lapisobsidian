// SPDX-License-Identifier: GPL-3.0-only
import { Reader } from "../protocol/binary.js";
import { material } from "./blocks.js";
export const key = (x, z) => `${x},${z}`;
// Protocol 772 removes the old long-array length field from paletted containers.
function container(r, count) {
  const bits = r.u8(),
    out = new Uint16Array(count);
  if (bits === 0) {
    out.fill(r.varint());
    return out;
  }
  const palette = [];
  if (bits <= 8) {
    const n = r.varint();
    if (n < 1 || n > 256) throw Error("Invalid chunk palette");
    for (let i = 0; i < n; i++) palette.push(r.varint());
  }
  if (bits > 15 || bits < 1) throw Error("Unsupported chunk bit width");
  const perLong = Math.floor(64 / bits),
    mask = (1n << BigInt(bits)) - 1n;
  for (let base = 0; base < count; base += perLong) {
    const packed = r.u64();
    for (let j = 0; j < perLong && base + j < count; j++) {
      const value = Number((packed >> BigInt(j * bits)) & mask);
      if (palette.length && value >= palette.length)
        throw Error("Invalid palette index");
      out[base + j] = palette.length ? palette[value] : value;
    }
  }
  return out;
}
export function decodeChunk(bytes) {
  const r = new Reader(bytes);
  if (r.varint() !== 0x27) throw Error("Not a chunk");
  const x = r.i32(),
    z = r.i32();
  if (r.varint() !== 0) throw Error("Unsupported heightmap encoding");
  const size = r.varint(),
    section = new Reader(r.take(size)),
    blocks = new Uint16Array(16 * 16 * 384);
  for (let y = 0; y < 24; y++) {
    section.u16();
    blocks.set(container(section, 4096), y * 4096);
    container(section, 64);
  }
  section.done();
  return { x, z, blocks };
}
export class World {
  constructor() {
    this.chunks = new Map();
  }
  clear() {
    this.chunks.clear();
  }
  add(c) {
    this.chunks.set(key(c.x, c.z), c);
  }
  has(x, z) {
    return this.chunks.has(key(Math.floor(x / 16), Math.floor(z / 16)));
  }
  get(x, y, z) {
    if (y < -64 || y >= 320) return 0;
    const c = this.chunks.get(key(Math.floor(x / 16), Math.floor(z / 16)));
    return (
      c?.blocks[
        (y + 64) * 256 + (((z % 16) + 16) % 16) * 16 + (((x % 16) + 16) % 16)
      ] || 0
    );
  }
  set({ x, y, z, state }) {
    if (y < -64 || y >= 320) return;
    const c = this.chunks.get(key(Math.floor(x / 16), Math.floor(z / 16)));
    if (c)
      c.blocks[
        (y + 64) * 256 + (((z % 16) + 16) % 16) * 16 + (((x % 16) + 16) % 16)
      ] = state;
  }
  collides(x, y, z, height = 1.8) {
    for (let by = Math.floor(y); by <= Math.floor(y + height - 0.001); by++)
      for (let bz = Math.floor(z - 0.299); bz <= Math.floor(z + 0.299); bz++)
        for (
          let bx = Math.floor(x - 0.299);
          bx <= Math.floor(x + 0.299);
          bx++
        ) {
          const m = material(this.get(bx, by, bz));
          if (m.solid) {
            const box = m.box || [0, 0, 0, 1, m.height, 1];
            if (
              x + 0.299 > bx + box[0] &&
              x - 0.299 < bx + box[3] &&
              z + 0.299 > bz + box[2] &&
              z - 0.299 < bz + box[5] &&
              y + height > by + box[1] &&
              y < by + box[4] - 0.001
            )
              return true;
          }
        }
    return false;
  }
  ray(origin, direction, reach = 5) {
    let last = {
      x: Math.floor(origin.x),
      y: Math.floor(origin.y),
      z: Math.floor(origin.z),
    };
    for (let t = 0; t <= reach; t += 0.025) {
      const p = {
          x: Math.floor(origin.x + direction.x * t),
          y: Math.floor(origin.y + direction.y * t),
          z: Math.floor(origin.z + direction.z * t),
        },
        m = material(this.get(p.x, p.y, p.z));
      if (!m.air && !m.fluid) {
        p.face =
          p.y < last.y
            ? 1
            : p.y > last.y
              ? 0
              : p.z < last.z
                ? 3
                : p.z > last.z
                  ? 2
                  : p.x < last.x
                    ? 5
                    : 4;
        p.distance = t;
        return p;
      }
      last = p;
    }
    return null;
  }
}
