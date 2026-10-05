// SPDX-License-Identifier: GPL-3.0-only
const encoder = new TextEncoder(),
  decoder = new TextDecoder("utf-8", { fatal: true });
export class Reader {
  constructor(bytes) {
    this.bytes = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.at = 0;
  }
  get remaining() {
    return this.bytes.length - this.at;
  }
  take(n) {
    if (!Number.isInteger(n) || n < 0 || n > this.remaining)
      throw Error("Truncated protocol packet");
    const v = this.bytes.subarray(this.at, this.at + n);
    this.at += n;
    return v;
  }
  number(type, n) {
    this.take(n);
    return this.view[type](this.at - n, false);
  }
  u8() {
    return this.number("getUint8", 1);
  }
  i16() {
    return this.number("getInt16", 2);
  }
  u16() {
    return this.number("getUint16", 2);
  }
  i32() {
    return this.number("getInt32", 4);
  }
  u32() {
    return this.number("getUint32", 4);
  }
  f32() {
    return this.number("getFloat32", 4);
  }
  f64() {
    return this.number("getFloat64", 8);
  }
  u64() {
    return this.number("getBigUint64", 8);
  }
  varint() {
    let v = 0;
    for (let i = 0; i < 5; i++) {
      const b = this.u8();
      if (i === 4 && b & 240) throw Error("Invalid VarInt");
      v |= (b & 127) << (7 * i);
      if (!(b & 128)) return v;
    }
    throw Error("Invalid VarInt");
  }
  string(max = 32767) {
    const n = this.varint();
    if (n > max) throw Error("Protocol string too long");
    return decoder.decode(this.take(n));
  }
  position() {
    const p = this.u64();
    return {
      x: Number(BigInt.asIntN(26, p >> 38n)),
      y: Number(BigInt.asIntN(12, p)),
      z: Number(BigInt.asIntN(26, p >> 12n)),
    };
  }
  done() {
    if (this.remaining) throw Error("Unexpected protocol fields");
  }
}
export class Writer {
  constructor() {
    this.bytes = new Uint8Array(128);
    this.at = 0;
    this.view = new DataView(this.bytes.buffer);
  }
  reserve(n) {
    if (this.at + n > this.bytes.length) {
      const b = new Uint8Array(Math.max(this.bytes.length * 2, this.at + n));
      b.set(this.bytes);
      this.bytes = b;
      this.view = new DataView(b.buffer);
    }
  }
  raw(v) {
    this.reserve(v.length);
    this.bytes.set(v, this.at);
    this.at += v.length;
    return this;
  }
  number(type, n, v) {
    this.reserve(n);
    this.view[type](this.at, v, false);
    this.at += n;
    return this;
  }
  u8(v) {
    return this.number("setUint8", 1, v);
  }
  u16(v) {
    return this.number("setUint16", 2, v);
  }
  i32(v) {
    return this.number("setInt32", 4, v);
  }
  f32(v) {
    return this.number("setFloat32", 4, v);
  }
  f64(v) {
    return this.number("setFloat64", 8, v);
  }
  u64(v) {
    return this.number("setBigUint64", 8, BigInt.asUintN(64, BigInt(v)));
  }
  varint(v) {
    v >>>= 0;
    do {
      const b = v & 127;
      v >>>= 7;
      this.u8(b | (v ? 128 : 0));
    } while (v);
    return this;
  }
  string(v) {
    const b = encoder.encode(v);
    return this.varint(b.length).raw(b);
  }
  position({ x, y, z }) {
    return this.u64(
      ((BigInt(x) & 0x3ffffffn) << 38n) |
        ((BigInt(z) & 0x3ffffffn) << 12n) |
        (BigInt(y) & 4095n),
    );
  }
  finish() {
    return this.bytes.slice(0, this.at);
  }
}
export function packet(id, write = () => {}) {
  const w = new Writer().varint(id);
  write(w);
  const body = w.finish();
  return new Writer().varint(body.length).raw(body).finish();
}
// Bounded amortized buffer: TCP/WebSocket boundaries never imply packet boundaries.
export class Framer {
  constructor(onPacket, max = 2 * 1024 * 1024) {
    this.onPacket = onPacket;
    this.max = max;
    this.buffer = new Uint8Array(8192);
    this.start = 0;
    this.end = 0;
  }
  push(bytes) {
    if (this.end - this.start + bytes.length > this.max + 65536)
      throw Error("Protocol receive buffer exceeded");
    if (this.end + bytes.length > this.buffer.length) {
      const size = this.end - this.start + bytes.length;
      const next = new Uint8Array(
        Math.max(size, Math.min(this.max + 65536, this.buffer.length * 2)),
      );
      next.set(this.buffer.subarray(this.start, this.end));
      this.end -= this.start;
      this.start = 0;
      this.buffer = next;
    }
    this.buffer.set(bytes, this.end);
    this.end += bytes.length;
    while (this.start < this.end) {
      let n = 0,
        p = 0;
      for (; p < 3; p++) {
        if (this.start + p === this.end) return;
        const b = this.buffer[this.start + p];
        n |= (b & 127) << (p * 7);
        if (!(b & 128)) break;
      }
      if (p === 3 || n < 1 || n > this.max)
        throw Error("Invalid protocol frame length");
      p++;
      if (this.end - this.start < p + n) return;
      const body = this.buffer.slice(this.start + p, this.start + p + n);
      this.start += p + n;
      this.onPacket(body);
    }
    this.start = this.end = 0;
  }
}
export function nbtText(r) {
  if (r.u8() !== 8) throw Error("Unsupported server text component");
  const b = r.take(r.u16());
  let out = "";
  for (let i = 0; i < b.length; ) {
    const a = b[i++];
    if (a < 128) out += String.fromCharCode(a);
    else if ((a & 224) === 192) {
      if (i >= b.length) throw Error("Invalid text");
      out += String.fromCharCode(((a & 31) << 6) | (b[i++] & 63));
    } else if ((a & 240) === 224) {
      if (i + 1 >= b.length) throw Error("Invalid text");
      out += String.fromCharCode(
        ((a & 15) << 12) | ((b[i++] & 63) << 6) | (b[i++] & 63),
      );
    } else throw Error("Invalid text");
  }
  return out;
}
