// Browser integration against the actual C server, not a simulated world.
const assert = require("node:assert/strict"),
  net = require("node:net");
const { spawn } = require("node:child_process"),
  { mkdir, mkdtemp, writeFile, rm } = require("node:fs/promises"),
  { resolve } = require("node:path"),
  { once } = require("node:events");
const { chromium } = require("../.tests/browser/node_modules/playwright");
const delay = (ms) => new Promise((r) => setTimeout(r, ms));
async function port() {
  const s = net.createServer().listen(0, "127.0.0.1");
  await once(s, "listening");
  const p = s.address().port;
  await new Promise((r) => s.close(r));
  return p;
}
(async () => {
  const native = await port(),
    web = await port();
  await mkdir(".tests/tmp", { recursive: true });
  const cwd = await mkdtemp(resolve(".tests/client2-world-"));
  await writeFile(
    `${cwd}/server.txt`,
    `port=${native}\nweb-address=127.0.0.1\nweb-port=${web}\ngamemode=creative\n`,
  );
  const server = spawn(resolve(process.argv[2] || "lapis-obsidian"), [], {
    cwd,
    env: {
      ...process.env,
      LAPIS_OBSIDIAN_CLIENT2_DIR: resolve("client2/dist"),
    },
    stdio: ["pipe", "pipe", "pipe"],
  });
  let logs = "",
    browser;
  server.stdout.on("data", (b) => (logs += b));
  server.stderr.on("data", (b) => (logs += b));
  const errors = [],
    requests = [];
  try {
    for (let i = 0; !logs.includes("Server listening"); i++) {
      assert.ok(i < 500, logs);
      await delay(20);
    }
    browser = await chromium.launch({
      headless: true,
      ...(process.env.CLIENT2_CHROMIUM
        ? { executablePath: process.env.CLIENT2_CHROMIUM }
        : {}),
      args: [
        "--use-gl=angle",
        "--use-angle=swiftshader",
        "--enable-unsafe-swiftshader",
      ],
      env: { ...process.env, TMPDIR: resolve(".tests/tmp") },
    });
    const page = await browser.newPage({
      viewport: { width: 1280, height: 800 },
    });
    page.on("pageerror", (e) => {
      errors.push(e.message);
      console.error("PAGE:", e.message);
    });
    page.on("request", (r) => requests.push(r.url()));
    page.on("console", (m) => {
      if (m.type() === "error") console.error("CONSOLE:", m.text());
    });
    await page.goto(`http://127.0.0.1:${web}/`);
    await page.waitForFunction(
      () => window.app?.currentScreen?.constructor.name === "GuiMainMenu",
    );
    assert.equal(await page.title(), "Lapis Obsidian Client");
    console.log("Main menu initialized");
    await page.screenshot({ path: ".tests/web-client2-menu.png", timeout: 60000 });
    async function clickButton(label) {
      const point = await page.evaluate((label) => {
        const button = app.currentScreen.buttonList.find(
          (b) => b.string === label,
        );
        if (!button) throw Error(`Button missing: ${label}`);
        return {
          x: (button.x + button.width / 2) * app.window.scaleFactor,
          y: (button.y + button.height / 2) * app.window.scaleFactor,
        };
      }, label);
      await page.mouse.click(point.x, point.y);
    }
    console.log("Connecting through GUI");
    await clickButton("Multiplayer");
    await page.waitForFunction(
      () => app.currentScreen?.constructor.name === "GuiDirectConnect",
    );
    await page.evaluate(() => {
      app.currentScreen.fieldName.text = "BrowserMode2";
      app.currentScreen.fieldAddress.text = location.host;
    });
    await clickButton("Connect");
    await page.waitForFunction(
      () =>
        app.lapisConnection?.ready &&
        app.world.getChunkProvider().chunks.size >= 25,
      null,
      { timeout: 90000 },
    );
    await page.waitForFunction(
      () =>
        [...app.world.getChunkProvider().chunks.values()].some((chunk) =>
          chunk.sections.some((section) =>
            section.group.children.some(
              (mesh) => mesh.geometry?.attributes.position?.count > 100,
            ),
          ),
        ),
      null,
      { timeout: 60000 },
    );
    const state = await page.evaluate(() => ({
      chunks: app.world.getChunkProvider().chunks.size,
      blocks: app.world
        .getChunkProvider()
        .getChunkAt(
          Math.floor(app.player.x / 16),
          Math.floor(app.player.z / 16),
        )
        .sections.reduce((n, s) => n + s.blocks.filter(Boolean).length, 0),
      position: [app.player.x, app.player.y, app.player.z],
      registry: app.lapisConnection.registries.size,
    }));
    assert.ok(state.chunks >= 25);
    assert.ok(state.blocks > 1000);
    assert.ok(state.registry > 0);
    await page.screenshot({ path: ".tests/web-client2-world.png" });
    console.log("World:", state);
    await page.evaluate(() => app.playerController.sendChatMessage("/tps"));
    await page.waitForFunction(
      () =>
        app.ingameOverlay.chatOverlay.messages.some((m) =>
          (typeof m === "string" ? m : m.message || "").includes("TPS"),
        ),
      { timeout: 10000 },
    );
    await page.evaluate(() => app.lapisConnection.creative(3));
    await page.waitForFunction(
      () => app.player.inventory.getItemInSlot(0) === 3,
    );
    await page.mouse.click(640, 400);
    await page.waitForFunction(() => app.window.isLocked());
    const before = await page.evaluate(() => [
      app.player.x,
      app.player.y,
      app.player.z,
    ]);
    await page.keyboard.down("Space");
    await delay(180);
    await page.keyboard.up("Space");
    const after = await page.evaluate(() => [
      app.player.x,
      app.player.y,
      app.player.z,
    ]);
    assert.notDeepEqual(
      after,
      before,
      "keyboard jumping moves the upstream player",
    );
    await page.keyboard.press("KeyE");
    await page.waitForFunction(
      () => app.currentScreen?.constructor.name === "Inventory",
    );
    await page.keyboard.press("KeyE");
    // Observe authoritative edits in the application world using its codec.
    await page.waitForFunction(() => app.player.onGround);
    const target = await page.evaluate(() => [
      Math.floor(app.player.x),
      Math.floor(app.player.y) - 1,
      Math.floor(app.player.z) + 1,
    ]);
    await page.evaluate(
      ([x, y, z]) =>
        app.lapisConnection.send(0x28, (p) =>
          p.vi(0).pos(x, y, z).u8(1).vi(++app.lapisConnection.sequence),
        ),
      target,
    );
    await page.waitForFunction(
      ([x, y, z]) => app.world.getBlockAt(x, y, z) === 0,
      target,
    );
    await page.evaluate(
      ([x, y, z]) =>
        app.lapisConnection.send(0x3f, (p) =>
          p
            .vi(0)
            .pos(x, y, z - 1)
            .vi(3)
            .f32(0.5)
            .f32(0.5)
            .f32(0.5)
            .u8(0)
            .u8(0)
            .vi(++app.lapisConnection.sequence),
        ),
      target,
    );
    await page.waitForFunction(
      ([x, y, z]) => app.world.getBlockAt(x, y, z) === 3,
      target,
    );
    await page.evaluate(() => app.loadWorld(null));
    await page.waitForFunction(() => !app.world);
    assert.equal(errors.length, 0, errors.join("\n"));
    assert.ok(
      requests.every((url) => url.startsWith(`http://127.0.0.1:${web}/`)),
      "no external asset requests",
    );
    assert.ok(
      requests.every((url) => !url.includes("/src/resources/")),
      "no inherited assets",
    );
    console.log(
      "Mode 2 Chromium: login, configuration, chunks, inventory, chat, movement, disconnect passed",
    );
  } finally {
    await browser?.close();
    server.kill("SIGTERM");
    if (server.exitCode === null) await once(server, "exit").catch(() => {});
    await rm(cwd, { recursive: true, force: true });
    if (errors.length || !browser) console.error(logs.slice(-4000));
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
