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
      LAPIS_ADMIN_TOKEN: "mode2-browser-test-admin-token-123456",
    },
    stdio: ["pipe", "pipe", "pipe"],
  });
  let logs = "",
    browser;
  server.stdout.on("data", (b) => (logs += b));
  server.stderr.on("data", (b) => (logs += b));
  const errors = [],
    requests = [];
  let completed = false;
  const sent = {},
    received = {},
    digging = [];
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
      viewport: { width: 800, height: 600 },
    });
    const { Stream } = await import("../client2/adapter/wire.mjs");
    page.on("websocket", (ws) => {
      const output = new Stream((id, p) => {
          sent[id] = (sent[id] || 0) + 1;
          if (id === 0x28) {
            digging.push({ status: p.vi(), position: p.pos(), face: p.u8() });
            if (digging.length > 30) digging.shift();
          }
        }),
        input = new Stream((id) => (received[id] = (received[id] || 0) + 1));
      ws.on("framesent", (e) => {
        try {
          output.push(new Uint8Array(e.payload));
        } catch {}
      });
      ws.on("framereceived", (e) => {
        try {
          input.push(new Uint8Array(e.payload));
        } catch {}
      });
    });
    page.on("pageerror", (e) => {
      errors.push(e.message);
      console.error("PAGE:", e.message);
    });
    page.on("request", (r) => requests.push(r.url()));
    page.on("console", (m) => {
      if (m.type() === "error") console.error("CONSOLE:", m.text());
    });
    async function wait(predicate, argument, options = {}) {
      const deadline =
        Date.now() + (options.timeout || argument?.timeout || 60000);
      while (!(await page.evaluate(predicate, argument))) {
        const detail = await page.evaluate(() => ({
          screen: app.currentScreen?.constructor.name,
          message: app.currentScreen?.message,
          phase: app.lapisConnection?.phase,
          ready: app.lapisConnection?.ready,
          chunks: app.world?.getChunkProvider().chunks.size,
          position: app.player && [app.player.x, app.player.y, app.player.z],
          rotation: app.player && [app.player.rotationYaw, app.player.rotationPitch],
          health: app.player?.health,
          focus: app.hasInGameFocus(),
          held: [...(app.lapisConnection?.actions.held || [])],
          dig: app.lapisConnection?.actions.dig && {
            position: [app.lapisConnection.actions.dig.hit.x, app.lapisConnection.actions.dig.hit.y, app.lapisConnection.actions.dig.hit.z],
            block: app.lapisConnection.actions.dig.block,
            duration: app.lapisConnection.actions.dig.duration,
            finished: app.lapisConnection.actions.dig.finished,
          },
        }));
        if (detail.message) throw Error(`Client error: ${detail.message}`);
        assert.ok(
          Date.now() < deadline,
          `Browser condition timed out: ${predicate.toString()} ${JSON.stringify(detail)}`,
        );
        await delay(50);
      }
    }
    await page.goto(`http://127.0.0.1:${web}/`);
    await wait(
      () => window.app?.currentScreen?.constructor.name === "GuiMainMenu",
    );
    assert.equal(await page.title(), "Lapis Obsidian Client");
    console.log(
      "Main menu initialized",
      await page.evaluate(() => ({
        fps: app.fps,
        size: [app.window.width, app.window.height],
        canvas: [app.window.canvas.width, app.window.canvas.height],
      })),
    );
    async function capture(path) {
      const data = await page.evaluate(() => {
        app.onRender(app.timer.partialTicks);
        const canvas = document.createElement("canvas");
        canvas.width = app.window.canvas.width;
        canvas.height = app.window.canvas.height;
        const context = canvas.getContext("2d");
        context.drawImage(
          app.window.canvasWorld,
          0,
          0,
          canvas.width,
          canvas.height,
        );
        context.drawImage(app.window.canvas, 0, 0);
        return canvas.toDataURL("image/png").split(",")[1];
      });
      await writeFile(path, Buffer.from(data, "base64"));
    }
    await capture(".tests/web-client2-menu.png");
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
    await wait(
      () => app.currentScreen?.constructor.name === "GuiDirectConnect",
    );
    await page.evaluate(() => {
      app.currentScreen.fieldName.text = "BrowserMode2";
      app.currentScreen.fieldAddress.text = location.host;
    });
    await clickButton("Connect");
    console.log(
      "Connection started",
      await page.evaluate(() => ({
        screen: app.currentScreen?.constructor.name,
        address: app.currentScreen?.address,
        phase: app.lapisConnection?.phase,
        closed: app.lapisConnection?.closed,
      })),
    );
    await wait(
      () =>
        app.lapisConnection?.ready &&
        app.world.getChunkProvider().chunks.size >= 25,
      null,
      { timeout: 90000 },
    );
    await wait(
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
    await capture(".tests/web-client2-world.png");
    console.log("World:", state);
    await page.evaluate(() => app.playerController.sendChatMessage("/tps"));
    await wait(
      () =>
        app.ingameOverlay.chatOverlay.messages.some((m) =>
          (typeof m === "string" ? m : m.message || "").includes("TPS"),
        ),
      { timeout: 10000 },
    );
    await page.evaluate(() => app.lapisConnection.creative(3));
    await wait(() => app.player.inventory.getItemInSlot(0) === 3);
    await page.mouse.click(400, 300);
    await wait(() => document.pointerLockElement === app.window.canvas);
    await wait(() => app.player.onGround);
    const before = await page.evaluate(() => [
      app.player.x,
      app.player.y,
      app.player.z,
    ]);
    await page.keyboard.down("Space");
    // Assert the jump while airborne. Under software rendering the player can
    // land between separate browser calls, so a later position is not evidence
    // of whether the jump happened.
    await wait(
      (before) => app.player.y > before[1] + 0.05 && !app.player.onGround,
      before,
    );
    await page.keyboard.up("Space");
    await page.keyboard.press("KeyE");
    await wait(() => app.currentScreen?.constructor.name === "Inventory");
    await page.keyboard.press("KeyE");
    // Observe authoritative edits in the application world using its codec.
    await wait(() => app.player.onGround);
    const target = await page.evaluate(() => [
      Math.floor(app.player.x),
      Math.floor(app.player.y),
      Math.floor(app.player.z) + 2,
    ]);
    await page.evaluate(
      ([x, y, z]) =>
        app.lapisConnection.send(0x28, (p) =>
          p.vi(0).pos(x, y, z).u8(1).vi(++app.lapisConnection.sequence),
        ),
      target,
    );
    await wait(([x, y, z]) => app.world.getBlockAt(x, y, z) === 0, target);
    await page.evaluate(
      ([x, y, z]) =>
        app.lapisConnection.send(0x3f, (p) =>
          p
            .vi(0)
            .pos(x, y - 1, z)
            .vi(1)
            .f32(0.5)
            .f32(0.5)
            .f32(0.5)
            .u8(0)
            .u8(0)
            .vi(++app.lapisConnection.sequence),
        ),
      target,
    );
    await wait(([x, y, z]) => app.world.getBlockAt(x, y, z) === 3, target);
    // Clear the full sightline, including cover in the intervening column,
    // through authoritative creative actions while keeping the dirt fixture.
    for (const [dy, dz] of [[0, -1], [1, -1], [2, -1], [1, 0], [2, 0]]) {
      const above = [target[0], target[1] + dy, target[2] + dz];
      await page.evaluate(([x, y, z]) => {
        const c = app.lapisConnection;
        c.send(0x28, (p) => p.vi(0).pos(x, y, z).u8(1).vi(++c.sequence));
      }, above);
      await wait(([x, y, z]) => app.world.getBlockAt(x, y, z) === 0, above);
    }
    // Test the actual held-input path; a released mouse must never finish mining.
    await page.evaluate(() =>
      app.playerController.sendChatMessage(
        "/admin mode2-browser-test-admin-token-123456",
      ),
    );
    await wait(() =>
      app.ingameOverlay.chatOverlay.messages.some((m) =>
        String(m.message || m).includes("Administrator access enabled"),
      ),
    );
    await page.evaluate(() =>
      app.playerController.sendChatMessage("/gamemode survival"),
    );
    await wait(() => app.lapisConnection.mode === 0);
    // These fixtures set their aim explicitly. Under pointer lock, CDP button
    // dispatch can deliver a mousemove after page.mouse.down() resolves, so
    // draining the current motion alone races with the next rendered frame.
    // Disable look sensitivity only while testing the fixed mining/entity rays;
    // real mouse press/release handlers and held-action ticks remain active.
    const sensitivity = await page.evaluate(() => {
      const previous = app.settings.sensitivity;
      app.settings.sensitivity = 0;
      return previous;
    });
    async function aimAtFixture() {
      await page.evaluate(([x, y, z]) => {
      app.window.pullMouseMotionX();
      app.window.pullMouseMotionY();
      const p = app.player,
        dx = x + 0.5 - p.x,
        dy = y + 0.5 - p.y - p.getEyeHeight(),
        dz = z + 0.5 - p.z;
      p.setRotation(
        (-Math.atan2(dx, dz) * 180) / Math.PI,
        (-Math.atan2(dy, Math.hypot(dx, dz)) * 180) / Math.PI,
      );
    }, target);
    }
    await aimAtFixture();
    const aim = await page.evaluate(() => {
      const hit = app.player.rayTrace(4.5, app.timer.partialTicks);
      return hit && [hit.x, hit.y, hit.z];
    });
    assert.deepEqual(aim, target, "survival fixture must be directly visible");
    await page.mouse.down();
    await aimAtFixture();
    await wait(([x, y, z]) => {
      const hit = app.lapisConnection.actions.dig?.hit;
      return hit?.x === x && hit?.y === y && hit?.z === z;
    }, target);
    await page.mouse.up();
    await delay(1100);
    assert.equal(
      await page.evaluate(([x, y, z]) => app.world.getBlockAt(x, y, z), target),
      3,
      "mouse release cancels digging",
    );
    await page.mouse.down();
    await aimAtFixture();
    await wait(([x, y, z]) => app.world.getBlockAt(x, y, z) === 0, target);
    await page.mouse.up();
    await wait(() =>
      [...app.lapisConnection.entities.values()].some(
        (e) => e.kind === "item" && e.itemStack?.count,
      ),
    );
    assert.ok(
      await page.evaluate(() =>
        [...app.lapisConnection.entities.values()]
          .filter((e) => e.kind === "item")
          .every(
            (e) => e.constructor.name === "ServerEntity" && e.height < 0.5,
          ),
      ),
    );
    console.log("Held/canceled survival mining and item model passed");
    await page.keyboard.press("KeyE");
    await wait(() => app.currentScreen?.constructor.name === "Inventory");
    assert.equal(
      await page.evaluate(() => app.currentScreen.cells.length),
      46,
      "survival crafting, armor, storage and hotbar slots",
    );
    await capture(".tests/web-client2-survival-inventory.png");
    await page.keyboard.press("KeyE");
    // Distinct original mob model, driven by a real spawn packet.
    const spot = await page.evaluate(async () => {
      const {nameToBlock}=await import("/adapter/registry.mjs");
      const p = app.player,
        x = Math.floor(p.x),
        z = Math.floor(p.z);
      for (let dx = 2; dx <= 4; dx++)
        for (let dz = 1; dz <= 4; dz++) {
          let y = 250;
          while (y > 0 && (!app.world.getBlockAt(x + dx, y, z + dz) || ['snow','moss_carpet','short_grass','fern'].some(name=>nameToBlock.get(name)===app.world.getBlockAt(x+dx,y,z+dz)))) y--;
          if(app.world.getBlockAt(x+dx,y,z+dz)===9)continue;
          if (Math.abs(y + 1 - p.y) < 4) return [x + dx, y + 1, z + dz];
        }
      throw Error("No nearby spawn ground");
    });
    await page.evaluate(
      (spot) =>
        app.playerController.sendChatMessage("/spawnmob cow " + spot.join(" ")),
      spot,
    );
    await wait(() =>
      [...app.lapisConnection.entities.values()].some((e) => e.kind === "cow"),
    );
    await page.evaluate(() => {
      app.window.pullMouseMotionX();
      app.window.pullMouseMotionY();
      const cow = [...app.lapisConnection.entities.values()].find(
        (e) => e.kind === "cow",
      );
      const p = app.player,
        dx = cow.x - p.x,
        dy = cow.y + 0.8 - p.y - p.getEyeHeight(),
        dz = cow.z - p.z;
      p.setRotation(
        (-Math.atan2(dx, dz) * 180) / Math.PI,
        (-Math.atan2(dy, Math.hypot(dx, dz)) * 180) / Math.PI,
      );
    });
    await wait(
      () =>
        [...app.lapisConnection.entities.values()].find((e) => e.kind === "cow")
          ?.renderer.group.children.length >= 8,
    );
    assert.ok(await page.evaluate(async () => {
      const { pickEntity } = await import("/adapter/entities.mjs");
      return pickEntity(app.player, app.lapisConnection.entities,
        app.player.rayTrace(4.5, app.timer.partialTicks), app.timer.partialTicks)?.kind === "cow";
    }), "cow body must be visible and targetable above surface cover");
    await capture(".tests/web-client2-entities.png");
    console.log("Survival inventory and cow model passed");
    await page.evaluate((sensitivity) => {
      app.window.pullMouseMotionX();
      app.window.pullMouseMotionY();
      app.settings.sensitivity = sensitivity;
    }, sensitivity);
    // Measure Web Audio signal after the positional listener, even in muted CI.
    await page.evaluate(async () => {
      const sound = app.soundManager,
        context = sound.audioListener.context;
      await context.resume();
      const analyser = context.createAnalyser();
      analyser.fftSize = 256;
      sound.audioListener.getInput().connect(analyser);
      window.testAudioAnalyser = analyser;
      const position = app.worldRenderer.camera.position;
      sound.playSound(
        "minecraft:block.note_block.harp",
        position.x,
        position.y,
        position.z,
        1,
        1.5,
      );
    });
    await wait(
      () => {
        const data = new Float32Array(testAudioAnalyser.fftSize);
        testAudioAnalyser.getFloatTimeDomainData(data);
        return data.some((value) => Math.abs(value) > 0.0001);
      },
      null,
      { timeout: 5000 },
    );
    assert.equal(
      await page.evaluate(
        () =>
          app.soundManager.soundPool["minecraft:block.note_block.harp"][0]
            .playbackRate,
      ),
      1.5,
    );
    await page.evaluate(() =>
      app.soundManager.audioListener.getInput().disconnect(testAudioAnalyser),
    );
    await page.evaluate(() => app.loadWorld(null));
    await wait(() => !app.world);
    assert.equal(errors.length, 0, errors.join("\n"));
    assert.ok(
      requests.every((url) => url.startsWith(`http://127.0.0.1:${web}/`)),
      "no external asset requests",
    );
    assert.ok(
      requests.every((url) => !url.includes("/src/resources/")),
      "no inherited assets",
    );
    completed = true;
    console.log(
      "Mode 2 Chromium: login, chunks, inventory, chat, movement, held/canceled survival mining, dropped items, cow geometry, audible first-play audio/pitch and disconnect passed",
    );
  } finally {
    await browser?.close();
    server.stdin.end("stop\n");
    const stopTimer = setTimeout(() => server.kill(), 5000);
    if (server.exitCode === null) await once(server, "exit").catch(() => {});
    clearTimeout(stopTimer);
    await rm(cwd, { recursive: true, force: true });
    if (!completed) {
      console.error(logs.slice(-8000));
      console.log("Wire counters", JSON.stringify({ sent, received, digging }));
    }
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
