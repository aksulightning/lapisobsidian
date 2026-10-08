#!/bin/sh
# Emit deterministic C arrays; only used by opt-in builds. No downloads/runtime.
set -eu
for name in index.html style.css protocol.mjs renderer.mjs client.mjs; do
  symbol=$(printf '%s' "$name" | tr '.-' '__')
  printf 'static const unsigned char web_%s[] = {\n' "$symbol"
  od -An -v -tu1 "web/$name" | awk '{for(i=1;i<=NF;i++) printf "%s,", $i; print ""}'
  printf '};\n'
done
printf 'static const unsigned char web_catalog_mjs[] = {\n'
awk '
  BEGIN { print "// Generated from the server registry at compile time."; print "export const items = {" }
  $1 == "#define" && $2 ~ /^I_/ { printf "%s:\"%s\",\n", $3, substr($2,3) }
  $1 == "#define" && $2 ~ /^B_/ { names[$3] = substr($2,3) }
  /^const uint16_t block_palette\[\]/ {
    print "};\nexport const blocks = {";
    sub(/^[^{]*\{ */, ""); sub(/ *\};.*/, "");
    count=split($0, ids, /, */);
    for(i=1;i<=count;i++) printf "%s:\"%s\",\n", ids[i], names[i-1];
    print "};";
  }
' include/registries.h src/registries.c | od -An -v -tu1 | awk '{for(i=1;i<=NF;i++) printf "%s,", $i; print ""}'
printf '};\n'
