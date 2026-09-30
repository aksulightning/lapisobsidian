# Notices

Lapis Obsidian is a modified version of p2r3/bareiron, based on commit
02733bee99289a9d2fe152d65bab82c7b3326767. Modifications began 2026-09-30.
The upstream GNU GPL v3 license is preserved in LICENSE.

Compatibility identifiers were selected from PrismarineJS/minecraft-data (MIT,
as declared by that project's README) and misode/mcmeta's 1.21.8 registry lists.
See docs/registries.md for exact revisions, retained fields and maintenance.
Names and numbers are retained solely for protocol interoperability.

The Beta terrain RNG, noise, climate and density C modules are modified ports
of betanium, reference revision 03d77b9dae27a086a2398c21abe52fd07f27195c.
Copyright (C) 2026 Aksu Lightning. These parts are licensed GPL-3.0-or-later.
They are distributed WITHOUT ANY WARRANTY, including implied warranties of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE.
The port replaces the reference implementation's runtime with standalone C
and uses a shared climate lattice for chunk boundaries. No Lua is included
or executed by Lapis Obsidian.

Minecraft is a trademark of Mojang Studios. This project is independent of
Mojang Studios and Microsoft.
