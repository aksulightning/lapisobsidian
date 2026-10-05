// SPDX-License-Identifier: GPL-3.0-only
import { World, decodeChunk, key } from "./world.js";
import { meshChunk } from "./mesh.js";
let world = new World(),
  generation = 0,
  timer;
const dirty = new Set();
function mark(x, z) {
  for (const [dx, dz] of [
    [0, 0],
    [1, 0],
    [-1, 0],
    [0, 1],
    [0, -1],
  ])
    if (world.chunks.has(key(x + dx, z + dz))) dirty.add(key(x + dx, z + dz));
  schedule();
}
function schedule() {
  if (!timer) timer = setTimeout(work, 0);
}
function work() {
  timer = null;
  const k = dirty.values().next().value;
  if (k === undefined) return;
  dirty.delete(k);
  const c = world.chunks.get(k);
  if (c) {
    const mesh = meshChunk(world, c);
    postMessage({ type: "mesh", generation, x: c.x, z: c.z, ...mesh }, [
      mesh.opaque.buffer,
      mesh.transparent.buffer,
    ]);
  }
  if (dirty.size) schedule();
}
self.onmessage = ({ data: m }) => {
  try {
    if (m.type === "reset") {
      world = new World();
      generation = m.generation;
      dirty.clear();
      return;
    }
    if (m.generation !== generation) return;
    if (m.type === "chunk") {
      const c = decodeChunk(m.bytes);
      world.add(c);
      const blocks = c.blocks.slice();
      postMessage({ type: "chunk", generation, x: c.x, z: c.z, blocks }, [
        blocks.buffer,
      ]);
      mark(c.x, c.z);
    }
    if (m.type === "block") {
      world.set(m.block);
      mark(Math.floor(m.block.x / 16), Math.floor(m.block.z / 16));
    }
    if (m.type === "unload") {
      world.chunks.delete(key(m.x, m.z));
      dirty.delete(key(m.x, m.z));
      mark(m.x, m.z);
    }
  } catch (error) {
    postMessage({ type: "error", generation, message: error.message });
  }
};
