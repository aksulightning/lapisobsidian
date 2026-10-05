// SPDX-License-Identifier: GPL-3.0-only
const errors = {
  AUTH_FAILED: "Local authentication failed. Restart Lapis Obsidian Client.",
  CLIENT_BUSY: "Another tab is using this client.",
  CONNECT_FAILED:
    "Could not reach the server. Check the address, port, and whether it is running.",
  REMOTE_ERROR: "The server connection ended or timed out.",
  INVALID_TARGET: "Enter a valid server hostname and port.",
  TARGET_NOT_ALLOWED: "This gateway does not allow that server address and port. Use the server supplied by its owner.",
  PROTOCOL_REQUIRED: "This bridge accepts only the supported game protocol.",
  RATE_LIMIT: "The connection exceeded its traffic limit.",
};
export class BridgeTransport {
  onData = () => {};
  onClose = () => {};
  onError = () => {};
  onReady = () => {};
  onUnavailable = () => {};
  constructor(status) {
    this.status = status;
    this.ready = false;
    this.connected = false;
  }
  start(token, endpoint = `ws://${location.host}/bridge`) {
    const socket = (this.socket = new WebSocket(
      endpoint,
    ));
    socket.binaryType = "arraybuffer";
    socket.onopen = () => {
      if (this.socket !== socket) return;
      socket.send(JSON.stringify({ type: "AUTH", token }));
      token = "";
    };
    socket.onmessage = (e) => {
      if (this.socket !== socket) return;
      if (e.data instanceof ArrayBuffer) {
        if (this.connected) this.onData(new Uint8Array(e.data));
        return;
      }
      try {
        const m = JSON.parse(e.data);
        switch (m.type) {
          case "AUTH_OK":
            this.ready = true;
            this.status("Bridge ready");
            this.onReady();
            this.heartbeat = setInterval(() => this.control("PING"), 10000);
            break;
          case "CONNECTED":
            this.connected = true;
            clearTimeout(this.deadline);
            this.pending?.resolve();
            this.pending = null;
            break;
          case "DISCONNECTED":
            this.connected = false;
            clearTimeout(this.deadline);
            this.pending?.reject(Error("Disconnected"));
            this.pending = null;
            this.onClose();
            break;
          case "ERROR": {
            const message =
              errors[m.code] || "The gateway rejected the connection.";
            clearTimeout(this.deadline);
            this.pending?.reject(Error(message));
            this.pending = null;
            this.onError(Error(message));
            this.onUnavailable(Error(message));
            break;
          }
          case "PONG":
            break;
          default:
            throw Error("Invalid bridge message");
        }
      } catch (e) {
        console.error(e);
        this.onError(Error("Invalid local bridge response."));
        socket.close();
      }
    };
    socket.onclose = () => {
      if (this.socket !== socket) return;
      this.ready = false;
      this.connected = false;
      clearInterval(this.heartbeat);
      clearTimeout(this.deadline);
      this.pending?.reject(Error("Local bridge closed"));
      this.pending = null;
      this.onUnavailable(Error("Gateway connection closed."));
      this.onClose();
    };
    socket.onerror = () => {
      if (this.socket !== socket) return;
      const error = Error("Cannot reach the gateway. Check its address and availability.");
      this.onUnavailable(error);
      this.onError(error);
    };
  }
  control(type, fields = {}) {
    if (this.socket?.readyState === WebSocket.OPEN)
      this.socket.send(JSON.stringify({ type, ...fields }));
  }
  connect(host, port) {
    if (!this.ready || this.connected || this.pending)
      return Promise.reject(Error("Bridge is not ready."));
    return new Promise((resolve, reject) => {
      this.pending = { resolve, reject };
      this.control("CONNECT", { host, port });
      this.deadline = setTimeout(() => {
        this.pending = null;
        this.control("DISCONNECT");
        reject(Error("Connection timed out."));
      }, 12000);
    });
  }
  send(data) {
    if (!this.connected || this.socket.bufferedAmount > 256 * 1024)
      throw Error("Game connection is not available.");
    this.socket.send(data);
  }
  disconnect() {
    this.connected = false;
    this.control("DISCONNECT");
  }
  close() {
    clearInterval(this.heartbeat);
    clearTimeout(this.deadline);
    const socket = this.socket;
    this.socket = null;
    this.ready = this.connected = false;
    this.pending?.reject(Error("Connection closed."));
    this.pending = null;
    socket?.close();
  }
}
