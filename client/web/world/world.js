// SPDX-License-Identifier: GPL-3.0-only
import { material } from "./blocks.js";
export const key = (x, z) => `${x},${z}`;
// lapisclient v1 chunk: fixed little-endian header, u16 palette, u8 indices.
export function decodeChunk(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== 98830)
    throw Error("Invalid chunk length");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint8(0) !== 1 || view.getUint8(1) !== 1 ||
      view.getInt16(10, true) !== -64 || view.getUint16(12, true) !== 384)
    throw Error("Unsupported chunk format");
  const x = view.getInt32(2, true), z = view.getInt32(6, true);
  if (x < -2048 || x > 2047 || z < -2048 || z > 2047) throw Error("Invalid chunk coordinates");
  const palette = new Uint16Array(256);
  for (let i=0; i<256; i++) palette[i] = view.getUint16(14 + i*2, true);
  const blocks = new Uint16Array(98304);
  for (let i=0; i<blocks.length; i++) blocks[i] = palette[bytes[526+i]];
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
