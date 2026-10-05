// SPDX-License-Identifier: GPL-3.0-only
import { spawn } from "node:child_process";
import { startBridge } from "./bridge.mjs";
let stopping = false,
  bridge;
async function stop() {
  if (stopping) return;
  stopping = true;
  clearTimeout(startup);
  await bridge?.close();
  process.stdin.pause();
}
const startup = setTimeout(() => {
  console.error(
    "Lapis Obsidian Client: no browser connected. Start again with a local WebGL2 browser available.",
  );
  stop();
}, 60000);
bridge = await startBridge({ onIdle: stop });
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
const timer = setInterval(() => {
  if (bridge.authenticated) {
    clearTimeout(startup);
    clearInterval(timer);
  }
  if (stopping) clearInterval(timer);
}, 250);
timer.unref();
if (process.argv.includes("--bootstrap-stdio")) {
  if (process.stdout.isTTY) {
    await stop();
    throw Error("Private bootstrap pipe required");
  }
  process.stdout.write(
    JSON.stringify({ origin: bridge.origin, token: bridge.token }) + "\n",
  );
  process.stdin.resume();
  process.stdin.on("end", stop);
} else {
  const url = `${bridge.origin}/#${bridge.token}`;
  // Only the fixed OS browser opener is executed; neither endpoints nor web controls select programs.
  const [command, args] =
    process.platform === "win32"
      ? ["rundll32.exe", ["url.dll,FileProtocolHandler", url]]
      : process.platform === "darwin"
        ? ["open", [url]]
        : ["xdg-open", [url]];
  const child = spawn(command, args, { stdio: "ignore" });
  child.on("error", async () => {
    console.error(
      "Could not open the system browser. Install a WebGL2-capable browser and try again.",
    );
    await stop();
  });
  console.log(
    "Lapis Obsidian Client is running. Close its tab or choose Quit to stop.",
  );
}
