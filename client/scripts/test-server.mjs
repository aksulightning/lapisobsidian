// SPDX-License-Identifier: GPL-3.0-only
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
const result = spawnSync("bash", ["./build.sh"], {
  cwd: fileURLToPath(new URL("../../", import.meta.url)),
  stdio: "inherit",
});
if (result.error) throw result.error;
if (result.status) process.exit(result.status);
