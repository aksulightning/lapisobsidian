# Milestone 7: your first tools

Join an ordinary survival world with `connect.sh NAME HOST PORT` (Windows:
`connect.bat NAME HOST PORT`). Terrain, inventory, recipes and drops come from
Lapis Obsidian. This walkthrough matches the pinned testing server.

1. Walk to a tree. Hold left mouse on its trunk to mine four logs; approach the
   drops to collect them. Releasing the button cancels mining progress.
2. Press B to open inventory. Put logs into one crafting cell, then Shift-click
   the result to turn them into planks. Four logs yield sixteen planks.
3. Make a workbench and four sticks using the recipes below. Right-click with a
   carried stack to place one ingredient at a time; left-click an empty inventory
   slot to put the remaining stack away.
4. Hover the workbench item and press 9 to move it to hotbar slot 9. Close the
   inventory, select 9, and right-click a nearby ground face to place it. Release
   the button and right-click the workbench again to open its 3×3 grid.
5. Make a wooden pickaxe. Hover it and press 8 to equip it in hotbar slot 8.
   Close the screen, select 8 and mine exposed stone to collect cobblestone.

| Recipe | Grid | Ingredient placement | Result |
| --- | --- | --- | --- |
| Planks | 2×2 or 3×3 | One oak log in any cell | 4 planks |
| Sticks | 2×2 or 3×3 | One plank directly above another | 4 sticks |
| Workbench | 2×2 | One plank in each cell | 1 crafting table |
| Wooden pickaxe | 3×3 | Three planks across the top row; sticks in the middle and bottom centre cells | 1 wooden pickaxe |
| Stone pickaxe | 3×3 | Same arrangement, replacing planks with cobblestone | 1 stone pickaxe |
| Furnace | 3×3 | Cobblestone around the outside; centre empty | 1 furnace |

The displayed result and ingredient consumption are supplied by the server.
A result appearing before every ingredient update is normal packet ordering;
wait for the screen to settle before the next operation.

Inventory shortcuts:

- Left click: move a stack. Right click: take half or place one.
- Shift-click: transfer a stack, or craft repeated results when space permits.
- Hover an item and press 1–9: swap it with that hotbar slot.
- Hover an item and press the configured Drop key (G by default): drop one.
  Hold Shift with the Drop key to drop the whole stack.
- B or Escape: close. Opening the player inventory asks the server to refresh
  its crafting cells; a brief updating message hides stale cell contents.

Hold right mouse to eat food; release before starting another use. Health and
food are shown above the hotbar. Lapis currently models tool wear with a chance
of breakage rather than a durability bar, so retain spare crafting materials.

The automated wood-to-tools test also verifies dropping/picking up a stack and
reconnecting with earned items. It does not cover a complete survival session,
server restarts, full inventories, or every recipe in this guide. In particular,
stone-pickaxe and furnace construction from freshly gathered resources are next
progression goals; the earlier furnace tests used supplied materials.
