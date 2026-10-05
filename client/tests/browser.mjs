// SPDX-License-Identifier: GPL-3.0-only
import { chromium, expect } from "@playwright/test";
import { mkdir, writeFile } from "node:fs/promises";
import { startBridge, startServer } from "./helpers.mjs";
const server = await startServer(),
  bridge = await startBridge();
let browser;
const results = [];
try {
  browser = await chromium.launch({
    executablePath: process.env.LAPIS_TEST_BROWSER,
    args: [
      "--no-sandbox",
      "--enable-unsafe-swiftshader",
      "--use-gl=angle",
      "--use-angle=swiftshader",
      "--disable-dev-shm-usage",
    ],
  });
  await mkdir("test-results", { recursive: true });
  for (const mobile of [false, true]) {
    const context = await browser.newContext({
      viewport: mobile
        ? { width: 844, height: 390 }
        : { width: 1280, height: 800 },
      hasTouch: mobile,
      isMobile: mobile,
      deviceScaleFactor: 1,
    });
    const page = await context.newPage(),
      errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (m.type() === "error") errors.push(m.text());
    });
    await page.goto(bridge.origin + "/#" + bridge.token);
    await expect(page).toHaveTitle("Lapis Obsidian Client");
    await expect(page.locator("#connection-status")).toHaveText("Bridge ready");
    await page.locator("#port").fill(String(server.port));
    await page.locator("#username").fill(mobile ? "TouchTest" : "DesktopTest");
    await page.locator("#connect").click();
    await page.waitForFunction(
      () =>
        window.lapisDiagnostics?.connected &&
        window.lapisDiagnostics.chunks >= 25 &&
        window.lapisDiagnostics.vertices > 0,
      {},
      { timeout: 45000 },
    );
    await expect(page.locator("#error")).toBeHidden();
    const before = await page.evaluate(() => window.lapisDiagnostics);
    await page.screenshot({
      path: `test-results/${mobile ? "touch" : "desktop"}-world.png`,
    });
    if (!mobile) {
      await page.locator("#world").click({ position: { x: 640, y: 350 } });
      await page.waitForFunction(
        () => document.pointerLockElement?.id === "world",
      );
      await page.keyboard.down("KeyW");
      await page.waitForTimeout(450);
      await page.keyboard.up("KeyW");
      const moved = await page.evaluate(() => window.lapisDiagnostics.position);
      expect(
        Math.hypot(moved.x - before.position.x, moved.z - before.position.z),
      ).toBeGreaterThan(0.15);
      await page.mouse.move(700, 380);
      await page.waitForTimeout(150);
      expect(
        (await page.evaluate(() => window.lapisDiagnostics.position)).yaw,
      ).not.toBe(before.position.yaw);
      await page.keyboard.press("Digit3");
      expect(await page.evaluate(() => window.lapisDiagnostics.slot)).toBe(2);
      await page.keyboard.press("Space");
      await page.keyboard.press("KeyT");
      await page.locator("#chat-input").fill("Browser desktop round-trip");
      await page.locator("#chat-form button.accent").click();
      await expect(page.locator("#chat-log")).toContainText(
        "Browser desktop round-trip",
      );
      await page.keyboard.press("KeyE");
      await expect(page.locator("#inventory")).toBeVisible();
      await page.locator("#creative-items .slot").first().click();
      await page.locator("#inventory-close").click();
      await expect(page.locator("#hotbar .slot").nth(2)).toContainText("64");
      await page.keyboard.press("Escape");
      await expect(page.locator("#menu")).toBeVisible();
      await page.locator("#resume").click();
    } else {
      await expect(page.locator("#touch-controls")).toBeVisible();
      const session = await context.newCDPSession(page);
      const center = async (id) => {
        const r = await page.locator(id).boundingBox();
        return { x: r.x + r.width / 2, y: r.y + r.height / 2 };
      };
      const stick = await center("#joystick"),
        look = { x: 550, y: 100 },
        jump = await center("#jump");
      const send = (type, points) =>
        session.send("Input.dispatchTouchEvent", {
          type,
          touchPoints: points.map((p, i) => ({
            id: p.id ?? i + 1,
            x: p.x,
            y: p.y,
            radiusX: 5,
            radiusY: 5,
            force: 1,
          })),
        });
      await send("touchStart", [
        { ...stick, id: 1 },
        { ...look, id: 2 },
      ]);
      await send("touchMove", [
        { x: stick.x, y: stick.y - 38, id: 1 },
        { x: look.x + 60, y: look.y + 20, id: 2 },
      ]);
      await page.waitForTimeout(400);
      await send("touchStart", [
        { x: stick.x, y: stick.y - 38, id: 1 },
        { x: look.x + 60, y: look.y + 20, id: 2 },
        { ...jump, id: 3 },
      ]);
      await page.waitForTimeout(150);
      const during = await page.evaluate(
        () => window.lapisDiagnostics.position,
      );
      expect(
        Math.hypot(during.x - before.position.x, during.z - before.position.z),
      ).toBeGreaterThan(0.1);
      expect(during.yaw).not.toBe(before.position.yaw);
      expect(during.y).toBeGreaterThan(before.position.y);
      await send("touchEnd", []);
      await page.locator("#hotbar .slot").nth(4).tap();
      expect(await page.evaluate(() => window.lapisDiagnostics.slot)).toBe(4);
      await page.locator("#chat-open").tap();
      await page.locator("#chat-input").fill("Touch round-trip");
      await page.locator("#chat-form button.accent").tap();
      await expect(page.locator("#chat-log")).toContainText("Touch round-trip");
      await page.setViewportSize({ width: 390, height: 844 });
      await page.waitForTimeout(100);
      await expect(page.locator("#joystick")).toBeVisible();
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth),
      ).toBeLessThanOrEqual(390);
      await page.screenshot({ path: "test-results/touch-portrait.png" });
    }
    await page.setViewportSize({ width: 1000, height: 600 });
    await page.waitForTimeout(200);
    const size = await page
      .locator("#world")
      .evaluate((c) => [c.width, c.height]);
    expect(size[0]).toBeGreaterThanOrEqual(1000);
    expect(size[1]).toBeGreaterThanOrEqual(600);
    if(mobile) await page.locator("#menu-open").click();
    else await page.keyboard.press("Escape");
    await page.locator("#disconnect").click();
    await expect(page.locator("#connection-status")).toHaveText("Disconnected");
    await page.locator("#connect").click();
    await page.waitForFunction(
      () => window.lapisDiagnostics?.connected,
      {},
      { timeout: 30000 },
    );
    const after = await page.evaluate(() => window.lapisDiagnostics);
    results.push({
      layout: mobile ? "touch" : "desktop",
      chunks: after.chunks,
      renderedVertices: before.vertices,
      errors,
    });
    expect(errors).toEqual([]);
    await context.close();
    await new Promise((r) => setTimeout(r, 150));
    console.log(
      `${mobile ? "Touch" : "Desktop"} world, controls, chat, resize and reconnect passed.`,
    );
  }
  await writeFile(
    "test-results/browser-results.json",
    JSON.stringify(results, null, 2),
  );
} finally {
  await browser?.close();
  await bridge.close();
  await server.stop();
}
