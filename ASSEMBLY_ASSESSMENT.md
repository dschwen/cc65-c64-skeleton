# Oscar64 optimization and assembly policy

Prefer readable C and measure the active Oscar64 build before replacing a
routine with assembly. Whole-program optimization can remove parameter,
arithmetic and pointer overhead that a source-level estimate assumes exists.
See [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) for the manual summary.

## Measurement checklist

1. Build with the actual game flags (`-n -Os -xz -tf=crt`).
2. Read `build/game.map` and `game.asm` to locate the routine and its helpers.
   Optional `-gp` emits code-size profiling information.
3. Count added constants, mutable state, stack/static frames and resident
   wrappers, not just the rewritten instruction body.
4. Measure cycles in VICE over representative calls, including worst cases;
   a smaller listing alone is not a speed measurement.
5. Verify outputs, calling conventions, mapping and interrupt behavior.
6. Re-run `make test`; add a targeted check for behavior it does not cover.

Banking a coarse service can recover resident space without rewriting it.
Audit the complete call/data closure first; code in ROM cannot self-modify.
Do not move charsets or sprites into main to simplify a routine.

## Good assembly boundaries

Exact IRQ/NMI exits, KERNAL register calls and short CPU mapping gates are
reasonable assembly boundaries. Use Oscar64's compiler-visible assembly
expressions for results and its interrupt qualifiers for register preservation.
Do not assume a fixed zero-page calling convention or transplant a standalone
routine without checking the generated caller and callee.

Prefer bounded unsigned types, explicit const data and simple loops first.
Use local optimizer pragmas, unrolling or `__assume` only with measured benefit
and proven preconditions. Existing binary formats must not acquire striped
array layout or compiler-dependent serialization.
