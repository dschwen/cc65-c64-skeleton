# oscar64 migration — feasibility assessment

## Context

The game currently builds with cc65 (cc65/ca65/ld65 + `cl65`) into an EasyFlash
cartridge. Over many sessions the project has grown a hand-built banking
architecture: 13 `cfg/*.cfg` linker configs, 8 independently-linked units at
hand-chosen fixed addresses, a `FAR_CALL` self-modifying trampoline, and ~2,500
lines of Python that parse `ld65` map files, `od65` object dumps and VICE `.lbl`
files to stitch the units together. `MEMORY_MAP.md` records margins of 18, 40,
182 and 246 bytes in the tightest regions — the layout is full.

You asked for an evaluation before any migration: what breaks, what it costs,
what it buys. **This document is the deliverable. No source, build or config
file is to be changed until you've read it and said go.**

Motivation you selected: better codegen/speed, escaping the cc65 ABI, native
EasyFlash support, and a richer C/C++ dialect — all four.

---

## Verdict

Feasible, and the architectural fit is better than it first looks — but this is
a **rewrite of the build system plus a re-authoring of ~6,900 lines of
assembly**, not a compiler swap. Nothing can be shared or staged between the
two toolchains: oscar64 has no external assembler and no object linker, so
there is no hybrid mode and no incremental cutover. The game builds with cc65
or it builds with oscar64.

The strongest argument in favour is not codegen — it is that oscar64's memory
model deletes the exact constraint that forced most of this project's
complexity. See "What it buys".

---

## What breaks

### 1. The entire build system (hard blocker, must be rebuilt)

oscar64 is a whole-program compiler: **one invocation compiles and links
everything**; there are no object files, no link steps, no linker config
language. Consequences:

- All 13 `cfg/*.cfg` files disappear. Memory layout moves into source pragmas:
  `#pragma region(name, start, end, flags, bank, {sections}, runtimeStart)` and
  `#pragma section(...)`, with `#pragma code(sec)` / `data` / `bss` switching
  emission per function. Verified in `samples/memmap/easyflash.c`.
- The 8 separate link units (IV/SL/SV/RH/LH/SC/BT + resident) collapse into one
  compile. Their fixed start addresses (`$84C0`, `$8800`, `$9300`, `$9F80`, …)
  become linker-chosen placements inside bank regions.
- `tools/generate_room_resolver.py`, `tools/finalize_room_code.py`,
  `tools/validate_prg_layout.py`, `tools/validate_banked_module.py` and the
  `od65 --dump-imports` machinery all exist solely to bridge cc65 link units.
  They become dead code — the problem they solve stops existing.
- `cart/ef_boot.s` + the `cartconv` step go away: oscar64 emits the `.crt`
  directly (`-tf=crt` is specifically EasyFlash) and its own startup expands the
  bank-0 common image into RAM, which is what `ef_boot.s` does by hand today.
- `tools/pack_easyflash.py` (463 lines) loses its module-packing half. Its
  resource half (rooms, portraits, scripts, directories in banks 3–63) is better
  re-expressed as `#embed` arrays placed into bank sections — cleaner, but a
  rewrite.
- The four validators' regexes are `ld65`-map-specific. oscar64 emits its own
  `.map`, `.asm`, `.lbl` (VICE format) and `.dbj`, so the checks are
  reproducible, but every parser must be rewritten.

### 2. All 6,900 lines of ca65 assembly (the largest single cost)

oscar64 cannot assemble or link a `.s` file — confirmed against the docs
("No external libraries") and the compiler's source-file handling. Every line
must move into `__asm { }` blocks inside C files, be rewritten in C, or be
deleted.

- `.byte` tables: oscar64's `byt` directive takes **one value per line** (see
  `include/crt.c`). Long tables should become C `const` arrays instead.
- No `.macro`. `SET_MAP`, `BANK_CALL`, `FAR_CALL` must be rebuilt from the
  `#assign`/`#repeat`/`#for` preprocessor or from C inline functions.
- No `.segment` — placement comes from `#pragma code(section)`.
- **oscar64's asm optimizer rewrites inline assembly at `-O2`+.** This project
  is full of self-modifying code (`render.s` patches operands, `FAR_CALL`
  patches bank+target). Those blocks need `__asm volatile` or
  `#pragma optimize(noasm)`, and getting this wrong produces subtle,
  intermittent failures rather than build errors. This is the single biggest
  correctness risk in the migration.
- Good news: struct-offset literals baked into asm (`_game_state+84`,
  `_platform_room+233`, `_object_types_a+69`) can use oscar64's
  `Type::Member` syntax and stop being fragile.

Where the asm sits today:

| Group | Lines | Difficulty |
|---|---|---|
| `src/render.s`, `irq.s`, `text.s`, `banking.s`, small leaves | ~2,650 | Mechanical — no cc65 ABI, touches only globals/hardware |
| `rooms/env/*.s` (6 files) | 1,258 | Mechanical — self-contained |
| `src/dirty_redraw.s` | 761 | Hard — reads cdecl args via `(sp),y`, uses `ptr1`, `incsp2` |
| `src/brazier.s`, `world_slots.s`, `room_code.s`, `banked_api.s` | 509 | Hard — cc65 fastcall/`sreg`/`pushax` assumptions |
| `rooms/asm/*_look_at.s`, `*_use_at.s` | ~550 | Hard — every one reads `(sp),1` |
| `modules/inventory_draw.s`, `disk_io.s` | 503 | Hard — `ptr1`/`ptr2`/`regsave` |

Nine cc65 runtime symbols are imported across these files — `sp`, `ptr1`,
`ptr2`, `sreg`, `regsave`, `pusha`, `pushax`, `incsp1`, `incsp2` — and none has
an oscar64 equivalent, because oscar64 passes parameters in statically-allocated
zeropage frames rather than on a software stack.

An important judgement call, not a mechanical one: **much of this assembly
exists to beat cc65's codegen.** `dirty_redraw.s` was written to save ~1 KB over
the C version. If oscar64's C output is close to the hand asm, the right move is
to delete these files and go back to C, not translate them. That decision needs
measurement (see Phase 0).

### 3. C-side changes (moderate, mostly mechanical)

- **106 `#pragma code-name`/`bss-name`/`rodata-name` occurrences** across
  `src/*.c` — every one maps to an oscar64 `#pragma code(...)`/`bss(...)`, but
  the target section names all change because the region scheme changes.
- **~30 `__fastcall__` declarations** — oscar64 has no calling-convention
  keywords at all; it picks per function via whole-program analysis. These get
  deleted, and every asm routine that relied on "argument arrives in A/X"
  changes with them.
- Zeropage C variables (`src/platform.c:147-158`, `.importzp`'d by `render.s`)
  move to `__zeropage` / `#pragma zeropage(var)`.
- Genuinely easy: there are **no** `__asm__()` statements, no cc65 headers, no
  `charmap`, no bitfields, no packing pragmas. Only `<stdint.h>`, `<string.h>`,
  `memcpy`/`memset`. The C is unusually portable.
- `tools/compile_script.py` emits interpreted **bytecode**, not 6502 — entirely
  unaffected. The `.script` sources and `modules/script.c`'s interpreter carry
  over as-is.

### 4. The memory map must be re-derived from scratch

`MEMORY_MAP.md` is byte-exact and non-portable. oscar64's static frame allocator
places each function's locals itself (visible as `main@stack` etc. in its
`.map`), the zeropage layout is fixed by the compiler (0x0d–0x1a leaf params,
0x1b–0x1e ACCU, 0x23–0x24 stack pointer, 0x43–0x8f saved regs), and there is no
cc65 software stack to place. VIC bank 3, the `$F800` screen, `$E800`/`$F000`
charsets and the sprite block are all expressible as regions/sections, but the
numbers change and every build-time layout check must be re-established.

### 5. Second-order risks

- **Function pointers are penalized** — the docs recommend `switch` over them,
  and they can force oscar64's dynamic stack. This project dispatches room hooks
  and env modules through tables and stores `rain_setup_hook`. Cross-bank calls
  through a pointer also need manual banking. Needs design work, not a rewrite.
- **Build times**: every change recompiles the whole game, including all banks
  and all embedded resources. Today's per-module `make` incrementality is lost.
- **Save-game compatibility**: struct layouts may shift; existing `.d64` saves
  would likely need to be discarded.
- **64-bank / 16 KB window limit** — the current layout uses banks up to 63
  already, so there's no headroom gained here.
- `cfg/disk_boot.cfg` and `disk/boot.s` appear to be already-dead code (no make
  rule references them); worth confirming before carrying them across.

---

## What it buys

1. **Banked C stops being a research problem.** The reason SL/SV/RH/LH/SC needed
   map-derived "dependency closure" analysis is that cc65 puts the software
   stack, runtime helpers and temporaries inside `$8000-$BFFF` — the very window
   the cartridge covers. That fight is recorded across `MEMORY_MAP.md`,
   `MEMORY_MAP_TARGET.md` and the banked-memory notes. oscar64 allocates all
   frames statically into whichever region you nominate, so a banked function's
   locals simply live below `$8000` by construction. The whole class of bug
   disappears.
2. **~1,500–2,000 lines of build machinery get deleted**: 13 linker configs,
   the `.lbl` resolver generator, the overlay header finalizer, two layout
   validators, the `od65` import/export parser, `ef_boot.s`, the `cartconv`
   step, and the module half of the packer.
3. **Native cross-bank calls.** `EF_CALL(fn)` (C++ mode) wraps a bank function so
   it is called with ordinary syntax, switching to `__bankof(fn)` and restoring
   the caller's bank on return — a typed, compiler-checked `FAR_CALL`, including
   the nested case. `__bankof` also removes the hand-maintained bank/address
   tables in `src/banked_api.s`.
4. **Code density.** oscar64 is generally substantially better than cc65 here.
   With margins of 18–246 bytes in four regions, any win converts directly into
   headroom — and possibly into deleting hand-written asm. **Unmeasured in this
   repo; Phase 0 exists to measure it rather than assume it.**
5. **Resources become declarative**: `#embed` arrays in bank sections replace
   hand-computed bank/offset packing and its directory format.
6. **The room DSL gets simpler.** `tools/compile_room.py` currently generates
   ca65 with hardcoded `jsr pusha` / `lda (sp),y` sequences and a literal
   `GAME_STATE_FLAGS_OFFSET = 84`. Retargeting it to emit **C** instead removes
   the generator's ABI knowledge entirely, and room code becomes ordinary banked
   functions — no `$9900` overlay, no resolver, no 24-byte header, no checksum.
7. C++ subset (templates, namespaces, references) for game logic, if wanted.

---

## Phase 0 results (spike completed 2026-09-18)

Ran the recommended measurement spike as a throwaway scratch build (not committed):
standalone, semantically-equivalent C reimplementations of two of `src/render.s`'s
routines — `_platform_map_draw_native` (tile → screen/color RAM blit, the
20×11-tile draw loop) and `_platform_lighting_apply_native` (per-tile
view/brightness → Color RAM pass) — same data-layout sizes, same algorithm,
compiled three ways and measured by linking each into a minimal program and
reading the exact address range of the resulting function from the linker's
own map/label output (not estimated).

| Routine | Hand ca65 asm (current) | cc65 `-Oirs` C | oscar64 `-O2` C |
|---|---|---|---|
| Tile/color draw loop | 380 B | 482 B | **197 B** |
| Lighting apply pass | 269 B | 507 B | **169 B** |

Both oscar64 builds were checked against their `.asm` listing to confirm they
are genuine, correct double loops with real branches and table lookups (not
inlining/dead-code elimination artifacts) — e.g. the lighting routine still
branches per-tile on `view_tiles[]` and does four `light_colors[]` lookups per
lit tile, matching the hand asm's logic exactly. All `-O1`/`-O2`/`-O3`/`-Os`
levels produced the *identical* 197 B / 169 B output for these two routines —
no further tuning available or needed at this code shape.

**oscar64 plain C beats this project's hand-optimized cc65-ABI assembly by
~1.9–2× on code size, using ordinary portable C with no `__fastcall__`,
pragmas, or inline asm.** This is a materially stronger and clearer result
than "Phase 0 exists to measure" assumed either direction might show — it
directly supports deleting rather than translating the ~2,000 "hard" lines of
asm identified in the inventory above (`dirty_redraw.s`, `brazier.s`,
`world_slots.s`, the `rooms/asm/*_look_at.s`/`*_use_at.s` family), since their
entire reason to exist was beating cc65's C codegen.

**Not obtained:** a live cycle-count comparison. The attempt to bracket one
routine call with VICE remote-monitor breakpoints and read the `STOPWATCH`
cycle counter (`r` command; confirmed the field exists and works for a single
snapshot) repeatedly hung across reconnects/resumes in this sandbox — likely a
text-monitor/`nc` framing issue rather than anything oscar64- or
project-specific, since the same friction would hit any cc65-vs-oscar64
comparison. Given the code-size gap is this large and this consistent (no
early-out branches removed, same instruction-mix character — indexed loads
into computed zero-page pointers vs. the hand asm's self-modified operands —
in both), cycle counts are very likely to favor oscar64 too, but that's an
inference, not a measurement. If a precise number is needed before committing
further, redo this with VICE's **binary monitor** protocol (`-binarymonitor`)
instead of the text monitor — `headless-c64-debugging-on-ubuntu.md` already
flags it as "the cleaner long-term interface," and it avoids the line-framing
assumptions that caused the hangs here.

**Revised recommendation:** proceed to Phase 1 (skeleton EasyFlash cartridge
boot under oscar64) when you're ready. The size data closes out the question
Phase 0 was scoped to answer; a cycle-accurate pass is worth doing but need not
block starting Phase 1, since Phase 1 doesn't depend on it.

---

## Cost

Rough shape of the work, assuming the game must end up at feature parity:

| Phase | Work | Size |
|---|---|---|
| 0 | Measurement spike: compile the draw/lighting core as plain C under oscar64, compare size/cycles vs cc65 C **and** vs the hand asm | small (~1 session) |
| 1 | Skeleton cartridge: regions for the target map, `-tf=crt` boot, raster IRQ under cart mapping, VIC bank 3, EasyFlash bank read | small–medium |
| 2 | Resources: `#embed` bank sections replacing the packer; keep script bytecode as-is | medium |
| 3 | Resident C port: `platform.c` (76 KB), `game.c`, `main.c`, `world.c`, runtimes — pragma/ABI rewrite | large |
| 4 | Assembly: translate, rewrite in C, or delete — decided per file by Phase 0's numbers | largest |
| 5 | Banked services (IV/SL/SV/RH/LH/SC) as bank sections + `EF_CALL` | medium |
| 6 | Rooms: retarget `compile_room.py` to emit C; delete overlay machinery | medium |
| 7 | Rebuild validators against oscar64's `.map`/`.lbl`; VICE regression pass | medium |

Overall this is comparable to, or larger than, the banked-memory rework the
project just completed — with the difference that the game does not boot at all
until Phases 1–4 are substantially done. There is no working intermediate state.

---

## Recommendation

Proceed only via **Phase 0 first**, as a throwaway spike in a scratch directory,
because it de-risks the single largest cost (Phase 4). Concretely:

- Take the tile-draw + lighting inner loop, build it three ways — oscar64 C
  (`-O2`/`-O3`), cc65 C, and the existing `render.s`/`dirty_redraw.s` asm — and
  compare bytes emitted and cycles per frame.
- If oscar64 C lands near the hand asm, Phase 4 becomes "delete ~2,000 lines"
  and the migration is clearly worth it.
- If it does not, Phase 4 is "hand-translate 6,900 lines with an asm optimizer
  that may rewrite them", and the case is much weaker.

A secondary decision to make before Phase 1: **C++ mode or not.** `EF_CALL` is
C++-only, and it is the closest analogue to the current `FAR_CALL`; choosing
plain C means hand-writing an `fcall(bank, fn)` trampoline as in
`samples/memmap/easyflash.c`.

## Verification

For Phase 0, the spike is self-verifying: `oscar64 -n -O2 -tf=prg` on the
extracted loop, read the emitted `.asm`/`.map` for size, and cycle-count in
VICE (`xvfb-run -a x64sc`, per `AGENTS.md`). Nothing in the repo is touched.

For any real migration, the existing regression discipline still applies and
should be ported before the code is: the VICE state checks listed in
`MEMORY_MAP.md` (`$01`, `$DD00`, `$D018`, `$DE00/$DE02`, frame counter, restored
tile bytes at `$3000`) are toolchain-independent and remain the acceptance test.

## Next step if you approve this document

Write it into the repo as `OSCAR64_MIGRATION.md` (committed, so it survives this
session) and stop. No source, Makefile or config changes; Phase 0 only on an
explicit further go-ahead.
