// Original protocol-772 adapter. SPDX-License-Identifier: MIT
const utf8 = new TextEncoder(), text = new TextDecoder();
export class Cursor {
  constructor(input) { this.data = input; this.offset = 0; this.view = new DataView(input.buffer,input.byteOffset,input.length); }
  bytes(count) { if(!Number.isSafeInteger(count)||count<0||this.offset+count>this.data.length)throw Error('Truncated packet');const result=this.data.subarray(this.offset,this.offset+count);this.offset+=count;return result; }
  u8() { return this.bytes(1)[0]; }
  scalar(method,count) {const at=this.offset;this.bytes(count);return this.view[method](at);}
  u16(){return this.scalar('getUint16',2);} i32(){return this.scalar('getInt32',4);} u64(){return this.scalar('getBigUint64',8);} f32(){return this.scalar('getFloat32',4);} f64(){return this.scalar('getFloat64',8);}
  vi(){let result=0;for(let i=0;i<5;i++){const octet=this.u8();if(i===4&&octet>15)throw Error('Invalid VarInt');result|=(octet&127)<<(7*i);if(octet<128)return result;}throw Error('Invalid VarInt');}
  str(){return text.decode(this.bytes(this.vi()));}
  pos(){const packed=this.u64();return [Number(BigInt.asIntN(26,packed>>38n)),Number(BigInt.asIntN(12,packed)),Number(BigInt.asIntN(26,packed>>12n))];}
}
export class Payload {
  constructor(){this.parts=[];}
  bytes(data){this.parts.push(...data);return this;}
  u8(n){this.parts.push(n&255);return this;}
  scalar(method,size,value){const buffer=new Uint8Array(size);new DataView(buffer.buffer)[method](0,value);return this.bytes(buffer);}
  u16(n){return this.scalar('setUint16',2,n);}i32(n){return this.scalar('setInt32',4,n);}u64(n){return this.scalar('setBigUint64',8,BigInt.asUintN(64,BigInt(n)));}f32(n){return this.scalar('setFloat32',4,n);}f64(n){return this.scalar('setFloat64',8,n);}
  vi(n){let remaining=n>>>0;while(remaining>=128){this.u8((remaining&127)|128);remaining>>>=7;}return this.u8(remaining);}
  str(value){const encoded=utf8.encode(value);return this.vi(encoded.length).bytes(encoded);}
  pos(x,y,z){return this.u64(BigInt.asUintN(26,BigInt(x))<<38n|BigInt.asUintN(26,BigInt(z))<<12n|BigInt.asUintN(12,BigInt(y)));}
  finish(){return Uint8Array.from(this.parts);}
}
export function frame(id,fill=()=>{}){const payload=new Payload().vi(id);fill(payload);return new Payload().vi(payload.parts.length).bytes(payload.parts).finish();}
export class Stream {
  constructor(deliver){this.deliver=deliver;this.buffer=new Uint8Array();}
  push(fragment){if(this.buffer.length+fragment.length>8388608)throw Error('Receive limit exceeded');const data=new Uint8Array(this.buffer.length+fragment.length);data.set(this.buffer);data.set(fragment,this.buffer.length);let offset=0;
    while(offset<data.length){let length=0,prefix=0,complete=false;for(;prefix<4&&offset+prefix<data.length;){const value=data[offset+prefix++];length|=(value&127)<<((prefix-1)*7);if(value<128){complete=true;break;}}
      if(!complete){if(prefix===4)throw Error('Oversized packet prefix');break;}if(length<1||length>2097152)throw Error('Invalid packet length');if(offset+prefix+length>data.length)break;
      const reader=new Cursor(data.subarray(offset+prefix,offset+prefix+length));this.deliver(reader.vi(),reader);offset+=prefix+length;
    }this.buffer=data.slice(offset);
  }
}
export function chunkData(cursor){const x=cursor.i32(),z=cursor.i32();if(cursor.vi()!==0)throw Error('Unsupported heightmaps');const sections=new Cursor(cursor.bytes(cursor.vi())),blocks=new Uint16Array(81920),biomes=[];
  for(let y=-4;y<20;y++){sections.u16();const width=sections.u8();if(width===0){const state=sections.vi();if(y>=0)blocks.fill(state,y*4096,(y+1)*4096);}else if(width===8){if(sections.vi()!==256)throw Error('Unexpected palette');const palette=Array.from({length:256},()=>sections.vi()),packed=sections.bytes(4096);if(y>=0)for(let cell=0;cell<4096;cell++)blocks[y*4096+cell]=palette[packed[cell^7]];}else throw Error('Unsupported section format');if(sections.u8()!==0)throw Error('Unsupported biome palette');const biome=sections.vi();if(y>=0)biomes.push(biome);}
  if(sections.offset!==sections.data.length)throw Error('Unexpected section bytes');return{x,z,blocks,biomes};
}
export function movement(player){return frame(0x1e,p=>p.f64(player.x).f64(player.y).f64(player.z).f32(player.rotationYaw).f32(player.rotationPitch).u8(player.onGround?1:0));}
export function stack(cursor){const count=cursor.vi();if(!count)return{count:0,item:0};const item=cursor.vi();if(cursor.vi()||cursor.vi())throw Error('Unsupported item components');return{count,item};}
