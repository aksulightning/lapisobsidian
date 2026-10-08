import assert from 'node:assert/strict';
import {Reader, Writer, PacketStream, packet, readChunk} from '../web/protocol.mjs';
const w=new Writer().varint(-1).varint(772).position(-4068,255,4068).double(-12.25).string('Lapis');
const r=new Reader(w.finish());
assert.equal(r.varint(),-1); assert.equal(r.varint(),772); assert.deepEqual(r.position(),[-4068,255,4068]);
assert.equal(r.double(),-12.25); assert.equal(r.string(),'Lapis'); assert.throws(()=>r.byte(),/Truncated/);
const packets=[];
const stream=new PacketStream((id,reader)=>packets.push([id,reader.string()]));
const a=packet(8,w=>w.string('first')),b=packet(8,w=>w.string('second'));
for(const byte of a)stream.push(Uint8Array.of(byte));
stream.push(b); assert.deepEqual(packets,[[8,'first'],[8,'second']]);
assert.throws(()=>new PacketStream(()=>{}).push(Uint8Array.of(128,128,128,128)),/length/);
assert.throws(()=>new PacketStream(()=>{}).push(Uint8Array.of(0)),/length/);
const sections=new Writer();
for(let s=-4;s<20;s++) {
  sections.short(4096);
  if(s<0)sections.byte(0).varint(85);
  else {
    sections.byte(8).varint(256);
    for(let i=0;i<256;i++)sections.varint(i);
    const data=new Uint8Array(4096);
    for(let i=0;i<4096;i++)data[i^7]=i%256;
    sections.raw(data);
  }
  sections.byte(0).varint(0);
}
const chunk=readChunk(new Reader(new Writer().int(-1).int(2).varint(0).varint(sections.bytes.length).raw(sections.finish()).finish()));
assert.equal(chunk.x,-1);assert.equal(chunk.z,2);assert.equal(chunk.data.length,81920);
for(let i=0;i<81920;i++)assert.equal(chunk.data[i],i%256);
console.log('web protocol codec and chunk coordinates: passed');
