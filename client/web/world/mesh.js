// SPDX-License-Identifier: GPL-3.0-only
import { material } from "./blocks.js";
// Each vertex: position.xyz, atlas.uv, lighting, opacity.
const faces = [
  {
    n: [0, -1, 0],
    v: [
      [0, 0, 1],
      [1, 0, 1],
      [1, 0, 0],
      [0, 0, 0],
    ],
    light: 0.55,
  },
  {
    n: [0, 1, 0],
    v: [
      [0, 1, 0],
      [1, 1, 0],
      [1, 1, 1],
      [0, 1, 1],
    ],
    light: 1,
  },
  {
    n: [0, 0, -1],
    v: [
      [1, 0, 0],
      [1, 1, 0],
      [0, 1, 0],
      [0, 0, 0],
    ],
    light: 0.76,
  },
  {
    n: [0, 0, 1],
    v: [
      [0, 0, 1],
      [0, 1, 1],
      [1, 1, 1],
      [1, 0, 1],
    ],
    light: 0.76,
  },
  {
    n: [-1, 0, 0],
    v: [
      [0, 0, 0],
      [0, 1, 0],
      [0, 1, 1],
      [0, 0, 1],
    ],
    light: 0.86,
  },
  {
    n: [1, 0, 0],
    v: [
      [1, 0, 1],
      [1, 1, 1],
      [1, 1, 0],
      [1, 0, 0],
    ],
    light: 0.86,
  },
];
export function meshChunk(world, c) {
  const opaque = [],
    transparent = [];
  let minY = 320,
    maxY = -64;
  for (let sy = 0; sy < 384; sy++)
    for (let z = 0; z < 16; z++)
      for (let x = 0; x < 16; x++) {
        const state = c.blocks[sy * 256 + z * 16 + x],
          m = material(state);
        if (m.air) continue;
        const wx = c.x * 16 + x,
          wz = c.z * 16 + z,
          y = sy - 64;
        // Completely buried sections are culled by neighbors, including across chunks.
        for (const f of faces) {
          const neighbor = material(
            world.get(wx + f.n[0], y + f.n[1], wz + f.n[2]),
          );
          if (
            !m.box &&
            !neighbor.air &&
            neighbor.height === 1 &&
            !neighbor.transparent
          )
            continue;
          if (!m.box && m.transparent && neighbor.index === m.index) continue;
          const target = m.alpha < 1 ? transparent : opaque,
            tile = m.index,
            tx = tile % 16,
            ty = Math.floor(tile / 16);
          const uv = [
            [0.02, 0.98],
            [0.02, 0.02],
            [0.98, 0.02],
            [0.98, 0.98],
          ];
          for (const i of [0, 1, 2, 0, 2, 3]) {
            const v = f.v[i];
            const inset = m.plant ? 0.3 : 0;
            const box = m.box || [
              inset,
              0,
              inset,
              1 - inset,
              m.height,
              1 - inset,
            ];
            target.push(
              wx + box[0] + v[0] * (box[3] - box[0]),
              y + box[1] + v[1] * (box[4] - box[1]),
              wz + box[2] + v[2] * (box[5] - box[2]),
              (tx + uv[i][0]) / 16,
              (ty + uv[i][1]) / 16,
              f.light,
              m.alpha,
            );
          }
          minY = Math.min(minY, y);
          maxY = Math.max(maxY, y + 1);
        }
      }
  return {
    opaque: new Float32Array(opaque),
    transparent: new Float32Array(transparent),
    minY,
    maxY,
  };
}
