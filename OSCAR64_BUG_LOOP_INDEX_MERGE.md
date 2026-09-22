# Oscar64 bug: two per-iteration array writes merge into one induction variable

Found 2026-09-22 investigating a report that the Look/Take cursor sprite's
square outline had pixels missing down its left edge.

**Status:** worked around in this repo; **not reported upstream**, and
**not reduced to a standalone repro** - three attempts at increasingly
close approximations of the real function (see "Repro attempts" below) all
compiled correctly. The bug is confirmed only by disassembling the real
build and reading emulator memory live, not by an isolated test case.
Compiler checkout: `~/Programs/oscar64` HEAD `f38a1f2` (2026-09-06),
matching [OSCAR64_BUG_POINTER_LOOP_ADVANCE.md](OSCAR64_BUG_POINTER_LOOP_ADVANCE.md).
I did not verify the installed binary was built from exactly that commit.

## Symptom

`platform_look_cursor_show()` (`src/platform.c`) builds the Look/Take/Use
cursor's 24x21 sprite bitmap: a solid top/bottom bar (written directly,
outside any loop) plus a one-pixel-wide left and right vertical edge for
the 16 rows in between, originally:

```c
for (row = 1u; row < 17u; ++row) {
    SPRITE_DATA[(uint8_t)(row * 3u)] = 0x80u;      /* left edge */
    SPRITE_DATA[(uint8_t)(row * 3u + 2u)] = 0x40u; /* right edge */
}
```

Compiled at `-Os`, only row 1's left-edge byte was ever correct. Reading
the sprite back from a live VICE session after triggering Look mode showed
rows 2-16 all had their left-edge byte at 0 (unwritten, still the
`memset(SPRITE_DATA, 0, 64u)` from earlier in the same function), while
every row's right-edge byte was correct:

```
row  1: 10000000 00000000 01000000   <- correct
row  2: 00000000 00000000 01000000   <- left edge missing
row  3: 00000000 00000000 01000000   <- left edge missing
...
row 16: 00000000 00000000 01000000   <- left edge missing
```

On screen this is a square with a solid top, bottom and right edge, but
the left edge present only at its very top pixel - "an outline with pixels
missing on the left side," exactly as reported.

## Root cause, from the disassembly

```
.l11:
3a1c : a9 80    LDA #$80
3a1e : 99 00 fc STA $fc00,y   ; left-edge write, indexed by Y
3a21 : a9 40    LDA #$40
3a23 : 9d 00 fc STA $fc00,x   ; right-edge write, indexed by X
3a26 : 8a       TXA
3a27 : 18       CLC
3a28 : 69 03    ADC #$03
3a2a : a8       TAY           ; Y := X + 3   <-- should be Y := Y + 3
3a2b : e8       INX
3a2c : e8       INX
3a2d : e8       INX           ; X := X + 3
3a2e : e0 35    CPX #$35
3a30 : d0 ea    BNE $3a1c
```

The compiler strength-reduced both `row*3u` and `row*3u+2u` into
loop-carried registers (X for the right edge, Y for the left edge) instead
of two multiplications - a legitimate optimization in principle. But Y's
update reads **X**, not its own prior value: `Y := X + 3` instead of
`Y := Y + 3`. X starts one iteration "ahead" of Y (5 vs. 3), so after the
first iteration Y snaps onto X's own sequence and the two indices become
equal from then on - the left-edge write and the right-edge write land on
the *same* byte every iteration, and the right-edge write (which runs
second) always wins. The true left-edge indices (6, 9, 12, ..., 48) are
never written at all.

## Repro attempts (none reproduced it)

Each of these compiled and ran correctly at `-O0` through `-O3`:

1. The loop alone, isolated in `main()`.
2. The loop preceded by the same `memset` + explicit border-byte stores as
   the real function.
3. The loop wrapped in a function with the real function's exact signature
   `(uint8_t tile_x, uint8_t tile_y)`, the same bounds check, a `vic_stuff()`
   helper call after the loop, and called through one extra layer
   (`other_caller()`) rather than directly from `main()`.

Whatever triggers this depends on the surrounding whole-program context
(register pressure from other functions, inlining decisions, or something
else entirely) in a way none of these approximations captured. Do not take
"compiles correctly in isolation" as proof a similar loop is safe in this
codebase - see the workaround below to sidestep this class of bug rather
than fight it.

## Workaround

Two things were tried and rejected before finding one that works. All
three are recorded here since the failures are informative:

1. **Two loop-carried running offsets instead of a multiply** (the fix
   pattern that worked for `OSCAR64_BUG_POINTER_LOOP_ADVANCE.md`):
   ```c
   left = 3u; right = 5u;
   for (row = 1u; row < 17u; ++row) {
       SPRITE_DATA[left] = 0x80u;
       SPRITE_DATA[right] = 0x40u;
       left += 3u; right += 3u;
   }
   ```
   **Did not fix it** - confirmed live. The two induction variables still
   merged, just onto a different byte (index 6, 9, 12, ... instead of 5, 8,
   11, ...): every row's `left`/`right` write both landed on `left`'s
   sequence, and the *right*-edge byte was the one left unwritten this
   time.

2. **What actually worked**: split into two separate loops, each with only
   one loop-carried variable driving one array write:
   ```c
   for (row = 3u; row <= 48u; row += 3u) SPRITE_DATA[row] = 0x80u;
   for (row = 5u; row <= 50u; row += 3u) SPRITE_DATA[row] = 0x40u;
   ```
   Confirmed live (VICE memory dump) that every row 1-16 gets both its
   left and right edge byte correctly.

This suggests the trigger is specifically **two array writes to
independently-varying indices within the same loop iteration**, not
loop-carried induction variables in general - but given the repro attempts
above all failed to isolate even that narrower claim, treat it as an
observation from one workaround succeeding, not a proven rule.

## Guidance

- A loop that writes to more than one array (or more than one place in the
  same array) per iteration, where more than one of those index
  expressions varies with the loop counter, is exactly the shape that
  produced this bug. Prefer one loop per destination sequence.
- This is a **second, distinct** Oscar64 `-Os` loop-codegen defect found in
  this project - see `OSCAR64_BUG_POINTER_LOOP_ADVANCE.md` for the first
  (a pointer's post-loop advance silently dropped). Between the two, "audit
  every loop that writes to more than one place" is warranted for any
  persisted or rendered buffer built at `-Os` in this codebase, not just
  the two functions already found and fixed.
- No regression test caught this either: `make test`'s existing checks
  enter Look mode but never inspected the cursor sprite's actual bitmap
  bytes, only that Look mode toggled on/off correctly. A pixel-level check
  was added alongside this fix.

## Before reporting upstream

Getting a standalone repro is the missing piece - try compiling the real
`src/platform.c` with unrelated functions progressively stripped out (a
delta-debugging pass) rather than building approximations up from nothing,
since three "build it up" attempts here all failed to trigger it.
