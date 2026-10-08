#!/bin/sh
# Checked-in route whitelist: no Node, downloads or runtime filesystem serving.
set -eu
index=0
while IFS='|' read -r route file mime; do
  [ -n "$route" ] || continue
  printf 'static const unsigned char web_asset_%s[] = {\n' "$index"
  if [ "$file" = '@catalog' ]; then
    sh tools/client-catalog.sh
  else
    cat "$file"
  fi | od -An -v -tu1 | awk '{for(i=1;i<=NF;i++) printf "%s,", $i; print ""}'
  printf '};\n'
  index=$((index+1))
done < client/embed.list
printf '#define WEB_ASSET_ENTRIES \\\n'
index=0
while IFS='|' read -r route file mime; do
  [ -n "$route" ] || continue
  printf '{"GET %s HTTP/1.1","%s",web_asset_%s,sizeof(web_asset_%s)}, \\\n' "$route" "$mime" "$index" "$index"
  index=$((index+1))
done < client/embed.list
printf '\n'
