// SPDX-License-Identifier: GPL-3.0-only
import { configuration, readBody } from "./session.mjs";

export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (!["/bridge", "/bridge/read", "/bridge/write"].includes(path))
      return new Response("Not found", { status: 404 });
    let target;
    try { target = configuration(env); }
    catch { return new Response("TCP translator is not configured", { status: 503 }); }
    if (request.headers.get("Origin") !== target.origin)
      return new Response("Forbidden", { status: 403 });
    const cors = response => {
      const headers = new Headers(response.headers);
      headers.set("Access-Control-Allow-Origin", target.origin);
      headers.set("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
      headers.set("Access-Control-Allow-Headers", "Authorization, Content-Type, X-Sequence");
      headers.set("Access-Control-Max-Age", "600");
      headers.set("Cache-Control", "no-store");
      headers.set("Vary", "Origin");
      return new Response(response.body, { status: response.status, headers });
    };
    if (request.method === "OPTIONS") return cors(new Response(null, { status: 204 }));
    const method = request.method;
    if (request.headers.has("Upgrade") ||
        !(path === "/bridge" && ["POST", "DELETE"].includes(method) ||
          path === "/bridge/read" && method === "GET" ||
          path === "/bridge/write" && method === "POST"))
      return cors(new Response("Use the HTTPS session API", { status: 405 }));
    try {
      if (path === "/bridge" && method === "POST") {
        if (!request.headers.get("Content-Type")?.startsWith("application/json"))
          return cors(new Response("JSON body required", { status: 415 }));
        let body;
        try { body = JSON.parse(new TextDecoder().decode(await readBody(request, 1024))); }
        catch { return cors(new Response("Invalid request", { status: 400 })); }
        if (body?.host !== target.host || body?.port !== target.port)
          return cors(new Response("Target not allowed", { status: 403 }));
        const id = env.TCP_SESSIONS.newUniqueId();
        const response = await env.TCP_SESSIONS.get(id).fetch("https://session/open", { method: "POST" });
        if (response.status !== 201) return cors(response);
        return cors(Response.json({ session: id.toString() }, { status: 201 }));
      }
      const token = request.headers.get("Authorization")?.match(/^Bearer ([a-f0-9]{64})$/)?.[1];
      if (!token) return cors(new Response("Session required", { status: 401 }));
      let id;
      try { id = env.TCP_SESSIONS.idFromString(token); }
      catch { return cors(new Response("Invalid session", { status: 401 })); }
      const forwarded = new Request(`https://session${path.slice(7) || "/"}`, request);
      return cors(await env.TCP_SESSIONS.get(id).fetch(forwarded));
    } catch {
      return cors(new Response("TCP translator unavailable", { status: 502 }));
    }
  },
};
