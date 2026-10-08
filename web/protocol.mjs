// The supported wire format is the one emitted by this repository (protocol 772).
// No Minecraft assets or third-party client code are bundled.
const encoder = new TextEncoder(), decoder = new TextDecoder();
export class Reader {
  constructor(bytes) { this.bytes = bytes; this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); this.pos = 0; }
  take(n) {
    if (!Number.isInteger(n) || n < 0 || n > this.bytes.length - this.pos) throw Error('Truncated server packet');
    const p = this.pos; this.pos += n; return this.bytes.subarray(p, this.pos);
  }
  byte() { return this.take(1)[0]; }
  number(method, n) { const p = this.pos; this.take(n); return this.view[method](p, false); }
  short() { return this.number('getUint16', 2); }
  int() { return this.number('getInt32', 4); }
  long() { return this.number('getBigUint64', 8); }
  float() { return this.number('getFloat32', 4); }
  double() { return this.number('getFloat64', 8); }
  varint() {
    let value = 0;
    for (let shift = 0; shift < 35; shift += 7) {
      const b = this.byte();
      if (shift === 28 && (b & 240)) throw Error('Invalid VarInt');
      value |= (b & 127) << shift;
      if (!(b & 128)) return value;
    }
    throw Error('Invalid VarInt');
  }
  string() { return decoder.decode(this.take(this.varint())); }
  position() {
    const n = this.long();
    return [Number(BigInt.asIntN(26, n >> 38n)), Number(BigInt.asIntN(12, n & 4095n)), Number(BigInt.asIntN(26, (n >> 12n) & 67108863n))];
  }
}
export class Writer {
  constructor() { this.bytes = []; }
  raw(bytes) { for (const b of bytes) this.bytes.push(b); return this; }
  byte(n) { this.bytes.push(n & 255); return this; }
  number(method, n, size) { const b = new Uint8Array(size); new DataView(b.buffer)[method](0, n, false); return this.raw(b); }
  short(n) { return this.number('setUint16', n, 2); }
  int(n) { return this.number('setInt32', n, 4); }
  long(n) { return this.number('setBigUint64', BigInt.asUintN(64, BigInt(n)), 8); }
  float(n) { return this.number('setFloat32', n, 4); }
  double(n) { return this.number('setFloat64', n, 8); }
  varint(n) {
    n >>>= 0;
    do { const b = n & 127; n >>>= 7; this.byte(b | (n ? 128 : 0)); } while (n);
    return this;
  }
  string(s) { const bytes = encoder.encode(s); return this.varint(bytes.length).raw(bytes); }
  position(x, y, z) { return this.long((BigInt.asUintN(26, BigInt(x)) << 38n) | (BigInt.asUintN(26, BigInt(z)) << 12n) | BigInt.asUintN(12, BigInt(y))); }
  finish() { return Uint8Array.from(this.bytes); }
}
export function packet(id, write = () => {}) {
  const w = new Writer().varint(id); write(w);
  return new Writer().varint(w.bytes.length).raw(w.bytes).finish();
}
// WebSocket messages are a byte stream: packet and message boundaries can differ.
export class PacketStream {
  constructor(onPacket) { this.pending = new Uint8Array(); this.onPacket = onPacket; }
  push(bytes) {
    if (this.pending.length + bytes.length > 8 * 1024 * 1024) throw Error('Server packet buffer exceeded');
    const all = new Uint8Array(this.pending.length + bytes.length);
    all.set(this.pending); all.set(bytes, this.pending.length);
    let start = 0;
    while (start < all.length) {
      let n = 0, prefix = 0;
      for (; prefix < 4 && start + prefix < all.length; prefix++) {
        const b = all[start + prefix]; n |= (b & 127) << (prefix * 7);
        if (!(b & 128)) { prefix++; break; }
      }
      if (all[start + prefix - 1] & 128) {
        if (prefix === 4) throw Error('Invalid packet length');
        break;
      }
      if (n <= 0 || n > 2 * 1024 * 1024) throw Error('Invalid packet length');
      if (all.length - start - prefix < n) break;
      const r = new Reader(all.subarray(start + prefix, start + prefix + n));
      this.onPacket(r.varint(), r); start += prefix + n;
    }
    this.pending = all.slice(start);
  }
}
export function readChunk(r) {
  const x = r.int(), z = r.int();
  if (r.varint() !== 0) throw Error('Unsupported heightmap encoding');
  const sections = new Reader(r.take(r.varint()));
  const data = new Uint16Array(16 * 16 * 320);
  for (let section = -4; section < 20; section++) {
    sections.short();
    const bits = sections.byte();
    if (bits === 0) {
      const state = sections.varint();
      if (section >= 0) data.fill(state, section * 4096, (section + 1) * 4096);
    } else if (bits === 8) {
      const length = sections.varint();
      if (length !== 256) throw Error('Unsupported block palette');
      const palette = Array.from({length}, () => sections.varint());
      const packed = sections.take(4096);
      if (section >= 0) for (let i = 0; i < 4096; i++) data[section * 4096 + i] = palette[packed[i ^ 7]];
    } else throw Error('Unsupported chunk encoding');
    if (sections.byte() !== 0) throw Error('Unsupported biome encoding');
    sections.varint();
  }
  if (sections.pos !== sections.bytes.length) throw Error('Unexpected chunk data');
  return {x, z, data, dirty: true};
}
