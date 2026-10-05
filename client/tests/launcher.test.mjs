// SPDX-License-Identifier: GPL-3.0-only
import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { once } from "node:events";
import { fileURLToPath } from "node:url";
import net from "node:net";
import { socketFor, until } from "./helpers.mjs";
test("launcher automatically starts authenticated bridge and exits when its UI closes", async () => {
  const child = spawn(
    process.execPath,
    ["runtime/launcher.mjs", "--bootstrap-stdio"],
    {
      cwd: fileURLToPath(new URL("../", import.meta.url)),
      stdio: ["pipe", "pipe", "pipe"],
    },
  );
  const lines = createInterface({ input: child.stdout });
  const [line] = await once(lines, "line");
  const bootstrap = JSON.parse(line);
  lines.close();
  let peer;
  try {
    peer = await socketFor(bootstrap);
    await peer.control("AUTH_OK");
    assert.equal((await fetch(bootstrap.origin)).status, 200);
    peer.ws.close();
    await until(() => child.exitCode !== null, "automatic launcher exit", 6000);
    assert.equal(child.exitCode, 0);
    const socket = net.connect(
      Number(new URL(bootstrap.origin).port),
      "127.0.0.1",
    );
    const [error] = await once(socket, "error");
    assert.equal(error.code, "ECONNREFUSED");
  } finally {
    peer?.ws.terminate();
    if (child.exitCode === null) {
      child.stdin.end();
      child.kill("SIGTERM");
    }
  }
});
