// SPDX-License-Identifier: GPL-3.0-only
export function configuration(env) {
  const host = String(env.SERVER_HOST || "").trim();
  const port = Number(env.SERVER_PORT ?? 25565);
  const origin = String(env.ALLOWED_ORIGIN || "");
  if (!host || host.length > 253 || /[\s/@?#]/.test(host) ||
      !Number.isInteger(port) || port < 1 || port > 65535 ||
      !origin.startsWith("https://") || new URL(origin).origin !== origin)
    throw Error("Configure SERVER_HOST, SERVER_PORT, and ALLOWED_ORIGIN.");
  return { host, port, origin };
}

export const MAX_WRITE = 65536;
export async function readBody(request, limit) {
  if (Number(request.headers.get("Content-Length")) > limit) throw Error("BODY_LIMIT");
  const reader = request.body?.getReader();
  if (!reader) return new Uint8Array();
  const chunks = [];
  let size = 0;
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    void reader.cancel().catch(() => {});
  }, 10000);
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (timedOut) throw Error("BODY_TIMEOUT");
      if (done) break;
      size += value.byteLength;
      if (size > limit) {
        void reader.cancel().catch(() => {});
        throw Error("BODY_LIMIT");
      }
      chunks.push(value);
    }
  } finally { clearTimeout(timer); reader.releaseLock(); }
  const result = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.byteLength; }
  return result;
}

// One instance lives inside one Durable Object. Separate HTTP requests share
// this socket; Workers must not share global sockets across unrelated requests.
export class HTTPSession {
  constructor(env, connect) {
    this.env = env;
    this.connect = connect;
    this.state = "new";
    this.sequence = 0;
  }
  touch() {
    clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => this.stop(), 60000);
  }
  stop() {
    if (this.state === "closed") return;
    this.state = "closed";
    clearTimeout(this.idleTimer);
    try { Promise.resolve(this.socket?.close()).catch(() => {}); } catch {}
    if (this.reader) {
      void this.reader.cancel().catch(() => {}).finally(() => this.reader.releaseLock());
      void this.writer.abort().catch(() => {}).finally(() => this.writer.releaseLock());
    }
  }
  async open() {
    if (this.state !== "new") return new Response("Session already used", { status: 409 });
    this.state = "opening";
    let timer;
    try {
      const target = configuration(this.env);
      this.socket = this.connect({ hostname: target.host, port: target.port }, { secureTransport: "off" });
      this.socket.closed.catch(() => this.stop());
      await Promise.race([
        this.socket.opened,
        new Promise((_, reject) => { timer = setTimeout(() => reject(Error("Timeout")), 10000); }),
      ]);
      if (this.state === "closed") throw Error("Closed");
      this.reader = this.socket.readable.getReader();
      this.writer = this.socket.writable.getWriter();
      this.state = "open";
      this.touch();
      return new Response(null, { status: 201 });
    } catch {
      this.stop();
      return new Response("TCP connection failed", { status: 502 });
    } finally { clearTimeout(timer); }
  }
  async read() {
    if (this.readBusy) return new Response("A read is already pending", { status: 409 });
    this.readBusy = true;
    let timer;
    try {
      // Retain a timed-out read for the next poll so no TCP bytes are discarded.
      this.pendingRead ??= this.reader.read();
      // A poll may time out before the TCP read rejects, so always observe it.
      this.pendingRead.catch(() => {});
      const result = await Promise.race([
        this.pendingRead,
        new Promise(resolve => { timer = setTimeout(() => resolve(null), 20000); }),
      ]);
      if (this.state === "closed") return new Response("Session ended", { status: 410 });
      if (!result) return new Response(null, { status: 204 });
      this.pendingRead = null;
      if (result.done) {
        this.stop();
        return new Response(null, { status: 205 });
      }
      return new Response(result.value, { headers: { "Content-Type": "application/octet-stream" } });
    } catch {
      this.stop();
      return new Response("TCP read failed", { status: 502 });
    } finally {
      clearTimeout(timer);
      this.readBusy = false;
      if (this.state === "open") this.touch();
    }
  }
  async write(request) {
    if (this.writeBusy || request.headers.get("X-Sequence") !== String(this.sequence))
      return new Response("Out-of-order write", { status: 409 });
    if (request.headers.get("Content-Type") !== "application/octet-stream")
      return new Response("Binary body required", { status: 415 });
    this.writeBusy = true;
    let timer;
    try {
      await Promise.race([
        (async () => {
          const bytes = await readBody(request, MAX_WRITE);
          if (this.state !== "open") throw Error("Closed");
          if (bytes.byteLength) await this.writer.write(bytes);
        })(),
        new Promise((_, reject) => { timer = setTimeout(() => reject(Error("Timeout")), 10000); }),
      ]);
      if (this.state !== "open") throw Error("Closed");
      this.sequence++;
      return new Response(null, { status: 204 });
    } catch (error) {
      this.stop();
      return new Response("TCP write failed", { status: error.message === "BODY_LIMIT" ? 413 : 502 });
    } finally {
      clearTimeout(timer);
      this.writeBusy = false;
      if (this.state === "open") this.touch();
    }
  }
  async fetch(request) {
    const path = new URL(request.url).pathname;
    if (path === "/open" && request.method === "POST") return this.open();
    if (request.method === "DELETE") { this.stop(); return new Response(null, { status: 204 }); }
    if (this.state !== "open") return new Response("Session ended", { status: 410 });
    this.touch();
    if (path === "/read" && request.method === "GET") return this.read();
    if (path === "/write" && request.method === "POST") return this.write(request);
    return new Response("Not found", { status: 404 });
  }
}
