# Banked-memory rework: status and plan

Goal: move the game off "copy everything into RAM" and onto EasyFlash banks -
code executed in place, static assets fetched to fixed destinations - so
resident RAM holds only what genuinely must be resident.

`MEMORY_MAP.md` describes the layout as it *is*. This file records what has
been done, what is blocked and why, and the order the remaining work has to
happen in. Update it as steps land.

## Done

| Step | Effect |
|---|---|
| `KIND_ASSET` resources | Charsets and tile data are fetched at boot instead of linked in. Byte-neutral so far (see below), but they are now swappable and no longer image-constrained. |
| `FAR_CALL` + trampoline | Banked routines callable by far address (bank + 16-bit offset), reentrant, with a cc65 `A`/`X` argument and return convention. |
| Software stack -> `$C000` | 256 bytes against a measured peak of 37. Prerequisite for banked C: cc65 code touches its stack constantly, and `$BA00` was inside the banking window. |
| `GameState` -> `$C100` | Banked code can read game state directly. |
| Raster IRQ closure -> below `$8000` | Frame/rain state and every per-frame callee are bank-visible; bank wrappers now mask only the map/register transition and leave the IRQ running during copies/far calls. |
| Cold object-type table -> bank 46 ROMH | Fixed a real pre-existing bug: later bank-47 modules were overwriting it, so every type above the room-helpers offset returned module code instead of its name and flags. |
| First in-place banked module | `platform_object_type_info_get()` runs from bank 47, and itself fetches from bank 46 while doing so. |
| Generated fixed-module layout | `cfg/easyflash_layout.json` generates runtime/ca65 constants; the final image validator checks every payload and the in-place linked entry. |
| Inventory -> in-place bank 48 ROMH | Removed its copy/validate cycle and 32-byte selected-slot cache. Five mutable bytes live at `$C174-$C178`; the 1,315-byte code/RODATA image runs at `$A000-$A522` and nests through bank 47/46 for cold type names. |
| Mode-aware far calls | The generated descriptor now carries CPU map and EasyFlash control separately. Type Info runs as bank 47 ROML with `$01=$37`/`$DE02=$06`; Inventory remains bank 48 ROMH with `$37`/`$07`. The validator applies the effective visibility contract. |
| Former `SV` overlay reduced below one page | Before conversion, Save-name editing reused the selected `SINDEX` entry and shrank the copy-to-RAM image enough to stabilize it. That compatibility step is now superseded by in-place SV. |
| Atomic VIC/layout migration | VIC bank 3 now owns `$E800-$FDFF`; room/staging/journal/BSS moved below `$3000`; the text module moved to `$3A00`; rebuildable work/staging moved to `$B000`. PAL and NTSC VICE traces cover the new paths. |
| Room Helpers -> in-place bank 47 ROML | Removed RH's copy/header/checksum/BSS path. Its 452 read-only bytes run at `$84C0-$8683`; the seven-byte ABI remains below `$2000`. Hot-type lookup and dirty marking moved before the far call so their `$34` -> `$35` mapping cycle cannot unmap the executing ROML service. PAL and NTSC tests cover neighbor lookup and a synthetic take/removal using type 106 under I/O RAM. |
| Look Helpers -> in-place bank 47 ROML | Removed LH's copy/header/checksum/BSS path. Its 2,017 read-only bytes run at `$9300-$9AE0`; 595 mutable bytes borrow `$2400` room-staging scratch, including a hit cache that halves collision lookups. `platform_object_type_get()` now restores the caller's exact `$01`, making type-106 collision safe from ROML. Type Info moved to `$9F80-$9FBA`. PAL/NTSC traces cover both Look operations, nested Type Info, Take/RH/LH sequencing, IRQ progress, and map restoration. |
| Script interpreter -> in-place bank 47 ROML | Removed SC's copy/header/checksum/BSS path. Its 2,633 read-only bytes run at `$8800-$9248`; its 10-byte window descriptor occupies `$27E9-$27F2`, and script content continues to use `$2400` as a sliding window. Six potentially hidden/transitively unsafe engine actions use one shared RAM-call dispatcher, which selects cartridge-off `$35/$04` through the reentrant far-call trampoline and restores `$37/$06` afterward. PAL/NTSC traces cover text, lightning, portrait fetch/show/hide, inventory mutation, transition message/request, IRQ progress, and exact map restoration. |
| SL/SV -> in-place bank 48 ROML | SL runs at `$8000-$894D`; SV runs at `$8A00-$96EE`. They share `$0400-$0491`, borrow `$3000-$3479` for the record, and restore all tile data before drawing. Disk and world-capture calls use explicit host gates. PAL save and PAL/NTSC load round trips verify exact maps and restored tile bytes. |
| Resident disk boundary | One `$B800-$BA0E` KERNAL driver runs under cartridge-off `$36/$04`. Each IEC transaction suspends/resynchronizes the raster source. Zone-B object bootstrap staging is split through `$B000-$B7FF`, so it cannot overwrite the driver. |
| Copied-service ABI removed | The generic `$B000` loader, run-time header/checksum validator, text-module validator tail, and obsolete finalizer are gone. `$B000` remains only rebuildable renderer RAM and bounded staging. |
| Cartridge-only runtime | The game D64 build/run path is gone. Disk device 8 exists only for `saves.d64`. |

## Interrupt/banking safety audit (2026-09-07)

Prompted by reports of live gameplay glitching (screen corruption, apparent
missed interrupts) attributed to this banking work. Full analysis session;
one confirmed bug found and fixed, one audit performed and closed out clean,
two standing risks recorded for the future.

### Bug found and fixed: ordinary edge-of-room walking bypassed the room-switch bracket

`platform_room_enter()`'s contract (`src/platform.h`) requires every caller to
bracket it - together with the `game_enter_room()`/`game_enter_tile()` sync
that must follow it, with no gap - inside one
`platform_screen_blank()`/`_unblank()` plus `raster_irq_suspend()`/`_resume()`
window. Without that bracket, `game_enter_room()`'s environment-module fetch
is exposed to a real, previously-hit failure mode documented in its own
comment (`src/game.c`): `platform_resource_fetch()`'s post-copy checksum loop
re-enables interrupts before it finishes summing the just-copied bytes, so the
raster IRQ can fire mid-checksum and run the freshly-copied (not yet
validated) environment module's `env_tick`, which mutates its own
persistent-state bytes - changing the very bytes still being summed and
spuriously failing the checksum. That exact symptom (room 00's environment
module silently reverting to the null stub) was found live in VICE once
already and fixed - but only for the callers that already used the bracket.

Three call sites into `platform_room_enter()` used the bracket correctly:
startup (`src/main.c`), scripted transitions
(`game_process_pending_transition()`, `src/game.c`), and save/load
(`saveload_apply_pending()`, `src/saveload_runtime.c`). A **fourth call
site did not**: `platform_player_step()` (`src/platform.c`), reached directly
from every arrow-key press via `game_player_step()` (`src/game.c`) with no
bracket at all - not even a screen blank. This is the path exercised by
*ordinary walking off the edge of a room*, almost certainly the single most
common way a player changes rooms in actual play (far more common than
scripted trigger transitions or save/load), so this is very likely the
dominant source of the reported glitching.

**Fix**: `platform_player_step()` no longer calls `platform_room_enter()`
directly for an edge crossing. It queues the same
`game_transition_request()`/`game_process_pending_transition()` deferred
path a room script's `room_transition` already uses, so the actual switch
happens one frame later through the already-correct, already-bracketed code
path instead of a fourth hand-rolled bracket. This collapses the number of
places the room-switch contract has to be manually honored from four down to
three, rather than just patching the fourth. Net effect: an edge-crossing step
now takes one extra frame (~20ms, imperceptible) to load the destination room
and run its `enter_room()`/`enter_tile()`; `game_state.current_room` does not
update until that following frame. `game_state.turn` (used only for the save
record, not read by any gameplay logic today - see `modules/saveload_save.c`)
now increments on the frame the step is *accepted* rather than the frame the
destination room's arrival tile is confirmed solid; a step into a room whose
arrival tile turns out blocked therefore still consumes a turn where it
previously didn't. Deliberately accepted - fixing it would require querying
transition-success before requesting it, for a counter nothing currently
reads.

See `src/platform.h`'s `platform_player_step()` comment,
`PLATFORM_API.md`'s "Game loop..." and "Screen transitions" sections, and the
commit for the actual diff.

### Audit: every other EasyFlash-copy-into-IRQ-reachable-memory call site

The specific hazard above needs two things at once: (1) a checksum (or other)
verification step that runs with interrupts already back on, and (2) a
destination the raster IRQ itself reads or writes. Swept every caller of
`platform_resource_fetch()`/`_fetch_range()`, `platform_overlay_load()`,
`platform_easyflash_copy_roml()`/`_romh()`, and the room-code/object-type
loaders for that combination:

| Call site | Destination | IRQ-reachable? | Verdict |
|---|---|---|---|
| `game_enter_room()`, `platform_resource_fetch(..._ENVIRONMENT...)` | `ENVCODE_BASE` (`$7E00-$7FFF`) | Yes - `weather_animate` jumps into `ENVCODE_TICK` every frame | The one real hazard - now always called from inside a bracket (see above) |
| `platform_init()`'s three boot `platform_resource_fetch(..._ASSET...)` calls (charsets, tiles) | `charset_tile`/`charset_text`/`tile_data` | N/A | Run before `raster_irq_install()` - no IRQ exists yet to race |
| `modules/script.c`'s `platform_resource_fetch_range()` (windowed script reader) | private resident script buffer | No | Not IRQ-touched; also has no checksum step at all (documented: range fetches don't verify checksums) |
| `platform_overlay_load()` (script, saveload, saveload-save overlays) | `$B000` overlay window / room-code `$9900` | No | None of these addresses are read or written by `src/irq.s` |
| Room/object-type EasyFlash loaders (`platform_room_load`, `platform_object_types_load`, `room_code_prepare_easyflash`) | `platform_room`, object-type tables, `$B000` staging | No | Same - not IRQ-touched |
| `platform_object_type_info_get()` (in-place `FAR_CALL`) | `platform_object_type_info_scratch` (`LOWBSS`) | No | Not IRQ-touched; also human-input-paced only per its own doc comment |
| Inventory UI (in-place `FAR_CALL`) | screen/color RAM and `$C174-$C178`; reads `GameState` | No | IRQ stays active; no dependency on `$8000-$BFFF` RAM |
| Room Helpers (in-place `FAR_CALL`) | room at `$2000`, ABI at `$1F40-$1F46`, rendered state below `$3000` | No | IRQ stays active; all `$B000` work and mapping-changing hot-type access remain outside the far call |
| Look Helpers (in-place `FAR_CALL`) | ABI below `$2000`, current room/visibility below `$3000`, temporary workspace at `$2400` | No | IRQ stays active; brightness is staged before entry and hot-type access restores the caller's exact map |

Conclusion: `ENVCODE_BASE` was the only instance of this hazard class, and it
is now closed for every path that reaches it. No other latent instance found.
This does not mean no other bug exists - see the two items below, which are
real but different in kind (not this checksum race).

### Remaining banked-code risks

Not bugs with a known trigger today, but the most concrete
architecture-level risks this audit surfaced, worth fixing before they bite
rather than after:

1. **The linked half of the banked-memory contract is now enforced; raw
   pointers still require discipline.** `tools/validate_banked_module.py`
   rejects writable/private segments and resident imports outside
   `$0000-$7FFF` or `$C000-$CFFF`. That catches a future banked C module which
   accidentally imports `platform_room`, room code, WORKBSS, or resident BSS.
   It cannot prove the run-time provenance of an arbitrary pointer argument:
   a resident caller could still hand banked code a pointer into
   `$8000-$BFFF`, which would silently read cartridge bytes. Keep banked APIs
   value-oriented or copy their inputs to always-visible resident storage;
   add pointer-range assertions in a debug build when an API must accept a
   pointer. IRQ execution while the cartridge is mapped is intentional now,
   not itself an error: the complete IRQ closure is below `$8000`.
2. **`platform_object_type_get()`'s shared scratch buffer is a two-pointers-
   at-once trap.** For type IDs 106-255 it returns a pointer into one shared
   `platform_object_type_scratch` record (types 0-105 return a direct pointer
   into the resident table instead, which is why this hasn't fired yet).
   Holding two such pointers at once - e.g. future code comparing two
   objects' hot fields, both IDs >= 106 - would have the second fetch
   silently overwrite the first's data underneath the caller. Audited every
   current call site (`src/platform.c`, `src/game_support.c`, and LH through
   collision); none currently hold two pointers at once. RH's wrapper consumes
   the light value before entering cartridge code and never exports the
   scratch pointer.
   **Recommended fix**: either a second scratch slot, or a debug assertion
   that traps a second fetch while a live pointer from the first is still
   plausibly in scope, before any code needs to compare two objects' types.
3. **The validator cannot infer self-modifying code.** It enforces segment and
   import placement, but a local absolute store can still target an instruction
   in the same ROM module without appearing in the resolver. Inventory had
   exactly this: its drawer patched the operand of `STA $FFFF`. Writes beneath
   cartridge ROM update RAM, so the patched operand was never fetched. It now
   uses a zero-page indirect destination, and “no self-modifying code” is an
   explicit banked-module ABI rule. Assembly review (or a future relocation/
   disassembly lint) remains necessary for this class.

## Overlay-load visual glitch (2026-09-07, same session)

Reported live: looking at a description-bearing object (room 00's milestone,
`assets/scripts/00.script`) showed "Looking..." flash as garbled tile-charset
glyphs, then the correct description appeared, then a keypress caused an
apparently unnecessary full-screen redraw, with a suspicion that a further
page of text was shown and immediately erased. All three turned out to be
real, and distinct from the interrupt-bracket bug above - this one is a
**previously undocumented cost of the copy-to-RAM overlay design itself**,
plus one straightforward pacing bug layered on top of it.

### Root cause 1, fixed: overlay copies masked the raster IRQ for multiple frames

`easyflash_copy_window` is roughly 50 cycles per byte, so the larger overlays
take several video frames to copy. The original bank wrapper held `SEI` for
that complete interval. The split, frame counter, rain, and environment tick
all stopped, leaving status text rendered through whichever charset happened
to be selected.

The raster IRQ's complete per-frame code and state now live below `$8000`.
Bank wrappers mask interrupts only while changing `$01` and the EasyFlash
registers, then restore the caller's interrupt state for the copy or far call.
VICE verification (before RH's later in-place conversion) showed the frame
counter advance during its 507-byte copy while CPU port `$01` was `$37` and
ROML/ROMH were selected. The existing blank
brackets around large overlay loads remain as conservative loading presentation,
but are no longer required to hide a frozen charset split.

### Root cause 2: the final page of room-script text was wiped before it could be read

`run_loaded_overlay()` (`src/script_runtime.c`) - the shared cleanup for
`game_script_play()`, `game_conversation_play()`, and
`game_room_script_entry()` - cleared both status text rows and redrew the
room immediately after `platform_overlay_run_native()` returned, with no
pause. `src/text.s`'s pager already waits for a fresh keypress *between*
pages of a multi-line message (`next_text_line()`'s `wait_for_fresh_key()`),
but nothing waited *after* the last page, so the one page a player actually
had a chance to still be reading was the one instantly erased - visible, per
the report, as "more text shown for a split second, vanishing when the map
redraw happens," with the keypress that advanced past the *first* page
appearing to be what triggered the redraw.

**Fix applied**: a resident flag, `script_text_shown`, set by
`modules/script.c`'s `say()` (the `OP_TEXT` handler) whenever it writes
room/standalone-script text, checked by `run_loaded_overlay()` before its
clear-and-redraw - if set, it calls `game_wait_fresh_key()` (exported from
`src/game.c`, previously `static` and used only by
`game_transition_reveal()`) first. Deliberately **not** set for a
conversation's own topic text (`say()` checks
`script_resource_kind != PLATFORM_RESOURCE_KIND_CONVERSATION`): a
conversation already paces itself (each loop iteration's "Ask about..."
prompt only overwrites the previous answer once the player has typed
something, and exiting via RUN/STOP is itself the acknowledgment - an
unconditional extra wait here would demand a second, redundant keypress
right after that exit). Also skipped when `game_transition_pending_message`
is set: that path already gets its own wait, later, in
`game_transition_reveal()` - waiting twice would demand two keypresses for
one message.

Verified via clean `make`/`make cartridge` rebuilds (adds one BSS
byte, `script_text_shown`, into the 19 bytes `MEMORY_MAP.md` records as free
there - now 18 remaining). Not verified via a live interactive playtest in
this session - see [[project-vice-headless-keywait-limitation]] on why a
keypress-driven repro isn't reliable to automate headlessly here; recommend
confirming interactively that the milestone's full 3-line description is now
readable start to finish with no flash and no premature wipe.

## Transition hang: `platform_wait_frame()` cannot advance while the raster IRQ is suspended (2026-09-07, same session)

Reported live: after the milestone/mystery sequence's three lightning
flashes, the screen went black and the game never transitioned to room 01 -
permanently stuck, not a glitch. Root cause found by tracing the exact call
chain rather than by live reproduction (see "Live VICE debugging attempts"
below for why): a genuine, deterministic infinite hang, not a rendering bug.

**The chain:** `game_process_pending_transition()` (`src/game.c`) wraps the
*entire* room switch - `platform_room_enter()` plus the `game_enter_room()`/
`game_enter_tile()` sync that must follow it with no gap - in one
`raster_irq_suspend()`/`raster_irq_resume()` bracket (by design; see
`platform_room_enter()`'s contract in `src/platform.h` and the "Interrupt/
banking safety audit" section above for why that gap is unsafe).
`raster_irq_suspend()` (`src/irq.s`) masks `VIC_IRQ_ENABLE` for the bracket's
whole duration, so **no raster IRQ fires at all** while it's held - which
means `_platform_frame_counter` (only ever incremented from inside that IRQ)
is frozen for exactly as long. `game_enter_room()`, called from inside that
bracket, calls the destination room's `enter_room()` handler. Room 01's
(`rooms/asm/01_enter_room.s`) shows a one-time, three-line arrival narration
via `game_room_script_entry()` the first time it's entered. Showing that text
runs the bottom pager (`src/text.s`), whose `next_text_line()` calls
`wait_for_fresh_key()` between pages of output longer than two lines -
which calls `platform_wait_frame()` in a loop, waiting for
`_platform_frame_counter` to change. **It never does**, because the IRQ that
would change it cannot fire. The loop spins forever. The game is not glitched
- it is deterministically hung, every time this exact path runs, on real
hardware or in any emulator.

This bug predates this session's other work: `rooms/asm/01_enter_room.s` and
its three-line arrival text were added in the original "mysterious inn"
commit, and this is the game's first-ever scripted room-to-room transition -
nothing suggests it was ever actually played through to this exact point in
VICE before now. This session's `run_loaded_overlay()` change (the "Overlay-
load visual glitch" section above) made the same hang additionally reachable
through *any* room/standalone-script text shown during a suspended
transition, not just text long enough to need the pager's own internal
pagination - but the underlying vulnerability, and this specific repro,
already existed independent of that change.

**Fix**: `platform_wait_frame()` (`src/platform.c`) now checks
`platform_raster_irq_active` (newly given a C-visible `_`-prefixed alias in
`src/irq.s`, pointing at the same `LOWBSS` byte `src/irq.s`'s own code
already reads/writes under its unprefixed name - zero added bytes). When the
raster IRQ is suspended, instead of polling the frozen frame counter, it
polls the VIC's own raster position directly (wait for the 9-bit raster to
reach the bottom of the frame, then wrap to the top) - real hardware timing
that keeps advancing regardless of whether the VIC is allowed to request an
interrupt, the same "one frame has passed" signal
`_platform_bank_call_enter` (`src/banking.s`) already trusts for a related
purpose. This is a single, central fix: it also protects `game_wait_fresh_key()`
(`src/game.c`) and `modules/script.c`'s `wait_fresh_key()` (the `OP_WAIT_KEY`
opcode) - every "wait for a keypress" primitive in the codebase is built on
`platform_wait_frame()` - rather than requiring each call site to know not to
run during a suspended transition.

**Known remaining issue, not fixed**: this stops the hang, but the arrival
text is still shown while `raster_irq_suspend()` holds the display on a
single, statically-chosen charset for its whole duration (tile, in normal
gameplay - see `_raster_irq_suspend`'s own logic in `src/irq.s`) rather than
the split that normally puts the *text* charset under the status rows. The
narration will render as garbled tile glyphs, readable-but-wrong, exactly
like the "Overlay-load visual glitch" section's Look/Take/Use symptom, until
the player's keypress lets the transition finish and the display resync. The
hang is what made the game unplayable past this point; this remaining
glitch is real but far less severe, and worth its own fix later - most
likely by moving `enter_room()`/`enter_tile()` narration onto the
purpose-built `game_transition_show_message()`/`game_transition_pending_message()`
mechanism (`src/game.c`), which already exists precisely to show text before/
during a transition without this hazard, rather than calling
`game_room_script_entry()` directly from a handler that always runs inside
the suspended window.

### Live VICE debugging: solved (2026-09-07, later the same day)

The blocker above was tooling unfamiliarity, not a real limitation - fixed
once `headless-c64-debugging-on-ubuntu.md` (repo root) was found and
followed. Two mistakes in the earlier attempt: using `go` (not a real
monitor command - the actual resume command is `x`, "leave the monitor and
resume execution") and not knowing that **a fresh connection always halts
the CPU on its own**, so there's no need for a pre-armed breakpoint to get
back in - just reconnect.

**Working recipe**, confirmed end to end:

```bash
Xvfb :99 -screen 0 1024x768x24 &   # or xvfb-run -a, either works
DISPLAY=:99 x64sc -remotemonitor -remotemonitoraddress ip4://127.0.0.1:6510 \
  -sounddev dummy -cartcrt build/game.crt -8 build/saves.d64 &

printf 'r\n' | nc -q 2 127.0.0.1 6510                     # connect = halt; shows registers
printf 'keybuf "\\x11"\nx\n' | nc -q 2 127.0.0.1 6510      # queue a cursor-down, then resume
printf '> c100 13\nx\n' | nc -q 2 127.0.0.1 6510           # poke a byte (game_state.turn low byte), resume
printf 'm c108 c10b\nx\n' | nc -q 2 127.0.0.1 6510         # dump 4 bytes, resume
printf 'screenshot "/path/out.png" 2\nx\n' | nc -q 2 127.0.0.1 6510
```

Each `nc -q N` call is a fresh connection: it halts the CPU, runs every
command in the pipe in order, and `x` at the end resumes before the process
exits. This is a genuine synchronous stop/inspect/resume cycle - no
breakpoint needed for the common case of "let it run a bit, then look."

**One thing worth correcting in the guide itself**: it warns `keybuf` "does
not faithfully simulate arbitrary key press/release timing" and may not
work for a game that scans the CIA keyboard matrix directly. This game does
exactly that (`platform_input_poll()` in `src/input.s` calls KERNAL `SCNKEY`
before `GETIN`) - and `keybuf` **worked perfectly** for it anyway, including
multi-key queued sequences consumed one per game frame and reliably
advancing `wait_for_fresh_key()`-style multi-press waits. The likely reason:
`GETIN` only ever dequeues from the software keyboard buffer regardless of
how it was filled, and `SCNKEY` (run first every frame) finds nothing on the
real, keyboardless virtual machine's matrix and leaves the queue alone - so
for a game whose *only* interaction with the matrix is calling `SCNKEY` next
to `GETIN` (not reading `$DC00`/`$DC01` port state directly), `keybuf` is
just as good as real key events. The distinction that matters is "does the
game read `$DC00`/`$DC01` directly" (would need real key events / `xdotool`),
not "does it call `SCNKEY`."

**This directly resolves [[project-vice-headless-keywait-limitation]]'s open
question**: that memory's original "environmental limitation" test used raw
`$0277`/`$C6` pokes under `-moncommands`, a much more fragile technique than
`keybuf` over `-remotemonitor` - it was very likely hitting tooling friction
(or the same `platform_wait_frame()`-during-suspension hang class now fixed
below), not a fundamental inability to drive this shape of wait loop
headlessly. Re-test any future "stuck keypress wait" against this recipe
before concluding it's environmental again.

**Used to live-verify both fixes in this file**, not just reason through
them: navigated to the milestone via queued `keybuf` cursor moves, looked at
it and confirmed the full 3-line description displays cleanly (no
tile-charset garbling) and the final page stays up until dismissed (screens
`look3_page1.png` / `look4_page2_immediate.png` / `look5_page2_later.png`,
1.5s apart, identical) - the "Overlay-load visual glitch" fix, confirmed
live. Then poked `game_state.turn`/`flags[2]` directly to skip to the
mystery-sequence trigger condition, stepped onto a qualifying tile, and
watched screenshots through lightning, the (expected, not-yet-fixed) garbled
arrival narration, and a clean final redraw of room 01 (`current_room` byte
at `$C108` read back as `01`, actual inn/rain artwork visible, status row
clean) - the "Transition hang" fix, confirmed live: it does not hang, it
reaches room 01, and the one predicted residual glitch (narration shown via
the wrong charset while `raster_irq_suspend()` holds it) is present exactly
as documented and no worse.

## Color RAM corruption: `redraw_dirty()`/`platform_lighting_rebuild()` are not safe to call from an overlay (2026-09-08)

Reported live, with a screenshot: after looking at an empty tile in room 01,
the whole visible map turned into scattered rainbow-colored static - shapes
intact (tiles, trees, walls still recognizable), only the colors wrong. Not
a rendering-timing glitch like the two sections above - this one is
straightforward wrong-data corruption, and the most severe of the three
found this week: the write half of it overwrites *live, not-yet-executed*
code, not just a data array, which makes the exact visible symptom
unpredictable rather than a specific fixed glitch.

**Root cause.** `platform_base_colors` and `platform_brightness`
(`src/platform.c`) are deliberately placed in `WORKBSS` to save resident RAM
(`MEMORY_MAP_TARGET.md`'s own earlier note: "already rebuilt rather than
persistent" - a known, accepted trade-off pending the future VIC-bank-move
work). `WORKBSS` occupies the head of `$B000-$BFFF` - the same physical memory
the copied overlays use. At the time this bug was found, RH was one of those
overlays. Two call chains
touch these arrays **from inside an overlay's own execution**, not from
resident code:

- `look_helpers_tile_check()` (`modules/look_helpers.c`, the "too dark"/
  visibility check behind Look) reads `platform_brightness[offset]` directly.
  While the `LOOK_HELPERS` overlay is loaded, that read returns the
  overlay's own compiled bytes instead of real brightness data - a silently
  wrong "too dark" judgment, not itself a screen-paint.
- `room_helpers_object_remove()` (`modules/room_helpers.c`, Take's actual
  removal) called `redraw_dirty()` and, when the removed object emitted
  light, `platform_lighting_rebuild()` (both `src/platform.c`) - **from
  inside the `ROOM_HELPERS` overlay**. `redraw_dirty()` both reads
  `platform_brightness` (garbage, same as above) *and writes*
  `platform_base_colors[offset] = color` for every dirty cell.
  `platform_lighting_rebuild()` starts with
  `memset(platform_brightness, ..., sizeof(platform_brightness))` - a
  220-byte fill. Both writes land in `WORKBSS`, i.e. **inside the
  `ROOM_HELPERS` overlay's own currently-loaded code** - overwriting
  instructions the overlay has not executed yet with color/brightness
  values. This is not "wrong data gets read" but "the running program
  overwrites itself out from under itself" - the specific visible result
  (scattered rainbow corruption across the whole map, per the screenshot)
  is one plausible outcome of executing color-index bytes as 6502
  instructions after that; a crash or hang would have been an equally
  plausible outcome from the same root cause, just not the one that
  happened to reproduce.

This is a generalization of a hazard already flagged as a standing risk in
this file's "Interrupt/banking safety audit" section (`platform_lightning()`
reading `platform_base_colors`/`platform_brightness` while the `SC` overlay
is loaded) - that instance was reasoned to be self-correcting (the next full
`platform_room_draw()` rebuilds the cache before anything reads it again) and
therefore lower priority. This instance is not self-correcting: Take's
darkness-check/removal path is the *last* thing that runs before control
returns to the player, so nothing rebuilds the cache afterward, and the
corruption is a direct write into the calling overlay's own code, not just a
read of stale data.

**Fix.** Neither call needs to happen from inside the overlay - both follow
the "resident wrapper finishes the job after the overlay returns" pattern
already used elsewhere in this codebase (room-code activation, script/room-
script text):

- `look_helpers_tile_check()` no longer reads `platform_brightness` itself.
  `platform_look_tile_check()` (`src/platform.c`) now reads
  `platform_brightness[offset]` resident-side, *before* loading the overlay,
  and passes it through a new staging global, `look_helpers_light`.
- `room_helpers_object_remove()` no longer calls `redraw_dirty()`/
  `platform_lighting_rebuild()`. The first fix left dirty marking inside the
  copied overlay and moved only the WORKBSS operations after its return. RH's
  later in-place conversion tightened that boundary further: light lookup,
  `dirty_clear()`, and `mark_object_cells()` now all run resident-side before
  the far call; the ROML service only mutates the room object and rendered
  limit; redraw/lighting still run resident-side after it returns.

Moving `platform_room_object_remove()` into `UPPERCODE` (it and
`platform_look_tile_check()`'s prefetch both grew `HIGHCODE` past its
margin - see `platform_room_object_remove()`'s own comment) was needed to
make room; verified via clean `make`/`make cartridge` (HIGH now
has ~156 bytes free, UPPER ~76).

**Live-verified**: the original fix reproduced a clean, uncorrupted room 01.
The later RH conversion adds a deterministic removal-path check: VICE writes
a valid type-106 hot record into RAM beneath I/O, places that takeable object
on the player tile, drives `T` + Enter through the normal UI, and breaks at
RH's `$84C0` entry. On PAL and NTSC the object is present on entry and zeroed
on return, inventory gains type 106, `$01` changes from the service's `$37`
back to gameplay `$35`, EasyFlash changes from bank 47/control `$06` back to
bank 0/control `$04`, and the frame counter continues advancing.

**Not audited this session**: whether any *other* code reads or writes
`platform_base_colors`/`platform_brightness`/other `WORKBSS` members from
inside an overlay's own execution - only the two call chains a live report
pointed at were checked. `platform_lightning()`'s pre-existing case (audited
earlier, reasoned self-correcting) is the only other known instance; a
deliberate grep-and-check sweep of every overlay for other `WORKBSS`
reads/writes, mirroring the sweep already done for the checksum-race hazard,
would be worth doing before extending any overlay's responsibilities
further.

## Removing the unnecessary "after text" redraw (2026-09-08, same day)

> Historical note: the repair described in this section was correct while SC
> was copied over WORKBSS. SC now executes in place, so it no longer corrupts
> those caches; `platform_lighting_repair()` and its cleanup call were removed.

Asked directly: why does dismissing a Look/room-script description blank the
screen before *and* after, and could the "after" one just go away, since the
map is still intact and doesn't need redrawing? Correct instinct, but the
obvious fix (delete the `platform_room_draw()` call in `run_loaded_overlay()`
- `src/script_runtime.c`) would have silently reintroduced the same
corruption class fixed a section above: that call's real job, underneath the
visible redraw, is repairing `platform_base_colors`/`platform_brightness`
(`WORKBSS`), which the overlay that just ran corrupted by occupying that
exact memory. Skip it entirely and the corruption doesn't disappear - it
just becomes silent and waits for the next unrelated redraw to surface it,
exactly the failure mode this session already root-caused once.

**First attempt was wrong, caught by live testing.** Replaced the call with
a bare `platform_lighting_rebuild(&platform_room, platform_player)`,
reasoning that its final pass (`platform_lighting_apply()`) writes Color RAM
directly with no clear-first step, so it would be invisible. Built clean,
but a live screenshot (`noblank1.png`) showed scattered rainbow corruption
across the whole map - worse than before. Root cause of *that*:
`platform_lighting_rebuild()` only ever recomputes `platform_brightness`
(via `light_source_apply()`/`wall_cache_apply()`, confirmed by reading both
- neither touches `platform_base_colors`). `platform_base_colors` is
populated only by `platform_map_draw_native()`/`object_draw_base()`
(confirmed in `src/render.s`: both write character codes to screen RAM *and*
color indices to `platform_base_colors`, never to Color RAM directly). Since
those two calls were skipped, the final lighting-apply pass combined
freshly-correct brightness with still-garbage base colors - a strictly
worse bug than the one being fixed, since it's now unconditional on every
Look instead of only after Take.

**Working fix**: `platform_room_draw()`'s body was split into a shared
`redraw_map_and_objects()` (screen-RAM redraw plus, as its real purpose,
`platform_base_colors` repopulation - both native calls, confirmed to touch
only screen RAM and that cache, never Color RAM) and the rest
(`platform_look_cursor_hide()`, the two visible clears, `view_rebuild()`,
`platform_lighting_rebuild()`). A new public function,
`platform_lighting_repair()` (`src/platform.c`), calls
`redraw_map_and_objects()` followed by `platform_lighting_rebuild()` -
skipping the visible clears (never needed - nothing to hide) and
`view_rebuild()` (`platform_view_tiles` is plain resident `BSS`, untouched
by the overlay, so already correct). Because neither step this keeps ever
writes Color RAM until `platform_lighting_apply()`'s own final pass, there
is no intermediate wrong-color state for the VIC to scan out - the first
paint is already the correct one. `run_loaded_overlay()` now calls
`platform_lighting_repair()` instead of `platform_room_draw()`; `platform_
room_draw()` itself is behaviorally unchanged (same steps, now via the
shared helper).

Live-verified both paths after the fix: dismissing the milestone's
description now shows the room with no flash at all (`noblank2.png`,
correct colors, no leftover corruption), and a full room transition through
`platform_room_draw()`'s unchanged path still renders room 01 correctly
(`room01_full.png`) - confirming the refactor didn't change its behavior.
Verified via clean `make`/`make cartridge` throughout (`HIGH` and
`UPPER` both still have margin - see `build/game.map` after any future
change here).

## Two charset bugs around the room 00→01 transition (2026-09-08, same day)

Reported live with two screenshots: (1) during the transition, the arrival
narration displays at the bottom, but renders as unreadable tile-charset
garbage instead of text; (2) once the room has switched in, the *map itself*
gets permanently stuck showing through the text charset instead of tile -
i.e. the two charsets were swapped from what each moment actually needed.
Both are charset-split bugs, but with different root causes and different
fixes; solving one did not solve the other.

### Bug 1: the map got permanently stuck on the text charset

This one took real live debugging to pin down - two wrong turns are worth
recording so a future session doesn't repeat them.

**First hypothesis (right idea, wrong completeness):** `$D011` bit 7 means
different things on read vs. write - on read it's the current raster line's
bit 8; on write it's bit 8 of the raster-IRQ *compare* value `src/irq.s`
schedules through `$D012`. `platform_screen_blank()`/`_unblank()`
(`src/platform.c`) did a plain read-modify-write of `$D011` for the DEN bit,
which copies whatever bit 7 *read* as straight back out as the *compare*
value's high bit - safe ~82% of the time (raster below 256), but the other
~18% (called while the raster happens to be in the 256-311 range) it
latches compare-bit-8=1, silently rescheduling every future "top of frame"
trigger from raster==0 to raster==256. `raster_irq_install()` and
`_raster_irq_resync()` (`src/irq.s`) already knew to force bit 7 back to 0
on every `$D011` write for exactly this reason; these two functions didn't.
Fixed by forcing bit 7 to 0 unconditionally on every write
(`platform_screen_blank()`: `&= 0x6f`; `_unblank()`:
`(x & 0x7f) | 0x10`) instead of preserving whatever was read.

This fix is real and correct, but built and tested clean **before** it was
confirmed to be the actual cause of the persistent-lock symptom being
chased. Live tracing (see below) showed the raster IRQ was *already*
oscillating correctly - bit 7 was never actually stuck. Rather than an
active bug in this exact repro, the read-modify-write hazard is a *latent*
one this fix closes pre-emptively, confirmed safe to ship (harmless,
strictly more correct, matches the invariant the rest of the IRQ machinery
already relies on) - it just wasn't the mechanism behind what was actually
observed.

**Debugging detour worth recording**: naive `-remotemonitor` memory peeks
at `$D011`/`$D012` are not reliable evidence for this class of bug, because
*reading* `$D012`/`$D011` always returns the **live raster position**, never
the **write-only compare value** actually driving the IRQ - there is no way
to observe the compare register's stored contents directly. Several rounds
of "sample, resume, sample again" produced what looked like a stuck
text-charset pattern, but this was very likely a sampling artifact (network/
monitor round-trip timing correlating with frame phase, plus a run of bad
luck across a small sample), not evidence of anything actually wrong. A
`break <raster_irq_body_addr>` + single-step trace of several *consecutive*
real IRQ entries - reading `$D011`/`$D012` right at entry, before the
handler's own branch runs - is the technique that actually settled it: it
showed clean alternation (raster 0 → sets tile, raster 226 → sets text,
repeatably), and a final full-screenshot check (not a register peek)
confirmed the rendered map was correct throughout. Trust a screenshot over a
register-peek trend when they disagree - the peek technique has a specific,
easy-to-miss failure mode here.

### Bug 2: the arrival narration itself renders unreadable

Separate root cause, separate fix. `game_process_pending_transition()`
(`src/game.c`) holds `raster_irq_suspend()` across the *entire* room switch,
which pins the display on one statically-chosen charset (tile, in normal
gameplay - see `_raster_irq_suspend`'s own logic) for that whole duration,
because nothing was expected to need the split *during* a transition before
now. Room 01's `enter_room()` shows its one-time arrival text from exactly
inside that window (via `game_room_script_entry()` →
`run_banked_script()`, `src/script_runtime.c`) - so the text renders
through the wrong (tile) charset the entire time it's up, readable-but-wrong
becoming actually-unreadable. This was already flagged as a known, lower-
severity follow-up in the "Transition hang" section above; asked to fix it
properly this time.

**Fix**: the script runtime checks `platform_raster_irq_active` and calls
`raster_irq_resume()` if it finds the IRQ still suspended - before running
the interpreter (so the pager's own text
output, and this function's own `game_wait_fresh_key()` dismiss-wait, both
render/operate with a working split) - and deliberately does **not**
re-suspend afterward. This is safe specifically because, by the time any
room-entry/tile-entry hook can run at all, `platform_room_enter()`'s own
EasyFlash work and the environment module's checksum-race-sensitive fetch
(`game_enter_room()`, `src/game.c` - the hazard the whole-transition
suspend bracket exists to prevent in the first place) are both already
fully complete - confirmed by reading `game_enter_room()`'s actual sequence
(environment fetch, then `env_init()`, then the room hook that can reach the
script runtime - never the other order). Not re-suspending is
deliberate, not an oversight: `game_process_pending_transition()`'s own
later `raster_irq_resume()` call simply becomes a harmless no-op once this
has already run, and the screen stays correctly split for as long as text
might still be up (including the pager's between-pages waits, which happen
inside the banked interpreter, already covered).

Live-verified both fixes together in one repro: the arrival narration's two
pages both rendered as clean readable text (screenshots showing "Suddenly a
mysterious inn appears..." and "...or a cursed mirage?" correctly, map
background still the old room as expected/harmless since it hasn't been
redrawn yet), and the final room 01 view rendered correctly with no
corruption. Verified via clean `make`/`make cartridge` throughout.

## The rule everything is constrained by

While a far call runs, `$8000-$9FFF` is cartridge ROML. For a 16 KiB call,
`$A000-$BFFF` is cartridge ROMH; for an 8 KiB call it is BASIC ROM instead.
Either mapping hides the underlying upper RAM, so a banked routine may read
only `$0000-$7FFF` and `$C000-$CFFF`, and may only call resident code living
outside the hidden areas. Writes still reach the RAM underneath; only reads
are affected.

This distinction is easy to get wrong because the EasyFlash control value and
the 6510 CPU-port value use similar numbers. `$DE02=$06` selects 8 KiB
EasyFlash mode, but ROML is visible only with LORAM and HIRAM asserted, i.e.
`$01=$37`. `$01=$36` removes BASIC *and ROML*, exposing underlying RAM at both
`$8000-$9FFF` and `$A000-$BFFF`; execution at a ROML address then immediately
runs RAM garbage. Consequently CPU map and cartridge control are separate
fields in the far-call descriptor and generated layout.

`$C000-$CFFF` is the only RAM above `$8000` that no memory map ever covers,
which is why the stack and `GameState` went there and why it is now nearly
full (stack 256 + `GameState` 116 + Inventory state 5 + object-type zone A
3,710 of 4,096, leaving 9 bytes split across two tails).

## Cost model

Both bank-switch paths mask interrupts only for the short map/register
transition. The raster IRQ remains active during the banked operation. Each
call still pays fixed transition overhead, so **bank whole operations, not
inner-loop helpers**; a long banked operation also still blocks foreground
gameplay even though display timing continues.

## Layout bridge completed

The blocked relocation is now implemented as one atomic map change:

- VIC bank 3 owns `$E800-$FDFF` (charsets, screen, pointers, sprites).
- `PlatformRoom`, room staging, `WORLDDELTA`, BSS, and compact helpers moved
  into the freed `$2000-$2FFF` space.
- the native/SID/text module moved to `$3A00-$3BBC`;
- renderer `WORKBSS` and bounded staging share `$B000-$B7FF`;
- the resident save-disk driver occupies `$B800-$BA0E` and cannot be touched
  by staging;
- the generic copied-overlay window and validator no longer exist.

An intermediate bank-2 design put the screen at `$8000` and charsets at
`$A000`. It linked and the underlying RAM tested correctly, but Inventory's
16 KiB ROMH mapping changed what the VIC itself fetched at `$A000`, producing
garbled text. The final bank-3 design avoids the cartridge window entirely.
The VIC still reads RAM beneath KERNAL, while IRQ water animation reads its
source from an eight-byte low-RAM shadow so CPU-side KERNAL mapping is safe.

PAL VICE has exercised Inventory in bank 48 ROMH, Type Info's nested 8 KiB
far call, in-place `RH`/`SC`/`LH`, the relocated text pager, and an actual
named save/readback/load round trip through in-place SL/SV. NTSC load also
passes. The tests inspect `$01`, `$DD00`, `$D018`, `$DE00/$DE02`, service and
disk-gate entry maps, screen/charset/tile bytes, frame progress, and saved
state rather than relying only on screenshots.

## Next-candidate dependency audit

The independent linker resolvers were classified against the effective ROML
map, then checked for transitive mapping and workspace effects. A low imported
address is necessary but not sufficient: it may still change `$01`, call an
upper routine, or follow a pointer into hidden RAM.

| Module | Direct closure | Blocking transitive/data dependency | Rank |
|---|---|---|---|
| `RH` | All imports below `$8000`; no BSS/initialized writable data | Hot type IDs 106-222 originally made `platform_object_type_get()` restore `$35`; RH kept lookup/dirty marking in its resident wrapper | Converted |
| `LH` | All resolver imports are visible; no module-owned writable segment | A 595-byte workspace (former BSS plus collision-hit cache) borrows room staging; hot-type access restores the exact caller map instead of `$35` | Converted; PAL/NTSC high-type and nested-call tests pass |
| `SL` | Direct imports are visible or terminate at explicit KERNAL gates | Record moved from hidden `$A000` to temporary tile RAM; decoded list uses `$0400`; direct map changes removed | Converted; PAL/NTSC load tests pass |
| `SC` | Direct imports are now visible; six action imports terminate at explicit RAM-call gates | The resource window was already `$2400`, not `$B000`; 10 bytes of mutable metadata moved to ROOMSTAGE's tail. RAM gates cover upper-code and WORKBSS closures, including nested portrait fetches | Converted; PAL/NTSC text/action/IRQ tests pass |
| `SV` | Direct imports are visible or terminate at explicit gameplay/KERNAL gates | Record moved to `$3000`; raw index uses `$0400`; disk driver split resident; tile asset restored by resident wrapper | Converted; named PAL save/readback/load passes |

## Redesign outcome

The planned service conversions are complete. Inventory and Type Info show
nested bank calls; RH moves mapping-sensitive side effects to its resident
wrapper; LH demonstrates exact-map restoration and temporary workspace; SC
demonstrates gameplay host-action gates; SL/SV demonstrate separate gameplay
and KERNAL host gates plus resident cleanup of aliased data.

Future work should preserve these rules:

1. Bank coarse, value-oriented services, never inner-loop helpers.
2. Treat the complete C closure as part of the banked ABI: software stack,
   globals, literals, runtime helpers, nested callees, and IRQ-visible state.
3. Any routine that changes `$01` or `$DE02` must restore the exact caller
   state, not a presumed gameplay state.
4. Keep `$B800-$BA0E` outside every staging bound and restore borrowed
   `$3000` tile data before drawing.
5. Validate PAL and NTSC with structured monitor state, including at least one
   interrupt during long banked work and exact bank/map restoration.

## Cartridge-only runtime

The obsolete game D64 build/run path has been removed. Runtime resources,
room code, and banked services are EasyFlash-only; disk unit 8 is reserved for
the save image created and attached by `make run`/`make run-cartridge`.

## Dead ends, recorded so they are not retried

**Overlay window to `$E400` (under-KERNAL RAM).** Links cleanly, boot works,
room code and object types still stage correctly through it - but overlay
validation then rejects every overlay at the checksum step, so none run.
Confirmed a regression against the previous commit. The payload lands
byte-identical in RAM and the file's checksum is self-consistent; only the
checksum loop's reads disagree. Suspected cause: the ROMH copy path maps the
KERNAL over the destination (`CPU_MAP_CART_16K`, HIRAM=1) while copying into
it, so writes pass through but reads there return ROM. Do not retry without
first proving CPU reads at the new base under every map the load path passes
through. Kept as git stash "overlay window -> E400 WIP".

**Marking the vacated charset areas `file = ""`.** A PRG is a load address
followed by contiguous bytes, so removing bytes from the middle makes
everything after load at the wrong address; `tools/validate_prg_layout.py`
catches it. The areas must keep reserving their place until the charsets
physically leave low RAM, which is what the VIC move does. This is why the
asset split is byte-neutral until step 3.

## Bottom-pager scroll read KERNAL ROM instead of the screen (2026-09-11)

Found after the VIC-bank move (bank 3, screen matrix at `$F800`): the second
and later pages of any bottom-pager message longer than two lines displayed
correctly on write, but the line carried over from the *previous* page (the
one `next_text_line`'s scroll copies up, not the one freshly written)
rendered as garbled bytes - reproducibly, every time, not intermittently.
Live memory dump at the moment of corruption showed the "scrolled" row
literally contained 6502 machine code (`STA $01`, `AND #$08`, `JSR $FBB1`,
...) - unmistakably KERNAL ROM bytes, not screen RAM.

**Root cause**: `TEXT_ROW_0`/`TEXT_ROW_1` (`src/text.s`, `SCREEN_RAM + 23/24
* 40`) now sit at `$FB98`/`$FBC0` - inside `$E000-$FFFF`, the KERNAL ROM
shadow. Writes there always reach the underlying RAM regardless of the CPU
port (true 6510 behavior), which is why `write_character()` needed no
change and every *freshly written* line, including the first page, always
displayed correctly. But `next_text_line`'s scroll-up loop is the one place
in this module that *reads* the screen (`LDA TEXT_ROW_1,x`, to move the
previous line up before writing the next) - and a read of `$E000-$FFFF`
returns KERNAL ROM whenever HIRAM is set, not the RAM the VIC is actually
displaying. Confirmed live that gameplay's ambient `$01` value has HIRAM
set (`$35`/`$37` depending on the moment sampled - both have HIRAM=1),
unlike the pre-VIC-move layout where the screen lived at a plain low
address unaffected by CPU bank state. `CHARSETS`/`VICDISPLAY`'s own comment
in `cfg/myc64.cfg` already flags this general hazard ("CPU reads of this
RAM disappear while KERNAL is mapped") and says the mitigation is "banked
UI code only writes the screen" plus a LOWBSS shadow for the one IRQ read
that needs one (rain/water) - the pager's scroll was the one remaining read
site nobody had converted.

**Fix**: `text_scroll` now saves `$01`, switches to `CPU_MAP_GAME`
(LORAM=1, HIRAM=0, CHAREN=1 - RAM at `$E000-$FFFF` *and* Color RAM's I/O
window still visible, since the same loop also copies `COLOR_ROW_1`→
`COLOR_ROW_0`) for just the copy loop, then restores the exact saved value
- the same save/restore discipline used elsewhere in this codebase (e.g.
`platform_input_poll()`), not an assumption about what the ambient mapping
already is. Needed a matching bump to `cfg/text_module.cfg`'s `TEXT` region
size (330 → 368 bytes) to fit the extra bytes; confirmed safe against the
reserved `TEXTGAP` window in `cfg/myc64.cfg` (512 bytes total, 445 used
before this change), so no other region needed to move.

Live-verified: a memory dump of both status rows after scrolling now reads
back as the exact correct wrapped text (`"arrow and the words: 20 miles to
nowhere"` / `"in particular."`), matching the screenshot-based confirmation.
Verified via clean `make cartridge` (`tools/validate_easyflash_layout.py`
still passes, 7 fixed module placements validated).

Did a quick sweep for other screen-RAM reads while diagnosing this one.
Found exactly one more: `redraw_dirty()` (`src/platform.c`, the per-step
dirty-cell repaint used by ordinary movement) does
`if (P_SCREEN_RAM[offset] != ch) P_SCREEN_RAM[offset] = ch;` - a read-
compare-write, same hazard class. Left alone deliberately: unlike the
pager's scroll, this read's only job is skipping a redundant write when the
byte is already correct. A wrong read (KERNAL ROM, which will essentially
never coincidentally equal an intended tile/text character code) just makes
the comparison come out "different" and the write happens anyway - which
was always going to be the correct byte regardless, since writes reach the
underlying RAM correctly. Net effect: the optimization is quietly defeated
(marginally more writes than strictly necessary) with no visible
corruption, unlike the pager's scroll which actually *uses* the read
result. Not fixed here since there's nothing user-visible to fix; worth
tidying up if this area gets touched again, but not urgent. The same
`P_COLOR_RAM[offset] != visible_color` check right next to it is unaffected
- Color RAM is a real I/O register, visible under both ambient mappings
observed (`$35`/`$37`), not inside the KERNAL shadow.

## The in-place redesign dropped an unblank it didn't know it depended on (2026-09-11, same day)

Reported live: triggering a room transition shows lightning (correctly),
then the screen goes solid black and stays black - several blind keypresses
are needed before the new room finally appears, and no transition text is
ever visible.

**Root cause**: `game_process_pending_transition()`'s whole-switch bracket
(`src/game.c`) calls `game_transition_message_show()` - which blanks the
screen whenever no transition message is pending, the ordinary case - then
does not unblank again until `game_transition_reveal()` at the very end of
that same bracket. Room 01's `enter_room()` shows its one-time arrival
narration from *inside* that blanked window (via `game_room_script_entry()`
→ `run_banked_script()`, `src/script_runtime.c`). Before the in-place
redesign, `run_loaded_overlay()` (this function's previous name) unblanked
the screen around its own EasyFlash copy of the script overlay - purely to
hide *that copy's* charset-split glitch (see "Overlay-load visual glitch"
above) - and that unblank was, incidentally, also the only thing that ever
made a mid-transition narration visible at all. The redesign correctly
removed the now-unnecessary copy (the interpreter runs in place; there is
nothing to copy, so nothing to hide), but nothing replaced the unblank side
effect it happened to also be providing. The narration's own pacing (the
pager's between-pages waits, `game_wait_fresh_key()` at the end - both
already fixed once this session to correctly resume the raster IRQ so the
charset split works, see the "Two charset bugs" section above) still ran
exactly as before and still consumed real keypresses; they just did so with
`DEN` off the entire time, so nothing was ever visible to read.

This is the second bug in a row where a redesign correctly removed
now-dead code but didn't notice a second, unrelated job that same code
happened to be doing. Worth remembering as a general lesson for reviewing
any future removal of a "this copy step also blanks/unblanks the screen"
or similar dual-purpose bracket.

**Fix**: `run_banked_script()` now unblanks the screen at the same point it
already resumes the raster IRQ (both gated on the same "was the caller
still inside a suspended transition when we got here" check), and
deliberately does not re-blank afterward - `game_transition_reveal()`'s own
blank-draw-unblank cycle, later, already blanks unconditionally right
before it draws the new room, regardless of what state this leaves the
screen in.

Live-verified: lightning visible (unchanged, runs before the transition is
queued), then both pages of the arrival narration displaying as clean,
readable text with the old room's background still visible behind them (the
map hasn't been redrawn yet - expected, harmless), then a clean final view
of the new room. Verified via clean `make cartridge`
(`tools/validate_easyflash_layout.py` still passes).

## Rain sound didn't resume after leaving the tavern (2026-09-11)

Reported live: leaving the inn (room 02) back to room 01 correctly stops
the tavern music, but the rain ambient sound never comes back, even though
nothing about entering room 01 looked wrong on screen (rain sprites/visual
still animate normally - only the sound is missing).

Both `rooms/env/00.s` and `rooms/env/01.s`'s `sound_init` fully clear
`$D400`-`$D418` and then reprogram voice 1 as a continuous gated noise
generator (the rain bed) and voice 2 as an intermittently-gated noise
generator (droplet/footstep hits) - `V1_CTRL = $81`, `V2_CTRL = $80`
initially. Live register reads (`m d400 d41f` over a VICE remote-monitor
session, taken repeatedly across two full room 01 → room 02 → room 01
round trips) showed every SID register holding *exactly* the values
`sound_init` writes, and the rain-bed voice's gate bit set the entire time
- by every readable measure the rain should have been audible. A second
check on the module's own private workspace (`raintimer` at
`ENVCODE_BASE+12`) confirmed `env_tick` was actively running (the byte was
visibly counting down and reseeding across repeated reads) - so the tick
vector wasn't frozen either. Every observable, software-visible piece of
state said "this should be making noise."

**Root cause**: a well-known SID hardware quirk that is invisible to any
register read. A voice's noise-waveform output is generated by a 23-bit
LFSR that only *clocks* while that voice's control register has the noise
waveform bit selected; while a voice is playing some other waveform, its
LFSR sits frozen holding whatever value it last reached. If that value
happens to be exactly zero, reselecting the noise waveform later does not
restart it - an all-zero LFSR feeds zero back into itself forever, so the
voice stays silent indefinitely no matter what the gate/volume/frequency
registers say. Room 02's tavern melody (`rooms/env/02.s`) uses voice 1 for
a triangle-wave note and voice 2 for a sawtooth-wave note for as long as
the player stays inside - exactly the two voices room 00/01's rain reuses
for noise. Spend enough time in the tavern and either voice's noise LFSR
can end up parked at zero, so `sound_init`'s plain `LDA #$81 / STA
V1_CTRL` (and the equivalent for voice 2) reselects noise but never
un-sticks it - permanently silent from that point on, with a fully
"correct-looking" register dump the entire time. This also explains why
static analysis alone couldn't find it: nothing in the C or 6502 source
is wrong in the sense of writing a bad value anywhere; the bug is a
hardware property of the SID's internal (non-addressable) shift register
that no register read can reveal.

**Fix**: before the real `STA V1_CTRL`/`STA V2_CTRL` writes, `sound_init`
now pulses each voice's TEST bit first (`LDA #$88 / STA V1_CTRL` - noise
waveform selected with TEST held, gate off - immediately followed by the
real `LDA #$81 / STA V1_CTRL`, and the analogous `$88`→`$80` pair for
voice 2). Holding TEST forces the LFSR to load a fixed non-zero seed;
clearing it while noise stays selected guarantees the shift register is
clocking from a known-good state before the voice is ever gated on for
real. This is the standard, widely-used fix for this exact SID quirk (seen
in countless C64 sound drivers' drum/noise-hit routines) - applied here at
room-entry time in both `rooms/env/00.s` and `rooms/env/01.s`, the only two
modules that put a voice into noise mode after some other room's module
may have left it on a different waveform.

Verified the fix assembles into the built cartridge as intended by reading
the environment module's raw bytes at `ENVCODE_BASE` ($7E00) over the VICE
remote monitor after `make cartridge`: the `sound_init` routine contains
`A9 88 8D 04 D4` (`LDA #$88 / STA $D404`) immediately followed by `A9 81 8D
04 D4` (`LDA #$81 / STA $D404`), and the matching `$88`/`$80` pair at
`$D40B` for voice 2. Because the whole point of the fix is to affect
internal, non-addressable SID state, a live register dump after the fix
looks *identical* to one taken before it (both show the final $81/$80
values) - the register-level check can only confirm the extra instructions
ran and left the final state correct, not that the audible bug is gone;
that part rests on the SID's own well-documented behavior rather than on
anything observable in this emulator session (`-sounddev dummy`, no audio
output).
