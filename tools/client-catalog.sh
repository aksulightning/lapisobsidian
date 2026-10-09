#!/bin/sh
set -eu
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
' include/registries.h src/registries.c
