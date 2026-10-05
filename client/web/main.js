// SPDX-License-Identifier: GPL-3.0-only
import { BridgeTransport } from "./transport.js";
import { Renderer } from "./renderer.js";
import { Input } from "./input.js";
import { Game } from "./game.js";
import { material, itemName } from "./world/blocks.js";
import { registry } from "./protocol/registry.js";
const $ = (id) => document.getElementById(id),
  touch = matchMedia("(any-pointer:coarse)").matches;
const settings = {
  distance: touch ? 2 : 4,
  quality: touch ? "low" : "medium",
  fps: 60,
  scale: "normal",
  mouse: 0.13,
  touch: 0.22,
  controls: "auto",
};
try {
  const saved = JSON.parse(localStorage.getItem("lapis-settings") || "{}");
  for (const k of Object.keys(settings))
    if (typeof saved[k] === typeof settings[k]) settings[k] = saved[k];
} catch {}
function notice(message) {
  $("error").hidden = false;
  $("error").querySelector("span").textContent = message;
}
$("error-close").onclick = () => ($("error").hidden = true);
let game,
  input,
  renderer,
  connected = false,
  connecting = false,
  lastError = false;
function status(message) {
  $("connection-status").textContent = message;
  $("hud-status").textContent = message;
  $("connect").disabled = !transport.ready || connected || connecting;
}
const transport = new BridgeTransport(status);
transport.onError = (e) => {
  lastError = true;
  notice(e.message);
  status("Connection failed");
};
transport.onClose = () => {
  connected = connecting = false;
  if (game) game.playing = false;
  input?.stop();
  $("hud").hidden = true;
  $("touch-controls").hidden = true;
  $("resume").hidden = $("disconnect").hidden = true;
  $("connect-form").hidden = false;
  hideOverlays();
  $("menu").hidden = false;
  document.body.classList.remove("in-world");
  status(lastError ? "Connection failed" : "Disconnected");
};
function showTouch() {
  return (
    settings.controls === "touch" || (settings.controls === "auto" && touch)
  );
}
function hideOverlays() {
  for (const id of ["settings", "about", "inventory", "chat-form", "death"])
    $(id).hidden = true;
}
function menu() {
  if (game && connected && !$("inventory").hidden) {
    game.protocol.closeInventory(game.window);
    game.window = 0;
  }
  input?.stop();
  hideOverlays();
  $("menu").hidden = false;
  $("touch-controls").hidden = true;
  document.body.classList.remove("in-world");
}
function resume(lock = false) {
  if (!connected || game.health <= 0) return;
  hideOverlays();
  $("menu").hidden = true;
  $("hud").hidden = false;
  $("touch-controls").hidden = !showTouch();
  document.body.classList.add("in-world");
  input.active = true;
  if (lock && !showTouch()) input.lock();
}
function openChat() {
  if (!connected) return;
  input.stop();
  $("chat-form").hidden = false;
  $("chat-input").focus();
}
function slotButton(stack, index, callback) {
  const b = document.createElement("button");
  b.className = "slot";
  b.dataset.slot = index;
  b.title = stack?.count ? itemName(stack.item) : `Slot ${index + 1}`;
  const name = document.createElement("span");
  name.className = "name";
  name.textContent = stack?.count ? itemName(stack.item) : "";
  const count = document.createElement("strong");
  count.textContent = stack?.count || "";
  b.append(name, count);
  b.onclick = (e) => callback(e);
  return b;
}
function renderHotbar() {
  if (!game) return;
  const slots = game.inventory.get(0),
    bar = $("hotbar");
  bar.replaceChildren();
  for (let i = 0; i < 9; i++) {
    const b = slotButton(slots?.get(36 + i), i, () => game.select(i));
    b.classList.toggle("selected", i === game.slot);
    const n = document.createElement("small");
    n.textContent = i + 1;
    b.append(n);
    bar.append(b);
  }
}
function renderInventory() {
  if (!game) return;
  const slots = game.inventory.get(game.window),
    grid = $("inventory-slots");
  grid.replaceChildren();
  const count = game.window === 2 ? 63 : game.window === 14 ? 39 : 46;
  for (let i = 0; i < count; i++) {
    const b = slotButton(slots?.get(i), i, (e) =>
      game.protocol.click(game.window, i, 0, e.shiftKey),
    );
    b.oncontextmenu = (e) => {
      e.preventDefault();
      game.protocol.click(game.window, i, 1);
    };
    grid.append(b);
  }
  $("cursor-item").textContent = game.cursor.count
    ? `Holding ${game.cursor.count} ${itemName(game.cursor.item)} — click a slot to place`
    : "Click to pick up or place. Right-click splits; Shift-click moves stacks.";
  $("creative").hidden = game.mode !== 1;
}
function openInventory() {
  if (!connected) return;
  input.stop();
  $("inventory").hidden = false;
  $("inventory-title").textContent = game.window ? "Container" : "Inventory";
  renderInventory();
}
function closeInventory() {
  if (connected) {
    game.protocol.closeInventory(game.window);
    game.window = 0;
  }
  resume();
}
function event(type, value) {
  switch (type) {
    case "status":
      status(value);
      break;
    case "error":
      lastError = true;
      notice(value);
      status("Connection failed");
      break;
    case "ready":
      connected = true;
      connecting = false;
      $("connect-form").hidden = true;
      $("resume").hidden = $("disconnect").hidden = false;
      $("error").hidden = true;
      renderHotbar();
      resume();
      break;
    case "chat": {
      const p = document.createElement("p");
      p.textContent = value;
      $("chat-log").append(p);
      while ($("chat-log").childElementCount > 6)
        $("chat-log").firstChild.remove();
      break;
    }
    case "slot":
    case "cursor":
    case "held":
      renderHotbar();
      if (!$("inventory").hidden) renderInventory();
      break;
    case "inventory":
      openInventory();
      $("inventory-title").textContent = value.title;
      break;
    case "health":
      $("health").textContent =
        `Health ${Math.max(0, Math.ceil(value.health))} / 20  ·  Food ${value.food}`;
      if (value.health <= 0) {
        input.stop();
        $("death").hidden = false;
      }
      break;
    case "respawn":
      hideOverlays();
      status("Loading world...");
      break;
  }
}
try {
  renderer = new Renderer($("world"), settings, notice);
  game = new Game(transport, renderer, settings, event);
  input = new Input($("world"), settings, (type, value) => {
    switch (type) {
      case "menu":
        menu();
        break;
      case "chat":
        openChat();
        break;
      case "inventory":
        openInventory();
        break;
      case "slot":
        game.select(value);
        break;
      case "wheel":
        game.select(game.slot + value);
        break;
      case "drop":
        game.protocol.action(4);
        break;
      case "fullscreen":
        fullscreen();
        break;
      case "notice":
        notice(value);
        break;
    }
  });
} catch (e) {
  console.error(e);
  notice(e.message);
}
let token = location.hash.slice(1);
if (token) {
  sessionStorage.setItem("lapis-token", token);
  history.replaceState(null, "", location.pathname);
} else token = sessionStorage.getItem("lapis-token");
if (/^[a-f0-9]{64}$/.test(token || "")) {
  transport.start(token);
  token = "";
} else
  notice(
    "Launch Lapis Obsidian Client using its bundled launcher. A static webpage cannot open the local bridge.",
  );
$("connect-form").onsubmit = async (e) => {
  e.preventDefault();
  if (!game) return;
  lastError = false;
  $("error").hidden = true;
  connecting = true;
  status("Connecting...");
  try {
    const name = $("username").value;
    if (!/^\w{1,15}$/.test(name))
      throw Error(
        "Use 1–15 letters, digits, or underscores for your player name.",
      );
    const digest = await crypto.subtle.digest(
      "SHA-256",
      new TextEncoder().encode("LapisObsidian:" + name),
    );
    const uuid = new Uint8Array(digest).slice(0, 16);
    uuid[6] = (uuid[6] & 15) | 64;
    uuid[8] = (uuid[8] & 63) | 128;
    await game.connect(
      $("host").value.trim(),
      Number($("port").value),
      name,
      uuid,
    );
  } catch (err) {
    connecting = false;
    notice(err.message);
    status("Connection failed");
  }
};
$("disconnect").onclick = () => game.disconnect();
$("resume").onclick = () => resume(true);
$("menu-open").onclick = menu;
$("death-menu").onclick = menu;
$("chat-open").onclick = openChat;
$("chat-close").onclick = () => resume();
$("chat-form").onsubmit = (e) => {
  e.preventDefault();
  try {
    game.protocol.chat($("chat-input").value);
    $("chat-input").value = "";
    resume();
  } catch (err) {
    notice(err.message);
  }
};
$("inventory-open").onclick = openInventory;
$("inventory-close").onclick = closeInventory;
$("respawn").onclick = () => {
  game.protocol.respawn();
  $("death").hidden = true;
};
$("settings-open").onclick = () => {
  $("menu").hidden = true;
  $("settings").hidden = false;
};
$("settings-close").onclick = () => {
  $("settings").hidden = true;
  $("menu").hidden = false;
};
$("about-open").onclick = () => {
  $("menu").hidden = true;
  $("about").hidden = false;
};
$("about-close").onclick = () => {
  $("about").hidden = true;
  $("menu").hidden = false;
};
for (const el of $("settings-form").elements) {
  el.value = settings[el.name];
  el.onchange = () => {
    settings[el.name] = ["distance", "fps", "mouse", "touch"].includes(el.name)
      ? Number(el.value)
      : el.value;
    localStorage.setItem("lapis-settings", JSON.stringify(settings));
    document.body.classList.remove("ui-small", "ui-normal", "ui-large");
    document.body.classList.add("ui-" + settings.scale);
    game?.prune();
  };
}
async function fullscreen() {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else {
      await document.documentElement.requestFullscreen();
      if (showTouch())
        await screen.orientation?.lock?.("landscape").catch(() => {});
    }
  } catch {
    notice("Fullscreen is unavailable in this browser.");
  }
}
$("fullscreen").onclick = fullscreen;
$("quit").onclick = () => {
  transport.control("SHUTDOWN");
  input?.stop();
  notice("Lapis Obsidian Client has shut down. You can close this tab.");
};
for (const [name, id] of Object.entries(registry.items)
  .filter(([n,id]) => id > 0 && registry.palette[n] !== undefined)
  .slice(0, 100)) {
  const b = slotButton({ item: id, count: 64 }, id, () =>
    game.protocol.creative(36 + game.slot, id),
  );
  $("creative-items").append(b);
}
window.addEventListener("pagehide", () => {
  game?.worker.terminate();
  transport.close();
});
let previous = performance.now(),
  renderTime = 0,
  statTime = 0;
function frame(now) {
  const dt = Math.min(0.05, (now - previous) / 1000);
  previous = now;
  try {
    if (game && input) {
      game.update(dt, input.frame());
      if (now - renderTime >= 1000 / settings.fps - 1) {
        renderer.render(game.player, game.entities);
        renderTime = now;
      }
      if (now - statTime > 250) {
        statTime = now;
        const p = game.player;
        $("coordinates").textContent =
          `${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)} · ${game.world.chunks.size} chunks`;
        $("target-label").textContent = game.target
          ? material(
              game.world.get(game.target.x, game.target.y, game.target.z),
            ).name.replaceAll("_", " ")
          : "";
      }
    }
  } catch (e) {
    console.error(e);
    notice(
      "Gameplay stopped because of a connection or graphics error. Reconnect to try again.",
    );
    game?.disconnect();
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
// Read-only diagnostics support integration tests without exposing transport credentials.
Object.defineProperty(window, "lapisDiagnostics", {
  get: () =>
    game
      ? {
          connected: game.playing,
          position: { ...game.player },
          chunks: game.world.chunks.size,
          meshes: renderer.chunks.size,
          vertices: renderer.drawn,
          entities: game.entities.size,
          health: game.health,
          slot: game.slot,
          pendingChunks: game.pendingChunks,
          target: game.target,
        }
      : null,
});
