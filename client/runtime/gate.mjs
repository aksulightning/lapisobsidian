// SPDX-License-Identifier: GPL-3.0-only
// Transport admission only. The browser owns decoding and all game state.
import { Reader, Framer, Writer } from "../web/protocol/binary.js";
import { registry } from "../web/protocol/registry.js";
export class ProtocolGate {
  constructor(host, port, send) {
    this.state = "handshake";
    this.host = host;
    this.port = port;
    this.send = send;
    this.framer = new Framer((b) => this.accept(b), 8192);
  }
  push(b) {
    this.framer.push(b);
  }
  accept(bytes) {
    const r = new Reader(bytes),
      id = r.varint();
    if (this.state === "handshake") {
      if (
        id !== 0 ||
        r.varint() !== registry.protocol ||
        r.string(253) !== this.host ||
        r.u16() !== this.port ||
        r.varint() !== 2
      )
        throw Error("PROTOCOL_REQUIRED");
      r.done();
      this.state = "login";
    } else if (this.state === "login") {
      if (id !== 0 || !/^\w{1,15}$/.test(r.string(15)))
        throw Error("PROTOCOL_REQUIRED");
      r.take(16);
      r.done();
      this.state = "ack";
    } else if (this.state === "ack") {
      if (id !== 3) throw Error("PROTOCOL_REQUIRED");
      r.done();
      this.state = "configuration";
    } else if (this.state === "configuration") {
      if (![0, 2, 3, 7].includes(id)) throw Error("PROTOCOL_REQUIRED");
      if (id === 3) {
        r.done();
        this.state = "play";
      }
    } else if (
      ![
        0, 6, 8, 11, 12, 17, 18, 25, 27, 29, 30, 31, 32, 40, 41, 42, 43, 52, 55,
        59, 60, 63, 64,
      ].includes(id)
    )
      throw Error("UNSUPPORTED_PACKET");
    this.send(new Writer().varint(bytes.length).raw(bytes).finish());
  }
}
