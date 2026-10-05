// SPDX-License-Identifier: GPL-3.0-only
export class LapisClientTransport {
  onMessage = () => {};
  onBinary = () => {};
  onClose = () => {};
  onError = () => {};
  constructor(status = () => {}) { this.status = status; this.connected = false; }
  connect(address) {
    this.close();
    const url = new URL(address);
    if (!["ws:", "wss:"].includes(url.protocol) || url.username || url.password || url.hash)
      throw Error("Enter a ws:// or wss:// server endpoint without credentials or a fragment.");
    if (location.protocol === "https:" && url.protocol !== "wss:")
      throw Error("This HTTPS page requires a secure wss:// server endpoint.");
    this.status("Connecting");
    return new Promise((resolve, reject) => {
      const socket = this.socket = new WebSocket(url, "lapisclient");
      socket.binaryType = "arraybuffer";
      this.pending = { resolve, reject };
      this.deadline = setTimeout(() => this.fail(Error("Connection or protocol negotiation timed out.")), 12000);
      socket.onopen = () => {
        if (this.socket !== socket) return;
        if (socket.protocol !== "lapisclient") return this.fail(Error("Protocol mismatch"));
        this.status("Negotiating lapisclient");
        this.send({ type: "hello", protocol: "lapisclient", version: 1 });
      };
      socket.onmessage = ({ data }) => {
        if (this.socket !== socket) return;
        try {
          if (data instanceof ArrayBuffer) {
            if (!this.connected || data.byteLength > 256 * 1024) throw Error("Unexpected binary world data");
            this.onBinary(new Uint8Array(data));
            return;
          }
          if (data.length > 8192) throw Error("Server message exceeds limit");
          const message = JSON.parse(data);
          if (!message || typeof message !== "object" || typeof message.type !== "string") throw Error("Invalid server message");
          if (message.type === "error" || message.type === "login_error") {
            const error = Error(message.code === "protocol_mismatch" ? "Protocol mismatch" : message.message || "Server rejected the request");
            return this.fail(error);
          }
          if (!this.connected) {
            if (message.type !== "welcome" || message.protocol !== "lapisclient" || message.version !== 1) throw Error("Protocol mismatch");
            this.connected = true;
            clearTimeout(this.deadline);
            this.pending.resolve(message); this.pending = null;
            this.heartbeat = setInterval(() => {
              try { this.send({ type: "ping" }); } catch (error) { this.fail(error); }
            }, 10000);
          } else if (message.type === "disconnect") this.close(true);
          else if (message.type !== "pong") this.onMessage(message);
        } catch (error) { this.fail(error); }
      };
      socket.onerror = () => { if (this.socket === socket) this.fail(Error("Connection failed. Check the endpoint, allowed origin and TLS configuration.")); };
      socket.onclose = () => { if (this.socket === socket) this.close(true); };
    });
  }
  send(message) {
    if (this.socket?.readyState !== WebSocket.OPEN || this.socket.bufferedAmount > 256 * 1024)
      throw Error("Connection is unavailable or stalled");
    this.socket.send(JSON.stringify(message));
  }
  // v1 intentionally defines no client-to-server binary messages.
  sendBinary() { throw Error("lapisclient v1 accepts only named JSON client messages"); }
  fail(error) {
    this.onError(error);
    this.pending?.reject(error); this.pending = null;
    this.close(true);
  }
  disconnect() {
    if (this.connected && this.socket?.readyState === WebSocket.OPEN) this.send({ type: "disconnect" });
    this.close(true);
  }
  close(notify = false) {
    clearTimeout(this.deadline); clearInterval(this.heartbeat);
    this.pending?.reject(Error("Connection closed")); this.pending = null;
    const socket = this.socket; this.socket = null; this.connected = false;
    socket?.close(1000);
    if (notify) this.onClose();
  }
}
