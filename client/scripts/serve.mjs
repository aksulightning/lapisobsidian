// SPDX-License-Identifier: GPL-3.0-only
// Static development server only. Never opens a connection to a game server.
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { resolve, extname, sep } from "node:path";
const root = fileURLToPath(new URL("../dist/", import.meta.url));
export function serve(port = Number(process.env.PORT || 8080), host = process.env.HOST || "127.0.0.1") {
  const server = createServer(async (req, res) => {
    try {
      const pathname = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
      const path = resolve(root, "." + (pathname === "/" ? "/index.html" : pathname));
      if (!path.startsWith(root.endsWith(sep) ? root : root + sep) || !["GET", "HEAD"].includes(req.method)) {
        res.writeHead(403); res.end(); return;
      }
      const body = await readFile(path);
      const types = { ".html":"text/html", ".js":"text/javascript", ".css":"text/css", ".svg":"image/svg+xml" };
      res.writeHead(200, { "Content-Type": types[extname(path)] || "application/octet-stream", "X-Content-Type-Options":"nosniff", "Cache-Control":"no-store" });
      res.end(req.method === "HEAD" ? undefined : body);
    } catch { res.writeHead(404); res.end("Not found"); }
  });
  return new Promise(resolve => server.listen(port, host, () => resolve(server)));
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const server = await serve();
  console.log(`Lapis Obsidian Client: http://${server.address().address}:${server.address().port}`);
}
