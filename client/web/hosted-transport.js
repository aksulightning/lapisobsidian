// SPDX-License-Identifier: GPL-3.0-only
const MAX_BATCH = 65536;
const MAX_QUEUE = 256 * 1024;

export function gatewayURL(value) {
  let url;
  try { url = new URL(value); } catch {
    throw Error("Enter your server owner's HTTPS translator URL (https://…).");
  }
  if (url.protocol !== "https:" || url.username || url.password || url.hash || url.search)
    throw Error("The translator must use https:// with no credentials, query, or fragment.");
  return url.href.replace(/\/$/, "");
}

// Browser fetch only: one long-poll reader and ordered, batched binary POSTs.
// Each session maps to one TCP socket in the serverless translator.
export class HostedTransport {
  onData = () => {};
  onClose = () => {};
  onError = () => {};
  constructor(status, fetcher = (...args) => fetch(...args)) {
    this.status = status;
    this.fetcher = fetcher;
    this.ready = this.connected = false;
  }
  async prepare(value) {
    const endpoint = gatewayURL(value);
    if (this.endpoint !== endpoint) this.close();
    this.endpoint = endpoint;
    this.ready = true;
  }
  request(session, suffix, options = {}, timeout = 15000) {
    return this.fetcher(session.endpoint + suffix, {
      ...options,
      headers: { ...(session.id ? { Authorization: `Bearer ${session.id}` } : {}), ...options.headers },
      signal: AbortSignal.any([session.abort.signal, AbortSignal.timeout(timeout)]),
      credentials: "omit",
      cache: "no-store",
      redirect: "error",
    });
  }
  async connect(host, port) {
    if (!this.ready || this.session) throw Error("TCP translator is not ready.");
    const session = {
      endpoint: this.endpoint, abort: new AbortController(), chunks: [], queued: 0, sequence: 0,
    };
    this.session = session;
    try {
      await this.cleanup;
      if (this.session !== session) throw Error("Connection cancelled.");
      const response = await this.request(session, "", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ host, port }),
      });
      if (!response.ok) throw Error(response.status === 403
        ? "This translator does not allow that server address and port."
        : "Could not connect to the TCP server through the HTTPS translator.");
      const result = await response.json();
      if (!/^[a-f0-9]{64}$/.test(result.session || "")) throw Error("Invalid translator response.");
      session.id = result.session;
      if (this.session !== session) {
        this.cleanup = this.dispose(session);
        throw Error("Connection cancelled.");
      }
      this.connected = true;
      this.status("TCP connected");
      void this.read(session);
    } catch (error) {
      if (this.session === session) {
        this.session = null;
        this.connected = false;
        this.cleanup = this.dispose(session);
      }
      throw error;
    }
  }
  async read(session) {
    try {
      while (this.session === session) {
        const response = await this.request(session, "/read", {}, 30000);
        if (this.session !== session) return;
        if (response.status === 205) { this.disconnect(); return; }
        if (response.status === 204) continue;
        if (!response.ok) throw Error("The TCP connection ended or the translator is unavailable.");
        const data = new Uint8Array(await response.arrayBuffer());
        if (this.session === session && data.byteLength) this.onData(data);
      }
    } catch (error) { this.fail(session, error); }
  }
  send(data) {
    const session = this.session;
    if (!this.connected || !session) throw Error("Game connection is not available.");
    const bytes = data instanceof ArrayBuffer ? new Uint8Array(data)
      : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    if (session.queued + bytes.byteLength > MAX_QUEUE) {
      const error = Error("The connection exceeded its TCP write queue limit.");
      this.fail(session, error);
      throw error;
    }
    if (!bytes.byteLength) return;
    session.chunks.push(bytes.slice());
    session.queued += bytes.byteLength;
    if (!session.writing) {
      session.writing = true;
      queueMicrotask(() => void this.write(session));
    }
  }
  async write(session) {
    try {
      while (this.session === session && session.chunks.length) {
        const size = Math.min(MAX_BATCH, session.chunks.reduce((sum, b) => sum + b.byteLength, 0));
        const batch = new Uint8Array(size);
        let offset = 0;
        while (offset < size) {
          const chunk = session.chunks[0], take = Math.min(chunk.byteLength, size - offset);
          batch.set(chunk.subarray(0, take), offset);
          offset += take;
          if (take === chunk.byteLength) session.chunks.shift();
          else session.chunks[0] = chunk.subarray(take);
        }
        const response = await this.request(session, "/write", {
          method: "POST", headers: { "Content-Type": "application/octet-stream", "X-Sequence": String(session.sequence) },
          body: batch,
        });
        if (!response.ok) throw Error("TCP write failed. Reconnect to the server.");
        session.sequence++;
        session.queued -= size;
      }
    } catch (error) { this.fail(session, error); }
    finally { session.writing = false; }
  }
  fail(session, error) {
    if (this.session !== session) return;
    this.onError(error);
    this.disconnect();
  }
  dispose(session) {
    session.abort.abort();
    session.chunks = [];
    if (session.id) {
      // Best effort on page exit. Server-side idle expiry covers interrupted exits.
      try {
        return this.fetcher(session.endpoint, {
          method: "DELETE", headers: { Authorization: `Bearer ${session.id}` },
          keepalive: true, credentials: "omit", redirect: "error",
          signal: AbortSignal.timeout(5000),
        }).catch(() => {});
      } catch {}
    }
  }
  disconnect() {
    const session = this.session;
    this.session = null;
    this.connected = false;
    if (session) { this.cleanup = this.dispose(session); this.onClose(); }
  }
  close() {
    this.disconnect();
    this.ready = false;
  }
}
