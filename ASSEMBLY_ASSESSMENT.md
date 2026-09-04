# Assembly-space assessment

How much resident code space could be recovered by hand-translating more of
this project's C into 6502 assembly? This is a measured answer, not a guess -
the methodology, pilot translations, and their exact byte counts are recorded
below so the numbers can be checked and the exercise repeated on other
functions.

**Bottom line: targeted rewrite of specific pointer/array-heavy functions,
not a full-project rewrite.** Two real pilot translations (831 C bytes and
600 C bytes respectively) came back at 28.4% and 45.0% smaller in hand-asm.
That is real, worth having if you need the space, but it is not the kind of
uniform 2-3x win that would justify rewriting all ~140 remaining C functions
in `platform.c`. See "Recommendation" below.

## Why this needed measuring, not estimating

This codebase already moved every genuinely hot loop (map/object drawing,
lighting propagation, visibility building, EasyFlash bank pokes) into
hand-written `src/render.s`/`src/banking.s`, reached through thin C shims in
`src/platform.c`. That's the cheap, obvious win, and it's already banked.
What's left in C is orchestration and bookkeeping - room loading, resource
lookups, object-array management, look/take/use dispatch - code whose
compiled size depends heavily on its *shape* (pointer-chasing vs. tight
loops vs. branchy control flow), which cc65 handles with very different
efficiency depending on which shape it is. `LIGHTING.md`, this project's one
prior documented optimization, measured cycles, never bytes - there was no
existing size data point to extrapolate from.

## Baseline (commit `d5af968e`, 2026-09-03)

From `build/game.map`'s Modules list (CODE-type segments only: CODE, LOWCODE,
MIDCODE, UPPERCODE, HIGHCODE):

| | Bytes |
|---|---:|
| Total resident CODE | 24,957 |
| Project source files | 23,895 |
| cc65 runtime library (48 modules) | 1,062 (4.26%) |
| `platform.c` alone | 14,689 (61.5% of project total) |

The 4.26% library figure is a **floor, not the real overhead** - it's only
the shared library routine *bodies* (`pushax.o` is 26 bytes total, linked
once). Every call site's argument-staging code (the `lda`/`ldx` before a
`jsr pushax`, etc.) is baked into the *caller's* own segment and isn't
visible in that figure at all.

## Methodology

1. Picked three C "shapes" from `platform.c`'s remaining (non-native) code,
   deliberately not from an already-native hot loop (that would bias toward
   "huge savings," which isn't the open question):
   - **Arithmetic/pointer-out-param**: `resource_directory_lookup` +
     `platform_resource_fetch` + `platform_resource_fetch_range`
     (`src/platform.c:333-426`).
   - **Loop-over-struct-array**: `platform_room_object_count` +
     `platform_room_object_add` (+ `platform_room_object_transfer`, not
     translated - see caveat) (`src/platform.c:1298-1441`).
   - **Branchy orchestration**: `platform_room_enter`
     (`src/platform.c:1348-1414`) - analyzed but not hand-translated, see
     below.
2. Measured each pilot's exact current C size via `cc65 -t c64 -Oirs --cpu
   6502 -Isrc -o platform.s src/platform.c` (the real Makefile `CFLAGS`)
   followed by `ca65 --cpu 6502 -l platform.lst -o platform.o platform.s`,
   reading each function's `.proc`/`.endproc` byte span from the listing.
3. Hand-translated the first two pilots to `.s`, preserving each function's
   *public* fastcall ABI exactly (verified against the real compiled
   listing's stack-offset layout, so no caller would need to change) -
   except `resource_directory_lookup`, which is `static` in the original C
   (never called outside `platform.c`), so its four-pointer-out-param
   signature is an internal implementation detail, not an external
   contract. The hand-asm version gives it this codebase's own established
   convention for tightly-coupled internal handoffs - global staging
   variables, the same pattern already used everywhere in `src/banking.s`
   (`platform_ef_copy_bank`/`_offset`/`_destination`/`_size`) - instead of
   reproducing cc65's stack-passed-pointers choice. This is not redesigning
   the logic (every field, check, and return value is identical); it's the
   ordinary judgment call a competent 6502 programmer makes for a
   same-file-only helper, and is exactly the kind of difference this
   assessment exists to surface.
4. Assembled each translation (`ca65`) and read its real segment size via
   `od65 --dump-segments`.
5. All translations and intermediate files are throwaway measurement
   scratch (not committed, not wired into the real build) - see the
   verification section for where to find them if you want to re-check.

## Pilot 1: arithmetic/pointer-out-param (resource directory)

| Function | C bytes |
|---|---:|
| `resource_directory_lookup` | 261 |
| `platform_resource_fetch` | 317 |
| `platform_resource_fetch_range` | 253 |
| **Total** | **831** |

Hand-asm: **595 bytes CODE**, plus **24 bytes of new BSS** (the global
staging variables replacing the C version's stack-passed out-parameters).

**Result: 831 -> 595 = 236 bytes saved, 28.4% reduction.**

One concrete technique this pilot surfaced: `kind` (a
`PLATFORM_RESOURCE_KIND_*` value) only ever takes 0-2, so `kind *
RESOURCE_DIRECTORY_BYTES` (2048) doesn't need a general 16-bit multiply -
`kind*2048`'s low byte is always 0 and its high byte is exactly `kind*8`
(2048 = 8*256), so three inline `ASL` instructions on a known-small byte
replace cc65's generic `jsr shlax3` call plus its own internal carry-handling
body. cc65 has no way to know `kind`'s actual runtime range and must emit
the fully general case.

**Caveat**: the 24 new BSS bytes matter more than they might look. This
project's `BSS` segment has **zero bytes free** (see `MEMORY_MAP.md`,
re-verified this session) - a rewrite along these lines isn't free CODE
savings, it would need those 24 bytes found somewhere else first (e.g.
reusing an existing scratch buffer the way `room_stage`/`ROOMSTAGE`'s slack
is already reused elsewhere in this codebase, rather than declaring new
storage).

## Pilot 2: loop-over-struct-array (room object management)

| Function | C bytes |
|---|---:|
| `platform_room_object_count` | 204 |
| `platform_room_object_add` | 396 |
| `platform_room_object_transfer` | 397 (not translated) |
| **Measured total** | **600** |

Hand-asm (2 of 3 functions): **330 bytes CODE**, plus **9 bytes of new
BSS**.

**Result (measured functions only): 600 -> 330 = 270 bytes saved, 45.0%
reduction.**

This shape showed the strongest overhead of the three: cc65 recomputes
`room->objects[i]`'s address from scratch on every loop iteration via a
`jsr mulax3` (index * 3, since `PlatformObject` is a 3-byte `{type,x,y}`
record) followed by `jsr tosaddax` (base + offset) - a pair repeated 3-4
times per function in the compiled listing. A running pointer, advanced by
`+3` once per iteration via inline `ADC`/`BCC`, replaces all of that. The
256-iteration loop itself also uses the "8-bit index that naturally wraps
255->0" idiom this codebase already established for the same reason
elsewhere (`take_object_find` in `src/game_support.c`), instead of cc65's
16-bit loop-counter compare.

**Caveat**: `platform_room_object_transfer` (397 C bytes) was not
hand-translated, given the effort already spent on two functions sharing
its exact shape (it calls `platform_room_object_add` and does a similar
struct-field zeroing plus a `rendered_object_limit` trim loop). A similar
~40-45% reduction is plausible by extension but **not measured** - treat
the 600/330 pair as the solid number, not the full 997-byte pilot the plan
originally scoped.

## Pilot 3: branchy orchestration (`platform_room_enter`) - not translated

`platform_room_enter` (589 C bytes) was analyzed but not hand-translated,
given time already spent on the two full pilots above. Call-density
analysis from the same compiled listing: 21 "plumbing" `jsr`/`jmp`
instructions (`pushax`, `decsp7`, `mulax3` x2, `pusha0`, `tosumula0`,
`jmpvec`, `incsp2` x2, `addysp`, ...) versus 12 calls to other named
functions (`_raster_irq_suspend`, `_room_stage_load`,
`_platform_room_draw` x4, `_room_commit`, etc.) - a real plumbing signal,
including two `mulax3` calls (likely `room_stage.tiles[]` indexing), but
proportionally fewer than the object-array pilot and a much higher share of
calls that are genuine necessary function calls (not overhead a hand-asm
rewrite would eliminate - those calls stay calls either way). This suggests
a savings ratio somewhere between the two measured pilots, but **this is
informed speculation, not a measurement** - do not treat it as a third data
point with the same confidence as the two translated pilots.

## Extrapolation - read the caveat before the number

Applying the two measured ratios (28.4% low, 45.0% high) to `platform.c`'s
14,689 CODE bytes (nearly all of which is genuinely still-C orchestration -
the native rendering/lighting/banking code these ratios deliberately
excluded lives in `render.o`/`banking.o`, counted separately, not inside
`platform.o`) gives a range of roughly **4,170-6,610 bytes** of theoretical
maximum savings if the *entire* file behaved like the two pilots.

**This number should not be trusted at face value.** The two pilots
together cover 1,431 of `platform.c`'s 14,689 bytes - about 9.7% of the
file - and were deliberately chosen from the two shapes most likely to
compress well (pointer/array-heavy code). The third shape (branchy
orchestration, probably the single largest category by function count in
the remaining ~140 functions) was not measured and its call-density profile
suggests a *lower* ratio. A realistic estimate, weighting for that, is
meaningfully below the naive 4.2-6.6 KB range - plausibly in the 2-4 KB
region if extended across the whole file, but this is not itself a
measurement either. Do not extrapolate to other files (`game_support.o`
1,962 bytes, `main.o` 1,310, `room_runtime.o` 1,006, etc.) at all - they
were not sampled and may have entirely different shape mixes.

## Recommendation

**Targeted rewrite, not a full-project rewrite.** The two real pilots land
in a "meaningful but not enormous" band (28-45%), concentrated specifically
in pointer-out-param and fixed-stride-array-loop code - exactly the classes
cc65's codegen is weakest at (no strength-reduction of index*constant into
pointer increments, no range-narrowing of small-domain multiplies). That is
a real, identifiable category of functions worth hand-rewriting on its own
merits, independent of any broader initiative:

- **Good targeted candidates** (same shape as the measured pilots, likely
  similar ratios): `platform_room_object_transfer` (measured shape, just not
  translated), and any other function iterating `room->objects[]` or a
  similar fixed-stride array with per-iteration multiplication.
- **Not recommended without their own pilot measurement**: the branchy
  orchestration functions (`platform_room_enter` and similar) - the
  call-density signal suggests a real but smaller win, and most of their
  bytes are necessary function-call machinery that doesn't shrink much
  under hand-asm regardless of language.
- **Not recommended at all**: a full-project or full-`platform.c` rewrite.
  The already-completed native-shim pattern (hot loops in `render.s`/
  `banking.s`, orchestration in C) already captures the largest available
  win; converting the remaining ~140 functions would be a large, ongoing
  maintenance-cost increase (losing C's readability/type-safety for this
  many functions) for a return that, per this measurement, is real but
  moderate - not the kind of finding that changes the cost/benefit
  calculus for the whole file.

If resident space becomes a hard blocker for a specific new feature (the
sound-routine/weather-pattern conversation that prompted this assessment),
the first move should be identifying which *specific* functions in the
critical path resemble the measured pilots' shape, pilot-measuring those
specifically, and rewriting only those - not a blanket conversion.
