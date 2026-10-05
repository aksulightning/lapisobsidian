// Node harness for the exact browser worker module, without a WebGL mock world.
import { parentPort } from "node:worker_threads";
globalThis.self = globalThis;
globalThis.postMessage = (data, transfer) => parentPort.postMessage(data, transfer);
await import("../web/world/worker.js");
parentPort.on("message", data => self.onmessage({ data }));
