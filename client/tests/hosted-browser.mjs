// SPDX-License-Identifier: GPL-3.0-only
import { readFile, mkdir } from "node:fs/promises";
import { chromium, expect } from "@playwright/test";
import { startServer } from "./helpers.mjs";
import { runtime } from "./https-runtime.mjs";
const root = new URL("../web/", import.meta.url);
const server = await startServer();
const mf = runtime(server.port, { https: true });
let browser;
try {
  const endpoint = String(await mf.ready).replace(/\/$/, "") + "/bridge";
  browser = await chromium.launch({ executablePath: process.env.LAPIS_TEST_BROWSER,
    args: ["--no-sandbox", "--enable-unsafe-swiftshader", "--use-gl=angle", "--use-angle=swiftshader",
      // Only the test translator is on loopback. Production uses a public host.
      "--disable-features=LocalNetworkAccessChecks"] });
  await mkdir("test-results", { recursive: true });
  for (const mobile of [false, true]) {
    const context = await browser.newContext({ ignoreHTTPSErrors: true,
      viewport: mobile ? { width: 390, height: 844 } : { width: 1280, height: 800 }, hasTouch: mobile, isMobile: mobile });
    // Serve static assets at a distinct HTTPS origin to exercise real fetch CORS.
    await context.route("https://client.example/**", async route => {
      const path = new URL(route.request().url()).pathname;
      const file = path === "/" ? "index.html" : path.slice(1);
      const contentType = file.endsWith(".js") ? "text/javascript" : file.endsWith(".css") ? "text/css" : file.endsWith(".svg") ? "image/svg+xml" : "text/html";
      try { await route.fulfill({ contentType, body: await readFile(new URL(file, root)) }); }
      catch { await route.fulfill({ status: 404, body: "Not found" }); }
    });
    await context.addInitScript(() => {
      window.WebSocket = class { constructor() { throw Error("Hosted mode must not use WebSockets"); } };
    });
    const page = await context.newPage(), errors = [], sockets = [];
    page.on("pageerror", error => errors.push(error.message));
    page.on("websocket", socket => sockets.push(socket.url()));
    await page.goto("https://client.example/");
    await expect(page.locator("#gateway-field")).toBeVisible();
    await expect(page.locator("#connect")).toBeEnabled();
    await page.locator("#host").fill("127.0.0.1");
    await page.locator("#port").fill(String(server.port));
    await page.locator("#username").fill(mobile ? "HTTPSTouch" : "HTTPSDesktop");
    await page.locator("#gateway").fill("wss://game.example/bridge");
    await page.locator("#connect").click();
    await expect(page.locator("#error")).toContainText("must use https://");
    await page.locator("#error-close").click();
    await page.locator("#gateway").fill(endpoint);
    for (let round = 0; round < 2; round++) {
      await page.locator("#connect").click();
      await page.waitForFunction(() => window.lapisDiagnostics?.connected && window.lapisDiagnostics.chunks >= 25 && window.lapisDiagnostics.vertices > 0,
        {}, { timeout: 45000 });
      await expect(page.locator("#error")).toBeHidden();
      if (round === 0) await page.screenshot({ path: `test-results/hosted-${mobile ? "touch" : "desktop"}.png` });
      if (mobile) await page.locator("#menu-open").click();
      else await page.keyboard.press("Escape");
      await page.locator("#disconnect").click();
      await expect(page.locator("#connection-status")).toHaveText("Disconnected");
    }
    expect(sockets).toEqual([]);
    expect(errors).toEqual([]);
    await context.close();
  }
  console.log("Hosted desktop/touch gameplay and reconnect passed over HTTPS with WebSocket disabled.");
} finally {
  await browser?.close();
  await mf.dispose();
  await server.stop();
}
