// SPDX-License-Identifier: GPL-3.0-only
import { connect } from "cloudflare:sockets";
import { DurableObject } from "cloudflare:workers";
import { HTTPSession } from "./session.mjs";
export { default } from "./handler.mjs";

export class TCPSession extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.session = new HTTPSession(env, connect);
  }
  fetch(request) { return this.session.fetch(request); }
}
