// SPDX-License-Identifier: GPL-3.0-only
import http from "node:http";
import net from "node:net";
import { randomBytes, timingSafeEqual } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { WebSocketServer, WebSocket } from "ws";
import { ProtocolGate } from "./gate.mjs";

export function validTarget(host, port) {
  return (
    typeof host === "string" &&
    host.length <= 253 &&
    Number.isInteger(port) &&
    port > 0 &&
    port <= 65535 &&
    (net.isIP(host) !== 0 ||
      host
        .split(".")
        .every((s) => /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/i.test(s)))
  );
}
export async function startBridge({
  assetRoot = new URL("../dist/", import.meta.url),
  onIdle = () => {},
  idleGrace = 2500,
} = {}) {
  const token = randomBytes(32).toString("hex");
  const assets = new Map(),
    sessions = new Set(),
    peers = new Set();
  let active = null,
    origin,
    authority,
    stopping = false,
    idleTimer;
  const wss = new WebSocketServer({
    noServer: true,
    maxPayload: 65536,
    perMessageDeflate: false,
  });
  const server = http.createServer(async (req, res) => {
    if (
      req.headers.host !== authority ||
      req.socket.remoteAddress !== "127.0.0.1"
    ) {
      res.writeHead(403).end();
      return;
    }
    if (req.method !== "GET") {
      res.writeHead(405).end();
      return;
    }
    const route = req.url.split("?")[0],
      file = route === "/" ? "index.html" : route.slice(1);
    if (
      !/^[a-zA-Z0-9_./-]+$/.test(file) ||
      file.split("/").some((s) => s === ".." || s.startsWith("."))
    ) {
      res.writeHead(404).end();
      return;
    }
    try {
      let bytes = assets.get(file);
      if (!bytes) {
        bytes = await readFile(new URL(file, assetRoot));
        assets.set(file, bytes);
      }
      const type =
        {
          ".html": "text/html; charset=utf-8",
          ".js": "text/javascript; charset=utf-8",
          ".css": "text/css; charset=utf-8",
          ".svg": "image/svg+xml",
        }[path.extname(file)] || "text/plain";
      res.writeHead(200, {
        "Content-Type": type,
        "Cache-Control": "no-store",
        "X-Content-Type-Options": "nosniff",
        "Referrer-Policy": "no-referrer",
        "Cross-Origin-Opener-Policy": "same-origin",
        "Content-Security-Policy": `default-src 'none'; script-src 'self'; worker-src 'self'; style-src 'self'; img-src 'self' data:; connect-src ws://${authority}/bridge; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`,
      });
      res.end(bytes);
    } catch {
      res.writeHead(404).end();
    }
  });
  server.on("connection", (s) => {
    peers.add(s);
    s.on("close", () => peers.delete(s));
  });
  server.on("upgrade", (req, socket, head) => {
    if (
      stopping ||
      req.url !== "/bridge" ||
      req.headers.host !== authority ||
      req.headers.origin !== origin ||
      socket.remoteAddress !== "127.0.0.1" ||
      sessions.size >= 4
    ) {
      socket.end("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws));
  });
  wss.on("connection", (ws) => {
    sessions.add(ws);
    let authenticated = false,
      tcp = null,
      gate = null,
      dialing = false,
      last = Date.now(),
      controlCount = 0,
      binaryBytes = 0,
      window = Date.now(),
      dialGeneration = 0;
    const send = (value) => {
      if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(value));
    };
    const disconnect = () => {
      dialGeneration++;
      dialing = false;
      gate = null;
      if (tcp) {
        tcp.removeAllListeners();
        tcp.on("error", () => {});
        tcp.destroy();
        tcp = null;
      }
    };
    const fail = (code, fatal = false) => {
      send({ type: "ERROR", code });
      if (fatal) ws.close(1008);
    };
    const authTimer = setTimeout(
      () => ws.close(1008, "Authentication required"),
      3000,
    );
    const watchdog = setInterval(() => {
      if (Date.now() - last > 35000 || ws.bufferedAmount > 8 * 1024 * 1024)
        ws.terminate();
    }, 1000);
    ws.on("error", () => {});
    ws.on("close", () => {
      clearTimeout(authTimer);
      clearInterval(watchdog);
      disconnect();
      sessions.delete(ws);
      if (active === ws) {
        active = null;
        if (!stopping) {
          clearTimeout(idleTimer);
          idleTimer = setTimeout(onIdle, idleGrace);
        }
      }
    });
    ws.on("message", (bytes, binary) => {
      last = Date.now();
      if (last - window >= 1000) {
        window = last;
        controlCount = 0;
        binaryBytes = 0;
      }
      try {
        if (binary) {
          binaryBytes += bytes.length;
          if (!authenticated || !tcp || dialing || !gate)
            throw Error("NOT_CONNECTED");
          if (binaryBytes > 256 * 1024 || tcp.writableLength > 256 * 1024)
            throw Error("RATE_LIMIT");
          gate.push(bytes);
          return;
        }
        if (bytes.length > 4096 || ++controlCount > 30)
          throw Error("INVALID_CONTROL");
        const c = JSON.parse(bytes.toString());
        if (
          !c ||
          typeof c !== "object" ||
          Array.isArray(c) ||
          typeof c.type !== "string"
        )
          throw Error("INVALID_CONTROL");
        const fields = {
          AUTH: ["type", "token"],
          CONNECT: ["type", "host", "port"],
          DISCONNECT: ["type"],
          PING: ["type"],
          SHUTDOWN: ["type"],
        }[c.type];
        if (
          !fields ||
          Object.keys(c).length !== fields.length ||
          Object.keys(c).some((k) => !fields.includes(k))
        )
          throw Error("INVALID_CONTROL");
        if (!authenticated) {
          if (
            c.type !== "AUTH" ||
            typeof c.token !== "string" ||
            c.token.length !== 64 ||
            !timingSafeEqual(Buffer.from(c.token), Buffer.from(token))
          )
            throw Error("AUTH_FAILED");
          if (active) throw Error("CLIENT_BUSY");
          authenticated = true;
          active = ws;
          clearTimeout(authTimer);
          clearTimeout(idleTimer);
          send({ type: "AUTH_OK", version: 1 });
          return;
        }
        if (c.type === "PING") {
          send({ type: "PONG" });
          return;
        }
        if (c.type === "DISCONNECT") {
          disconnect();
          send({ type: "DISCONNECTED" });
          return;
        }
        if (c.type === "SHUTDOWN") {
          disconnect();
          onIdle();
          return;
        }
        if (c.type !== "CONNECT") throw Error("INVALID_CONTROL");
        if (!validTarget(c.host, c.port)) {
          fail("INVALID_TARGET");
          return;
        }
        if (tcp || dialing) {
          fail("ALREADY_CONNECTED");
          return;
        }
        if (
          (c.host === "127.0.0.1" || c.host === "localhost") &&
          c.port === server.address().port
        ) {
          fail("INVALID_TARGET");
          return;
        }
        dialing = true;
        const generation = ++dialGeneration;
        const stream = net.createConnection({ host: c.host, port: c.port });
        tcp = stream;
        stream.setNoDelay(true);
        stream.setTimeout(10000);
        stream.on("connect", () => {
          if (generation !== dialGeneration) return;
          dialing = false;
          stream.setTimeout(30000);
          gate = new ProtocolGate(c.host, c.port, (b) => stream.write(b));
          send({ type: "CONNECTED" });
        });
        stream.on("data", (data) => {
          if (generation !== dialGeneration || ws.readyState !== WebSocket.OPEN)
            return;
          if (ws.bufferedAmount > 4 * 1024 * 1024) {
            stream.pause();
          }
          ws.send(data, { binary: true }, (error) => {
            if (error) ws.terminate();
            else if (stream === tcp) stream.resume();
          });
        });
        stream.on("timeout", () => stream.destroy(Error("timeout")));
        stream.on("error", () => {
          if (generation === dialGeneration)
            fail(dialing ? "CONNECT_FAILED" : "REMOTE_ERROR");
        });
        stream.on("close", () => {
          if (generation !== dialGeneration) return;
          tcp = null;
          gate = null;
          dialing = false;
          send({ type: "DISCONNECTED" });
        });
      } catch (error) {
        fail(
          error.message.startsWith("AUTH") ||
            [
              "NOT_CONNECTED",
              "CLIENT_BUSY",
              "RATE_LIMIT",
              "PROTOCOL_REQUIRED",
              "UNSUPPORTED_PACKET",
            ].includes(error.message)
            ? error.message
            : "INVALID_CONTROL",
          true,
        );
      }
    });
  });
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  authority = `127.0.0.1:${server.address().port}`;
  origin = `http://${authority}`;
  return {
    origin,
    token,
    port: server.address().port,
    get authenticated() {
      return !!active;
    },
    async close() {
      if (stopping) return;
      stopping = true;
      clearTimeout(idleTimer);
      for (const ws of sessions) ws.terminate();
      await new Promise((resolve) => wss.close(resolve));
      for (const peer of peers) peer.destroy();
      await new Promise((resolve) => server.close(resolve));
    },
  };
}
