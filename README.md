# Lapis Obsidian

This is an independent project, unaffiliated with Mojang Studios or Microsoft.

**This is a playable prototype.** Some features are unfinished, bugs are
expected, and it does not include everything in "Minecraft".

A small, experimental block-building game project inspired by the feel of
"Minecraft" Beta 1.7.3. Explore, gather materials, craft tools, build a home and
grow food.

Lapis Obsidian runs the world. **LapisCube** is the companion game you use to
play, with options for playing alone in your browser or joining a server.

## Try it on your own

The simplest way to try LapisCube is the singleplayer browser version. You do
not need "Minecraft", a "Microsoft" account or a separate game server.

1. Open the [singleplayer downloads page](https://github.com/aksulightning/lapisobsidian/actions/workflows/lapiscube-singleplayer.yml).
2. Choose a successful build of the version you want to try. Scroll to
   **Artifacts** and download **LapisCube-singleplayer-drops-0**.
   GitHub may ask you to sign in to download it.
3. Unzip the download and open **LapisCube-singleplayer.html** in a current
   desktop Chrome or Chromium browser.
4. Choose a player name and press **Play**.

The game is contained in that one file and can be played without an internet
connection after downloading it. These are development downloads, so available
builds may change. If the file will not open or save properly, see the
[singleplayer guide](clients/lapiscube/docs/singleplayer.md).

### Your first steps

| To… | Use… |
| --- | --- |
| Move and look around | W, A, S, D and the mouse |
| Jump | Space |
| Break a block | Hold the left mouse button |
| Place a block or use something | Right mouse button |
| Open your bag and crafting | B |
| Choose an item | Number keys 1–9 |

Start by gathering wood from a tree. The
[first-tools walkthrough](clients/lapiscube/docs/survival-guide.md) shows how to
make a workbench and your first pickaxe.

### Keep your world

Use **Save & quit** before closing the game. Your world is saved in the browser
you used to play.

Return with the same browser profile, player name and file location. Clearing
browser data can erase your world. Copying the game file to another computer
does not copy your save, and world import/export is not available yet.

## Play with other people

Multiplayer needs someone to run a Lapis Obsidian server. Ask the host which
version to use and how to join.

- **LapisCube:** play through the host's browser link or use the desktop client.
- **"Minecraft" Java Edition:** the supported version is **1.21.8**.

The normal server setup does not verify player accounts or encrypt game
connections by default. Browser connections can be protected with HTTPS.
An experimental option verifies "Minecraft" accounts and encrypts Java gameplay;
LapisCube cannot join servers using that option yet.

If you want to host a game, start with the
[technical guide](docs/technical.md) and
[browser hosting guide](clients/lapiscube/docs/web.md).

## About the use of AI

AI tools have been used to help develop this prototype. AI is one of the tools
used in the project, and its suggestions can be incomplete or wrong. The people
maintaining the project remain responsible for what is included, checking how
it behaves, and being honest about its limitations. There is still work to do.

## Learn more or help

- [Technical guide](docs/technical.md): the previous README, with build instructions and detailed notes.
- [Plans and progress](docs/roadmap.md): what works and what is still planned.
- [Report a problem or suggest an idea](https://github.com/aksulightning/lapisobsidian/issues): tell us what happened and which version you used.
- [Credits](docs/credits.md), [notices](NOTICE.md) and [license](LICENSE): the people, projects and materials this work builds on.

Lapis Obsidian builds on **bareiron**, and LapisCube uses **ClassiCube**.
