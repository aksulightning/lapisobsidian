// SPDX-License-Identifier: GPL-3.0-only
import { BridgeTransport } from "./transport.js";

export function gatewayURL(value) {
  let url;
  try { url = new URL(value); } catch {
    throw Error("Enter the secure gateway URL supplied by your server owner (wss://…).");
  }
  if (url.protocol !== "wss:" || url.username || url.password || url.hash || url.search)
    throw Error("The gateway must use wss:// with no credentials, query, or fragment.");
  return url.href;
}

export class HostedTransport extends BridgeTransport {
  async prepare(value) {
    const endpoint = gatewayURL(value);
    if (this.ready && this.endpoint === endpoint) return;
    this.close();
    this.endpoint = endpoint;
    await new Promise((resolve, reject) => {
      const finish = (error) => {
        clearTimeout(timer);
        this.onReady = this.onUnavailable = () => {};
        if (error) { this.close(); reject(error); }
        else resolve();
      };
      const timer = setTimeout(() => finish(Error("Gateway connection timed out.")), 12000);
      this.onReady = () => finish();
      this.onUnavailable = finish;
      try { this.start("", endpoint); } catch (error) { finish(error); }
    });
  }
}
