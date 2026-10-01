# Musicbox songs

Put server-owned `.mid` files here, then restart or run `/music reload` as an
administrator. Right-click a placed jukebox and enter the numbered `/music`
command shown in chat. Any nearby player can play or stop that box.

To create a small original demo melody, run from the repository root:

```sh
cc tools/make_demo_song.c -o make-demo-song
./make-demo-song
```

This creates `songs/lapis-demo.mid` and refuses to overwrite an existing file.
The generated melody is dedicated to CC0. No Minecraft disc audio is included.
Only distribute other songs when you have the rights to do so.

Names must end in lowercase `.mid`. Use ASCII letters, numbers, spaces, `-` or
`_` before the extension, with at most 63 bytes for the complete filename.
Subdirectories, symlinks and arbitrary paths supplied through chat are not used.
The first 16 eligible names in bytewise alphabetical order appear in the menu.
Files are limited to 64 KiB. See [musicbox limits](../docs/redstone-and-music.md).
