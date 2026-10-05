// SPDX-License-Identifier: GPL-3.0-only
import { chromium, firefox, webkit } from "@playwright/test";
import { startBridge } from "./harness.mjs";
const bridge = await startBridge();
let browser;
try {
  const engine = { chromium, firefox, webkit }[
    process.env.BROWSER_ENGINE ?? "chromium"
  ];
  if (!engine)
    throw new Error("BROWSER_ENGINE must be chromium, firefox, or webkit");
  browser = await engine.launch({ headless: false });
  const context = await browser.newContext();
  await context.addInitScript((value) => {
    if (location.origin === value.origin) window.__LAPIS_BOOTSTRAP__ = value;
  }, bridge.bootstrap);
  const page = await context.newPage();
  await page.goto(bridge.bootstrap.origin);
  console.log(
    "Lapis Obsidian Client development window opened. Close it to stop the bridge.",
  );
  const stop = async () => {
    await browser.close();
    await bridge.stop();
  };
  process.once("SIGINT", stop);
  process.once("SIGTERM", stop);
  await new Promise((resolve) => {
    page.once("close", resolve);
    browser.once("disconnected", resolve);
  });
} finally {
  await browser?.close();
  await bridge.stop();
}
