// SPDX-License-Identifier: GPL-3.0-only
import { ProtocolGate } from "../runtime/gate.mjs";

// Only the operator-configured game server can be dialled, never arbitrary targets.
export function configuration(env) {
  const host = String(env.SERVER_HOST || "").trim();
  const port = Number(env.SERVER_PORT || 25565);
  const origin = String(env.ALLOWED_ORIGIN || "");
  if (!host || host.length > 253 || /[\s/@?#]/.test(host) ||
      !Number.isInteger(port) || port < 1 || port > 65535 ||
      !origin.startsWith("https://") || new URL(origin).origin !== origin)
    throw Error("Configure SERVER_HOST, SERVER_PORT, and ALLOWED_ORIGIN.");
  return { host, port, origin };
}

export function attachSession(ws, target, connect) {
  let authenticated = false, session = null, closed = false;
  let last = Date.now(), windowStart = last, controls = 0, bytes = 0;
  const send = (value) => { if (!closed) ws.send(JSON.stringify(value)); };
  const disconnect = () => {
    const old = session;
    session = null;
    if (old) {
      clearTimeout(old.timer);
      Promise.resolve(old.socket.close()).catch(() => {});
    }
  };
  const close = () => {
    if (closed) return;
    closed = true;
    clearTimeout(authTimer);
    clearInterval(watchdog);
    disconnect();
    ws.close(1000, "Session ended");
  };
  const fail = (code, fatal = false) => {
    send({ type: "ERROR", code });
    disconnect();
    if (fatal) close();
    else send({ type: "DISCONNECTED" });
  };
  const authTimer = setTimeout(close, 5000);
  const watchdog = setInterval(() => {
    if (Date.now() - last > 35000) close();
  }, 5000);
  ws.addEventListener("close", close);
  ws.addEventListener("error", close);
  ws.addEventListener("message", ({ data }) => {
    try {
      last = Date.now();
      if (last - windowStart >= 1000) {
        windowStart = last; controls = bytes = 0;
      }
      if (typeof data !== "string") {
        const chunk = new Uint8Array(data);
        bytes += chunk.byteLength;
        if (chunk.byteLength > 65536 || bytes > 262144) throw Error("RATE_LIMIT");
        if (!authenticated || !session?.gate) throw Error("PROTOCOL_REQUIRED");
        session.gate.push(chunk);
        return;
      }
      if (data.length > 4096 || ++controls > 30) throw Error("RATE_LIMIT");
      const message = JSON.parse(data);
      const fields = { AUTH: ["type", "token"], CONNECT: ["type", "host", "port"],
        PING: ["type"], DISCONNECT: ["type"] }[message?.type];
      if (!fields || Object.keys(message).length !== fields.length ||
          Object.keys(message).some(key => !fields.includes(key))) throw Error("INVALID_CONTROL");
      if (!authenticated) {
        if (message.type !== "AUTH" || message.token !== "") throw Error("AUTH_FAILED");
        authenticated = true;
        clearTimeout(authTimer);
        send({ type: "AUTH_OK", version: 1 });
        return;
      }
      if (message.type === "PING") { send({ type: "PONG" }); return; }
      if (message.type === "DISCONNECT") {
        disconnect(); send({ type: "DISCONNECTED" }); return;
      }
      if (message.type !== "CONNECT") throw Error("INVALID_CONTROL");
      if (session) { send({ type: "ERROR", code: "ALREADY_CONNECTED" }); return; }
      if (message.host !== target.host || message.port !== target.port) {
        send({ type: "ERROR", code: "TARGET_NOT_ALLOWED" }); return;
      }
      const socket = connect({ hostname: target.host, port: target.port }, { secureTransport: "off" });
      const current = { socket, queue: Promise.resolve(), queued: 0, gate: null };
      session = current;
      current.timer = setTimeout(() => {
        if (session === current) fail("CONNECT_FAILED");
      }, 10000);
      // Socket failures may reject closed independently of opened/readable.
      socket.closed.catch(() => {});
      socket.opened.then(async () => {
        if (closed || session !== current) return;
        clearTimeout(current.timer);
        const writer = socket.writable.getWriter();
        current.gate = new ProtocolGate(target.host, target.port, packet => {
          current.queued += packet.byteLength;
          if (current.queued > 262144) throw Error("RATE_LIMIT");
          current.queue = current.queue.then(async () => {
            if (session !== current) return;
            await writer.write(packet);
            current.queued -= packet.byteLength;
          }).catch(() => { if (session === current) fail("REMOTE_ERROR"); });
        });
        send({ type: "CONNECTED" });
        const reader = socket.readable.getReader();
        try {
          while (!closed && session === current) {
            const { value, done } = await reader.read();
            if (done) break;
            if (session !== current || closed) return;
            if (ws.bufferedAmount > 4 * 1024 * 1024) throw Error("Backpressure");
            ws.send(value);
          }
          if (session === current) { disconnect(); send({ type: "DISCONNECTED" }); }
        } finally { reader.releaseLock(); }
      }).catch(() => {
        if (session === current) fail(current.gate ? "REMOTE_ERROR" : "CONNECT_FAILED");
      });
    } catch (error) {
      const allowed = ["AUTH_FAILED", "RATE_LIMIT", "PROTOCOL_REQUIRED", "UNSUPPORTED_PACKET"];
      fail(allowed.includes(error.message) ? error.message : "INVALID_CONTROL", true);
    }
  });
  return close;
}
