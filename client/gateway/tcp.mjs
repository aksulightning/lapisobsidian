// SPDX-License-Identifier: GPL-3.0-only
const MAX_MESSAGE_BYTES = 1024 * 1024;
const MAX_PENDING_BYTES = 4 * MAX_MESSAGE_BYTES;
const MAX_PENDING_MESSAGES = 1024;

// One WebSocket owns one TCP socket. No application framing or control messages.
export function attachTCP(ws, target, connect) {
  let socket, reader, writer, timer;
  let stopped = false, pendingBytes = 0, pendingMessages = 0;
  let writes = Promise.resolve();

  const stop = (code = 1000, reason = "Connection closed") => {
    if (stopped) return;
    stopped = true;
    clearTimeout(timer);
    ws.removeEventListener("message", onMessage);
    ws.removeEventListener("close", onClose);
    ws.removeEventListener("error", onError);
    // close() interrupts pending reads/writes, including a dial still in progress.
    try { Promise.resolve(socket?.close()).catch(() => {}); } catch {}
    try { ws.close(code, reason); } catch {}
  };
  const onClose = () => stop();
  const onError = () => stop(1011, "WebSocket error");
  const onMessage = ({ data }) => {
    if (stopped) return;
    if (!(data instanceof ArrayBuffer) && !ArrayBuffer.isView(data)) {
      stop(1003, "Binary messages required");
      return;
    }
    const chunk = ArrayBuffer.isView(data)
      ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
      : new Uint8Array(data);
    if (chunk.byteLength > MAX_MESSAGE_BYTES ||
        pendingBytes + chunk.byteLength > MAX_PENDING_BYTES ||
        pendingMessages >= MAX_PENDING_MESSAGES) {
      stop(1009, "TCP write queue limit exceeded");
      return;
    }
    if (!chunk.byteLength) return;
    pendingBytes += chunk.byteLength;
    pendingMessages++;
    // Serialize writes and wait for connection establishment, including messages
    // received immediately after the WebSocket upgrade.
    writes = writes.then(async () => {
      try {
        if (!stopped) await writer.write(chunk);
      } finally {
        pendingBytes -= chunk.byteLength;
        pendingMessages--;
      }
    }).catch(() => stop(1011, "TCP write failed"));
  };

  ws.binaryType = "arraybuffer";
  ws.addEventListener("message", onMessage);
  ws.addEventListener("close", onClose);
  ws.addEventListener("error", onError);
  try {
    socket = connect(
      { hostname: target.host, port: target.port },
      { secureTransport: "off" },
    );
    timer = setTimeout(() => stop(1011, "TCP connection timed out"), 10000);
    // closed may reject independently of opened and the stream operations.
    socket.closed.catch(() => stop(1011, "TCP connection failed"));
    writes = socket.opened.then(() => {
      clearTimeout(timer);
      if (!stopped) writer = socket.writable.getWriter();
    }).catch(() => stop(1011, "TCP connection failed"));
    void (async () => {
      try {
        await writes;
        if (stopped) return;
        reader = socket.readable.getReader();
        while (!stopped) {
          const { value, done } = await reader.read();
          if (stopped) break;
          if (done) { stop(1000, "TCP connection ended"); break; }
          ws.send(value);
        }
      } catch {
        stop(1011, "TCP read failed");
      } finally {
        reader?.releaseLock();
        await writes;
        writer?.releaseLock();
      }
    })();
  } catch {
    stop(1011, "TCP connection failed");
  }
  return stop;
}
