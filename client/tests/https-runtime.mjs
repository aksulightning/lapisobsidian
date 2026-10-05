// SPDX-License-Identifier: GPL-3.0-only
import { fileURLToPath } from "node:url";
import { Miniflare } from "miniflare";

export function runtime(port, extra = {}) {
  return new Miniflare({
    modules: true,
    scriptPath: fileURLToPath(new URL("../gateway/worker.mjs", import.meta.url)),
    compatibilityDate: "2026-08-06",
    durableObjects: { TCP_SESSIONS: { className: "TCPSession", useSQLite: true } },
    bindings: { SERVER_HOST: "127.0.0.1", SERVER_PORT: port, ALLOWED_ORIGIN: "https://client.example" },
    ...extra,
  });
}
