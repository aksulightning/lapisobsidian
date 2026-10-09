#!/bin/sh
# Compile route metadata for mode 2; embed mode 1 assets without Node or downloads.
set -eu
if [ "${LAPIS_OBSIDIAN_WEB_CLIENT:-0}" = 2 ]; then
  # Route metadata only: never embed CC BY-NC application code in GPL binary.
  printf '#define WEB_ASSET_ENTRIES \\\n'
  while IFS='|' read -r route file mime; do
    [ -n "$route" ] || continue
    printf '{"GET %s HTTP/1.1","%s","%s"}, \\\n' "$route" "$mime" "$file"
  done < client2/routes.list
  printf '\n'
  exit 0
fi
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
