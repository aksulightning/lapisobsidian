// SPDX-License-Identifier: GPL-3.0-only
import { connect } from "cloudflare:sockets";
import { attachSession, configuration } from "./session.mjs";

export default {
  fetch(request, env) {
    let target;
    try { target = configuration(env); }
    catch { return new Response("Game gateway is not configured.", { status: 503 }); }
    if (new URL(request.url).pathname !== "/bridge")
      return new Response("Not found", { status: 404 });
    if (request.headers.get("Origin") !== target.origin)
      return new Response("Forbidden", { status: 403 });
    if (request.method !== "GET" || request.headers.get("Upgrade")?.toLowerCase() !== "websocket")
      return new Response("WebSocket required", { status: 426 });
    const [client, server] = Object.values(new WebSocketPair());
    server.binaryType = "arraybuffer";
    server.accept();
    attachSession(server, target, connect);
    return new Response(null, { status: 101, webSocket: client });
  },
};
