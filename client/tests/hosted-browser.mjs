// SPDX-License-Identifier: GPL-3.0-only
import http from "node:http";
import { readFile, mkdir } from "node:fs/promises";
import { chromium, expect } from "@playwright/test";
const root = new URL("../web/", import.meta.url);
const server = http.createServer(async (req, res) => {
  try {
    const path = new URL(req.url, "http://localhost").pathname;
    if (path.includes("..")) { res.writeHead(404).end(); return; }
    const file = path === "/" ? "index.html" : path.slice(1);
    const type = file.endsWith(".js") ? "text/javascript" : file.endsWith(".css") ? "text/css" : file.endsWith(".svg") ? "image/svg+xml" : "text/html";
    res.writeHead(200, { "Content-Type": type }).end(await readFile(new URL(file, root)));
  } catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ executablePath: process.env.LAPIS_TEST_BROWSER,
    args: ["--no-sandbox", "--enable-unsafe-swiftshader", "--use-gl=angle", "--use-angle=swiftshader"] });
  await mkdir("test-results", { recursive: true });
  for (const mobile of [false, true]) {
    const context = await browser.newContext({ viewport: mobile ? { width: 390, height: 844 } : { width: 1280, height: 800 }, hasTouch: mobile, isMobile: mobile });
    const page = await context.newPage(), errors = [];
    page.on("pageerror", error => errors.push(error.message));
    // localhost deliberately selects browser mode; the launcher uses 127.0.0.1.
    await page.goto(`http://localhost:${server.address().port}/`);
    await expect(page.locator("#gateway-field")).toBeVisible();
    await expect(page.locator("#connect")).toBeEnabled();
    await expect(page.locator("#error")).toBeHidden();
    await expect(page.locator("#connection-status")).toContainText("secure gateway");
    await page.locator("#host").fill("game.example");
    await page.locator("#gateway").fill("ws://game.example/bridge");
    await page.locator("#connect").click();
    await expect(page.locator("#error")).toContainText("must use wss://");
    await expect(page.locator("#connect")).toBeEnabled();
    await page.locator("#error-close").click();
    await page.locator("#gateway").fill("wss://game.example/bridge");
    await page.screenshot({ path: `test-results/hosted-${mobile ? "touch" : "desktop"}.png` });
    expect(errors).toEqual([]);
    await context.close();
  }
  console.log("Hosted browser startup, desktop/touch forms, and secure URL validation passed.");
} finally {
  await browser?.close();
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
}
