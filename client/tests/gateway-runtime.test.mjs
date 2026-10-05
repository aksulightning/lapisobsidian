// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import net from "node:net";
import { once } from "node:events";
import { runtime } from "./https-runtime.mjs";



test("real Workers runtime serves HTTPS API sessions with concurrent reads and writes", { timeout: 30000 }, async () => {
  const peers = new Set();
  const server = net.createServer(peer => {
    peers.add(peer); peer.on("close", () => peers.delete(peer));
    peer.on("error", () => {});
    peer.on("data", data => peer.write(data));
  });
  server.listen(0, "127.0.0.1"); await once(server, "listening");
  const mf = runtime(server.address().port);
  const ids = [];
  try {
    const base = String(await mf.ready).replace(/\/$/, "") + "/bridge";
    const call = (suffix, init = {}, id) => fetch(base + suffix, {
      ...init, headers: { Origin: "https://client.example", ...(id ? { Authorization: `Bearer ${id}` } : {}), ...init.headers },
    });
    assert.equal((await call("/read", { method: "OPTIONS" })).status, 204);
    for (let n = 0; n < 2; n++) {
      const response = await call("", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ host: "127.0.0.1", port: server.address().port }) });
      assert.equal(response.status, 201, await response.clone().text());
      ids.push((await response.json()).session);
    }
    assert.notEqual(ids[0], ids[1]);
    // Hold a GET open before POST arrives: they must run concurrently in the DO.
    const reading = call("/read", {}, ids[0]);
    const bytes = new Uint8Array([0, 128, 255, 8]);
    const written = await call("/write", { method: "POST", headers: { "Content-Type": "application/octet-stream", "X-Sequence": "0" }, body: bytes }, ids[0]);
    assert.equal(written.status, 204, await written.text());
    const response = await reading;
    assert.equal(response.status, 200);
    assert.deepEqual(new Uint8Array(await response.arrayBuffer()), bytes);
    assert.equal(response.headers.get("Access-Control-Allow-Origin"), "https://client.example");
    assert.equal((await call("/write", { method: "POST", headers: { "Content-Type": "application/octet-stream", "X-Sequence": "0" }, body: bytes }, ids[0])).status, 409);
    assert.equal((await call("", { method: "DELETE" }, ids[0])).status, 204);
    assert.equal((await call("/read", {}, ids[0])).status, 410);
    assert.equal((await call("/write", { method: "POST", headers: { "Content-Type": "application/octet-stream", "X-Sequence": "0" }, body: bytes }, ids[1])).status, 204);
    assert.deepEqual(new Uint8Array(await (await call("/read", {}, ids[1])).arrayBuffer()), bytes);
    await call("", { method: "DELETE" }, ids[1]);
  } finally {
    await mf.dispose(); peers.forEach(p => p.destroy());
    await new Promise(resolve => server.close(resolve));
  }
});
