// SPDX-License-Identifier: GPL-3.0-only
import { connect } from "cloudflare:sockets";
import { createGateway } from "./handler.mjs";

export default createGateway(connect);
