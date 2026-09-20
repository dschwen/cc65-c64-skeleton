# Oscar64 bug: pointer advance lost after a `*q++ = *src++` loop

Found 2026-09-20 while fixing the F1/F3 save crash (see
[SAVE_DISK_WRITE_BUG.md](SAVE_DISK_WRITE_BUG.md)). Oscar64 compiled valid C
into code that stores through a pointer correctly inside a loop, but never
applies the pointer's advance after the loop. Later stores through the same
pointer then overwrite the loop's own output.

**Status:** worked around in this repo; **not reported upstream**. Reproduced
with the compiler checkout at `~/Programs/oscar64` HEAD `f38a1f2`
(2026-09-06, "Fix loop forwarding range error"). I did not verify that the
installed binary was built from exactly that commit.

## Symptom in this project

`encode_record()` in `modules/saveload_save.c` builds the save record with a
running pointer `q`. After the inventory loop it copied the 32 flag bytes with:

```c
const uint8_t* src = game_state.flags;
for (i = 0u; i < GAME_FLAG_BYTES; ++i) *q++ = *src++;
put16(q, game_world_delta_count); q += 2u;
```

The compiled code copied the flags but left `q` unchanged, so the 2-byte delta
count overwrote `flags[0..1]`, and the payload length came out 98 instead of
130. The header was self-consistent (length and checksum both matched the
short payload), so the file looked valid but the loader rejected it: it
requires exactly `130 + 5 * deltas`. The visible symptom was "Load failed."
after a save that reported success once the read-name bug was also fixed.

## How to recognize it in `build/game.asm`

The flags loop is indexed by `Y` from a fixed base, with no update of the
pointer temporaries afterward, and the next store reuses the same base:

```
LDY #$00
.loop:
LDA $5101,y ; (game_state.flags[0])
STA (T0 + 0),y            ; store through the un-advanced base
INY
CPY #$20
BNE .loop
LDA T3 + 0                ; next: delta count
LDY #$00
STA (T0 + 0),y            ; <-- same T0: lands on flags[0]
```

A correct build advances `T0` by 32 between the loop and the count store.

## Reproduction

Standalone, no project code. Run with the built-in 6502 emulator, which
reports `main`'s return value:

```sh
oscar64 -n -Os -e min.c        # prints "Emulation result N"
```

```c
#include <stdint.h>

uint8_t flags[32];
uint8_t out[400];
uint16_t count;
uint8_t rec[5 * 4];

uint16_t encode(uint8_t* p) {
    uint8_t* q = p;
    uint16_t i;
    const uint8_t* f = flags;
    for (i = 0u; i < 32u; ++i) *q++ = *f++;      /* 32 bytes */
    q[0] = (uint8_t)count;                        /* 2-byte count */
    q[1] = (uint8_t)(count >> 8);
    q += 2u;
    for (i = 0u; i < count; ++i) *q++ = rec[i];   /* later loop also walks q */
    return (uint16_t)(q - p);                     /* expect 34 + count */
}

int main(void) {
    count = 3u;
    return (int)encode(out);                      /* expect 37 */
}
```

| Optimization | Result | Expected |
|---|---:|---:|
| `-O0` | 37 | 37 |
| `-O1` | **5** | 37 |
| `-O2` | **34** | 37 |
| `-Os` | **34** | 37 |
| `-O3` | **34** | 37 |

Notes on what I did and did not establish:

- The result differs by level (5 vs 34), so more than one advance is being
  lost, or a different one at `-O1`. I did not determine which loop is at
  fault at each level from the assembly of this reduction.
- It is an **interaction**, not a single bad loop. Rewriting *either* loop
  with an explicit advance (`for (...) q[k] = f[k]; q += 32;`, or the same for
  the tail loop) makes `-O2`/`-Os`/`-O3` return 37. At `-O1`, only rewriting
  the first (flags) loop fixes it; rewriting the tail loop still returns 5.
- A smaller version without the trailing `q`-walking loop (one `*q++` loop,
  then stores through `q`, then `q += 2`) looked correct in the `-Os`
  assembly. I only read that assembly; I did not execute it at every level,
  so a single loop may or may not be sufficient to trigger it.
- The unmodified original `encode_record()` extracted into a standalone
  file (with stub `GameState`/`GameWorldDelta` types) returns a payload of 98
  instead of 130 at `-O1`, `-O2`, `-Os` and `-O3`, and 130 at `-O0`.

## Workaround

Copy with a helper that takes the pointer by value and advance explicitly:

```c
copy_bytes(q, game_state.flags, GAME_FLAG_BYTES); q += GAME_FLAG_BYTES;
```

With this change the extracted function returns 130 at all optimization
levels. `copy_bytes()` is a `while (count-- != 0u) *dst++ = *src++;` loop in a
separate function; the caller's `q` is advanced by an explicit `+=` instead of
by the loop. I did not investigate why the helper form compiles correctly.

The encoder also now self-checks its size against the v1 layout
(`save_slot()` in `modules/saveload_save.c`) and refuses to write a record the
loader would reject, so a recurrence fails visibly ("Save failed.") instead of
writing a corrupt slot.

## Guidance

- Any code that builds a **persisted or wire-format** buffer with a running
  pointer should be checked against `build/game.asm`, or better, verified
  by size/round-trip in `make test`. This bug produced plausible output, no
  warning, and self-consistent metadata.
- Prefer explicit offsets or `copy_bytes()`/`memcpy` plus an explicit advance
  over `*q++ = *src++` when the pointer is used again afterward.
- The project builds with `-Os`; `-O0` is not a viable check for this class of
  bug because it is the only level that compiles the reduction correctly.
- The regression in `tools/test_oscar_boot.py` now catches this class of
  fault for the save record: it asserts the stored payload length is
  `130 + 5 * deltas`, and that the checksum and header are valid.

## Before reporting upstream

The reduction above is small enough to file. Still useful first: identify which
loop drops its advance at each optimization level (compare `-O1` and `-Os`
assembly for `encode`), and confirm against a current upstream build.
