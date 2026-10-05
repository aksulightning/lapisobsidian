// Derive compatibility labels from the server's source of truth, not a second snapshot.
import { readFileSync, writeFileSync } from "node:fs";
const source = readFileSync(
  new URL("../../include/protocol.h", import.meta.url),
  "utf8",
);
const version = source.match(/#define LAPIS_PROTOCOL_VERSION (\d+)/)?.[1];
const release = source.match(/#define LAPIS_MINECRAFT_VERSION "([\d.]+)"/)?.[1];
if (!version || !release) throw new Error("Cannot read protocol constants");
writeFileSync(
  new URL("../web/protocol-version.ts", import.meta.url),
  `// Generated from include/protocol.h. Do not edit.\nexport const protocolVersion = ${version};\nexport const serverRelease = '${release}';\n`,
);
