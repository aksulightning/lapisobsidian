// SPDX-License-Identifier: GPL-3.0-only
import { attachSession, configuration } from "./session.mjs";
import { attachTCP } from "./tcp.mjs";

export function createGateway(connect, platform = globalThis) {
  return {
    fetch(request, env) {
      const path = new URL(request.url).pathname;
      if (path !== "/bridge" && path !== "/tcp")
        return new platform.Response("Not found", { status: 404 });
      let target;
      try { target = configuration(env); }
      catch { return new platform.Response("Game gateway is not configured.", { status: 503 }); }
      if (request.headers.get("Origin") !== target.origin)
        return new platform.Response("Forbidden", { status: 403 });
      if (request.method !== "GET" || request.headers.get("Upgrade")?.toLowerCase() !== "websocket")
        return new platform.Response("WebSocket required", {
          status: 426, headers: { Upgrade: "websocket" },
        });
      const [client, server] = Object.values(new platform.WebSocketPair());
      server.binaryType = "arraybuffer";
      server.accept();
      if (path === "/tcp") attachTCP(server, target, connect);
      else attachSession(server, target, connect);
      return new platform.Response(null, { status: 101, webSocket: client });
    },
  };
}
