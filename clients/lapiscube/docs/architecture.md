# Architecture and implementation plan

## Inspection and scope

Before implementation, ClassiCube's module/style docs, Server/Protocol/main entry,
platform sockets, renderer/world/input/audio/model/UI APIs and makefiles were
reviewed. Lapis testing dispatch, packets, procedures, registry generator/snapshot,
inventory, interaction, mob, sign/door/circuit/farming/fluid and musicbox code were
inspected. The pinned Lapis source is authoritative; no vanilla behavior is
substituted where the server differs.

The incremental plan is: retain M1 framing/connection; decode bounded chunks and
render through the engine; add server-authoritative actions and presentation;
add entities/special blocks/audio; finish independent assets, builds and tests.
The original engine submodule stays untouched; the patch is applied to a build copy.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `LapisProtocol` | Framing, state machine, identifiers, login, teleport/keepalive and output queue |
| `LapisSession` | Nonblocking partial I/O, deadlines and budgets |
| `LapisIdentity` | Offline UUIDv3 with existing BearSSL MD5 |
| `LapisWorld` | Pure bounded palette/chunk/light validation, cache, block updates and authoritative world clock |
| `LapisGameplay` | Server-owned stacks/containers/health; bounded outgoing action encoding |
| `LapisEntities` | Bounded entity snapshots, movement and metadata parsing |
| `LapisSigns` / `LapisText` | Bounded sign cache/editor requests and validated modified-UTF-8 ↔ UTF-8 text |
| `LapisMining` | Pure local material/tool pacing; no world or inventory mutations |
| `LapisEffects` | Named sound packet decoding |
| `LapisBackend` | Engine sockets/lifecycle, moving map origin, player controls and packet routing |
| `LapisBlocks` / generated `LapisFacts` | Numeric state/item facts and native visual/collision definitions |
| `LapisGui` | Native HUD and server-backed inventory/container screens, sign editor/reading overlay and mining progress |
| `LapisMobs` | Original geometric models, native entity interpolation and targeting |
| `LapisAudio` | Original bounded synthesis and existing audio-pool playback |
| `engine/src/Protocol.c` | Untouched Classic/CPE protocol |
| `tools/probe.c` | Headless test harness around shared decoders, not the product client |

`Graphics`, `Builder`, `MapRenderer`, `Input`, `Window`, `Audio`, `Gui`, `Entity`
and `Model` remain engine components. Input hooks submit Lapis requests before
Classic optimistic world edits. Only server block/slot packets mutate Lapis state.
No client crafting recipe computation or item creation is used.

## World adapter and resource bounds

The fixed cache holds 49 chunks × 16×256×16 × two bytes (about 6.1 MiB), plus one
128 KiB staging chunk. All 24 wire sections are validated; Y=0..255 is retained.
A 112×256×112 engine map uses two byte arrays (about 6.1 MiB). Global coordinates
stay separate from local render coordinates; negative chunk division is floored.
An unloaded region is an invisible solid barrier. Center changes evict old cache
entries and shift player/entity coordinates; no terrain is generated locally.

Cache updates currently refill/refresh the dense map. This bounds memory but can
cause hitches; incremental column copies, lighting invalidation and mesh scheduling
remain performance work. The existing engine adds its normal mesh/texture memory.
Tiny-console compatibility or a total process-memory ceiling is not claimed.

- One 512 KiB incoming frame, 32 KiB outbound queue; no per-packet heap allocation.
- At most 32 registries, 2,048 identifiers and 64 KiB name storage.
- At most 255 entity snapshots and 16 windows × 64 item slots.
- Per-pump input budget 256 KiB, 16 KiB read scratch; partial writes retained.
- Connection/incomplete-frame timeout 15 seconds; idle receive timeout 30 seconds.
- Three-byte bounded frame lengths, five-byte field VarInts, checked arrays/palettes.
- At most 128 signs × two sides × four 96-byte UTF-8 lines; refreshed chunks invalidate cached text.
- Eight synthesized audio banks/voices; UI caches bounded text textures.
- Unsupported configuration fails explicitly. Unknown Play packets remain bounded;
  the core `skipped` counter means delegated to gameplay, not necessarily ignored.
- EOF, malformed frames and queue overflow close the connection with diagnostics.
- DNS is synchronous and uses the first resolved address; async lookup/fallback
  remains work. Numeric IP addresses avoid name-resolution stalls.

The desktop build requires extended block/texture support. Other upstream ports
need explicit memory/platform work before they can be called supported. Classic
packet selection is tested independently. See milestone and validation documents
for behavior that is implemented but not yet accepted through actual gameplay.

## Survival compatibility details

Opening a container does not resend the player inventory at this server revision.
The client aliases the already-authoritative slots into each menu and mirrors
updates back. Direct window -2 is also handled. Crafting, smelting and cursor
results always come from server slot packets; no recipe execution runs locally.

One right-button press emits one use request. Holding it lets the server's eating
timer complete; release emits action 5. Repeated Use requests reset that timer.
The server's saturation conversion can yield -0.4, so that valid wire range is
accepted instead of disconnecting a hungry player.

The server has instant-break rules but no timed hardness enforcement. Non-instant
mining durations are explicitly client presentation choices. The client cancels
when the target state, selected tool, mode or input changes. Authoritative updates
still decide removal, loot and tool durability.

Clock packet 6A carries world age, day ticks modulo 24000 and a ticking boolean.
Sixteen brightness steps tint engine sky/fog/sun/shadow colours only when their
values change. Time is not advanced independently and terrain is never generated
locally. This shading does not replace a full block-light implementation.

## Milestone 7 inventory synchronization

The pinned server's Close Container returns ingredients and sends cursor/player
slot updates, but does not resend the old crafting cells. Opening the player
inventory now queues a **hotbar self-swap** (window 0, slot 36, button 0, mode 2),
which the server explicitly treats as a no-op. Its cached-slot reports list the
four last-known 2×2 ingredients; `inventory_packets.c:sync_window` sends their
actual contents, plus the output and cursor. The client waits for those slot
updates, hides pending cells and never changes their item counts itself.

These reports are snapshots, not proposed items or client-computed crafting
results. Ordinary clicks/swaps/drops still send zero changed-slot reports.
Refresh uses no new server packet, relaxed validation or modified server code.
Inputs retained outside the visible 2×2 area after a full 3×3 close still require
separate acceptance; this mechanism makes no claim to solve that server edge case.

Number keys over a native inventory cell send mode-2 swaps. The configured Drop
key sends mode 4, with Shift selecting the entire stack. Window-specific bounds
are checked before encoding; only server slot/cursor packets update the display.
