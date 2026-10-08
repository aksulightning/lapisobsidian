// Original tests. SPDX-License-Identifier: MIT
import test from "node:test";
import assert from "node:assert/strict";
import {
  Cursor,
  Payload,
  Stream,
  frame,
  movement,
  chunkData,
  stack,
} from "../adapter/wire.mjs";
import {
  stateToBlock,
  itemToBlock,
  blockToItem,
} from "../adapter/registry.mjs";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { resolve } from "node:path";
test("bounded fragmented packet framing and malformed lengths", () => {
  const received = [],
    stream = new Stream((id, p) => received.push([id, p.str()])),
    bytes = frame(8, (p) => p.str("lapis"));
  for (const b of bytes) stream.push(Uint8Array.of(b));
  assert.deepEqual(received, [[8, "lapis"]]);
  assert.throws(() =>
    new Stream(() => {}).push(Uint8Array.of(128, 128, 128, 128)),
  );
  assert.throws(() => new Cursor(Uint8Array.of(255, 255, 255, 255, 255)).vi());
  assert.throws(() => new Cursor(Uint8Array.of(2, 65)).str());
});
test("movement matches native server schema", () => {
  let found;
  new Stream((id, p) => {
    assert.equal(id, 0x1e);
    found = [p.f64(), p.f64(), p.f64(), p.f32(), p.f32(), p.u8()];
  }).push(
    movement({
      x: 8.5,
      y: 70,
      z: -3.5,
      rotationYaw: 90,
      rotationPitch: 12,
      onGround: true,
    }),
  );
  assert.deepEqual(found, [8.5, 70, -3.5, 90, 12, 1]);
});
test("actual compact section encoding and endian-packed local palette", () => {
  const section = new Payload();
  for (let y = -4; y < 20; y++) {
    section.u16(y === 0 ? 4096 : 0);
    if (y === 0) {
      section.u8(8).vi(256);
      for (let n = 0; n < 256; n++) section.vi(n + 1);
      section.bytes(Uint8Array.from({ length: 4096 }, (_, i) => (i ^ 7) & 255));
    } else section.u8(0).vi(0);
    section.u8(0).vi(2);
  }
  const encoded = new Payload()
    .i32(-2)
    .i32(3)
    .vi(0)
    .vi(section.parts.length)
    .bytes(section.parts)
    .finish();
  const chunk = chunkData(new Cursor(encoded));
  assert.equal(chunk.x, -2);
  assert.equal(chunk.z, 3);
  assert.equal(chunk.blocks[0], 1);
  assert.equal(chunk.blocks[255], 256);
  assert.equal(chunk.blocks[4096], 0);
  assert.equal(chunk.biomes.length, 20);
  assert.throws(() => chunkData(new Cursor(encoded.subarray(0, 20))));
});
test("registry maps factual identifiers, empty stacks and original fallback", () => {
  assert.equal(stateToBlock.get(0), 0);
  assert.equal(itemToBlock.get(28), 3);
  assert.equal(blockToItem.get(3), 28);
  assert.deepEqual(stack(new Cursor(Uint8Array.of(0))), { count: 0, item: 0 });
  assert.throws(() => stack(new Cursor(Uint8Array.of(1, 1, 1, 0))));
  assert.equal(itemToBlock.get(-1) || 0, 0);
});
test("retained third-party bytes and provenance are complete", async () => {
  const root = resolve(import.meta.dirname, ".."),
    manifest = JSON.parse(await readFile(root + "/provenance.json", "utf8"));
  assert.ok(manifest.files.length > 100);
  for (const file of manifest.files) {
    assert.ok(["CC-BY-NC-4.0", "MIT", "Apache-2.0"].includes(file.license));
    assert.match(
      file.source,
      /^https:\/\/github.com\/LabyStudio\/js-minecraft\/blob\/468f942/,
    );
    assert.equal(
      createHash("sha256")
        .update(await readFile(root + "/" + file.path))
        .digest("hex"),
      file.sha256,
      file.path,
    );
    assert.ok(file.author);
    assert.equal(file.commit, manifest.upstreamCommit);
  }
  for (const asset of manifest.assets) assert.equal(asset.license, "CC0-1.0");
});

test("missing images use cached original fallback; absent audio stays silent before gesture", async () => {
  const { default: Minecraft } = await import(
    "../src/js/net/minecraft/client/Minecraft.js"
  );
  const { default: SoundManager } = await import(
    "../src/js/net/minecraft/client/sound/SoundManager.js"
  );
  const previous = globalThis.document;
  globalThis.document = {
    createElement: () => ({ getContext: () => ({ drawImage() {} }) }),
  };
  try {
    const app = {
      resources: { "gui/background.png": { width: 32, height: 32 } },
    };
    const first = Minecraft.prototype.getThreeTexture.call(app, "missing");
    assert.equal(app.resources.missing, app.resources["gui/background.png"]);
    assert.equal(
      Minecraft.prototype.getThreeTexture.call(app, "missing"),
      first,
    );
    const sound = new SoundManager();
    assert.doesNotThrow(() => {
      sound.playSound("missing", 0, 0, 0, 1, 1);
      sound.playSound("missing", 0, 0, 0, 1, 1);
    });
    assert.deepEqual(sound.soundPool, {});
  } finally {
    globalThis.document = previous;
  }
});

test('connection validates origin, encodes real login and reports socket failure', async () => {
  const {default: Connection}=await import('../adapter/connection.mjs');
  const previousLocation=globalThis.location,previousSocket=globalThis.WebSocket;
  let socket;const screens=[];
  globalThis.location={origin:'http://127.0.0.1:8080',protocol:'http:',assign(){throw Error('Unexpected navigation');}};
  globalThis.WebSocket=class {static OPEN=1;constructor(url){this.url=url;this.readyState=0;this.bufferedAmount=0;this.sent=[];socket=this;}send(data){this.sent.push(data);}close(){this.readyState=3;}};
  const app={getSession:()=>({getProfile:()=>({getUsername:()=> 'TestPlayer',getCompactUUID:()=> '12345678901234567890123456789012'})}),loadWorld(){},displayScreen(screen){screens.push(screen);}};
  try {const invalid=new Connection(app);invalid.connect('ftp://127.0.0.1');assert.equal(invalid.closed,true);assert.match(screens.at(-1).message,/HTTP/);
    const connection=new Connection(app);connection.connect('127.0.0.1:8080');assert.equal(socket.url,'ws://127.0.0.1:8080/ws');socket.readyState=1;socket.onopen();const messages=[];const stream=new Stream((id,p)=>messages.push([id,p]));for(const bytes of socket.sent)stream.push(bytes);assert.equal(messages[0][1].vi(),772);assert.equal(messages[0][1].str(),'127.0.0.1');assert.equal(messages[0][1].u16(),25565);assert.equal(messages[0][1].vi(),2);assert.equal(messages[1][1].str(),'TestPlayer');assert.equal(messages[1][1].bytes(16).length,16);socket.onerror();assert.equal(connection.closed,true);assert.match(screens.at(-1).message,/Cannot connect/);
  } finally {globalThis.location=previousLocation;globalThis.WebSocket=previousSocket;}
});
