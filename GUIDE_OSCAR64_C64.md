# Oscar64 C64 development guide

Agent-oriented summary of the local [Oscar64 manual](/home/daniel/Programs/oscar64/oscar64.md).
Read the relevant manual section and compiler headers when changing an ABI or
compiler feature. This guide distinguishes compiler capabilities from this
game's actual configuration; [MEMORY_MAP.md](MEMORY_MAP.md) owns the allocation.

## Build model and useful flags

Oscar64 compiles and links the whole program in one invocation. It analyzes
the call graph, removes unreachable objects, and allocates registers/static
frames across functions. There are no separately linked room executables.
Library headers can pull in implementation sources with `#pragma compile`;
their code does not automatically inherit the including file's bank section.

The Makefile builds native C with `-n -Os -xz -tf=crt`:

| Flag | Meaning and project implication |
|---|---|
| `-n` | Native 6502 output; this game does not use the bytecode interpreter. |
| `-Os` | Optimize for size. Main RAM, not flash capacity, is the tight budget. |
| `-xz` | Extended zero-page configuration; do not expect a return to BASIC. |
| `-tf=crt` | EasyFlash output with compiler startup; not the generic `crt8`/`crt16` formats. |
| `-i=path`, `-dNAME=value` | Add an include path or preprocessor definition. |
| `-g` | Include source/debug information and emit `.dbj` debug JSON. |
| `-gp` | Emit profiling information, including `.csz` code-size data. |
| `-rmp` | Write `error.map`/`error.asm` after a failed link for diagnosis. |
| `-O0` | Disable optimizations for a targeted diagnostic; the full game may no longer fit. |
| `-O2`/`-O3` | More aggressive optimization, including automatic inlining. Recheck bank boundaries. |
| `-OM` | Allow self-modifying code: inappropriate for code executing in cartridge ROM. |

Run `make OSCAR64=/home/daniel/Programs/oscar64/build/oscar64`.
To change flags, pass the complete replacement, e.g.
`OFLAGS='-n -Os -xz -tf=crt -g -rmp'`. Make does not track command-line flag
changes: use a fresh build or force a rebuild. Back up the save disk before
`make clean`: it removes all of `build/`, including default saves.

The compiler supports a C++ subset via `-pp` or `.cpp`, not every modern C++
feature. The active game uses C. Do not introduce C++-only banking helpers or
assume desktop standard-library/file-I/O support without changing the build.

Manual sections: “Command line options”, “Cartridge formats”, “Output files”,
“Using libraries”, and the C++ feature/limitations lists.

## Sections, regions, initialization

A section groups code/data/BSS objects; a region assigns sections to an address
range and optionally a cartridge bank. Region end addresses are exclusive.
`#pragma code(section)`, `data(section)`, and `bss(section)` change placement
of subsequent objects; restore the defaults when leaving a banked block.

The game's main region is:

```c
#pragma region(main, 0x0900, 0x7e00, , , {code, data, bss, stack})
#pragma stacksize(2048)
```

This includes resident globals and both dynamic stack storage and static
frames. Do not grow it into the environment window at `$7E00-$7FFF`.
No heap section is reserved in this game.

For banked sections, follow an existing module's pattern:

```c
#pragma section(example_code, 0)
#pragma section(example_data, 0)
#pragma region(example_bank, 0x8000, 0xc000, , BANK_EXAMPLE,
               {example_data, example_code})
#pragma code(example_code)
#pragma data(example_data)
/* __noinline entries and const ROM data here */
#pragma code(code)
#pragma data(data)
```

`BANK_EXAMPLE` is illustrative: allocate a nonconflicting bank in `src/banks.h`
before using it. Leave writable state in resident BSS, not in ROM.

EasyFlash startup expands main from bank 0 and clears its normal BSS.
Additional RAM regions are not automatically initialized; custom BSS sections
are not automatically cleared. Charset/sprite RAM stays high beneath ROM,
not in main. The game explicitly copies charsets and object zone B at boot.

A region's optional final argument is the runtime address for relocated code.
Environment modules use this to execute at `$7E00` after copying from flash.
Their entry-pointer section must precede constants and code.

`__export` or `#pragma reference(symbol)` keeps otherwise unreferenced objects;
`#pragma align(symbol, power_of_two)` controls alignment. Use these only where
the hardware or entry contract requires them. `__bankof(symbol)` identifies a
ROM bank; it does not switch banks or make a normal pointer bank-aware.

Manual sections: “Linker control”, “Placement”, “Additional BSS sections”,
“Heap and Stack sections”, “Cartridge banks”; sample `easyflashreloc.c`.

## Memory mapping and banked calls

`volatile` is for hardware registers and interrupt-shared state.
`__memmap` additionally prevents memory accesses from moving across a mapping
register write. Use the compiler's mapping-aware hardware declarations or
the established resident helpers, not an ordinary byte pointer to a bank port.

Use `far_call_bank` from `src/memmap.h` and `__noinline` banked entries.
The software shadow `platform_cart_bank` is authoritative: `$DE00` is
write-only. Ordinary pointers contain no bank identity; a pointer into ROM
is usable only while its bank is selected. Never return a bank-local string
for later resident use without copying it or retaining an explicit bank context.

Gameplay uses `$01` low bits `111`. Clearing HIRAM hides cartridge ROM as well
as KERNAL, so only resident code can make that change. CPU stores beneath
KERNAL reach RAM; loads see ROM until the resident RAM gate changes mapping.
The VIC independently reads the high-RAM charsets/sprites.
See [EasyFlash contracts](EASYFLASH_CARTRIDGE.md) for project-specific details.

Manual sections: “Memory consistency”, “Prevent inlining”, “Cartridge banks”;
samples `easyflash.c`, `easyflashshared.c`, `easyflashreloc.c`.

## Inline assembly and interrupts

Prefer C unless timing or a hardware/KERNAL entry requires assembly.
Oscar64 inline assembly references C variables by name and exposes compiler
register symbols such as `accu`. Do not hard-code a zero-page ABI copied from
the manual's illustrative layout; inspect this build's listing instead.

Return an assembly result with an assembly expression:

```c
uint8_t kernal_getin_example(void) {
    return __asm {
        jsr $ffe4
        sta accu
    };
}
```

This only illustrates result transfer. Game input must use
`platform_input_poll()`, which also performs SCNKEY, IRQ protection and
raster resynchronization. A bare `sta local` in an asm statement is not a
reliable substitute for assigning a C-visible result: the optimizer can
otherwise treat the C local as uninitialized.

`__interrupt` preserves the compiler zero-page registers used by its closure.
`__hwinterrupt` additionally saves CPU registers and returns with RTI.
Choose the wrapper for the actual entry stack: raw hardware IRQ/NMI and
KERNAL-dispatched IRQ are different. Follow `src/irq.c`; do not attach a
second hardware wrapper to a KERNAL prologue.

The assembly optimizer can transform instruction sequences. Use
`__asm volatile` or a scoped `#pragma optimize(noasm)` when appropriate, and
inspect emitted instructions for exact interrupt exits, register clobbers and
timing. Preserve processor status and the complete mapping state across gates.

Manual sections: “Inline Assembler”, “Interrupt routines”, “Assembler optimizer”.

## Assets and text encoding

`#embed [limit [offset]] "file"` embeds bytes; optional formats include
`lzo`, `rle`, and `word`. Compression requires a corresponding runtime
decoder; embedding does not initialize arbitrary high RAM in a CRT.
This game's `.cchr`, `.ctil` and `.cobj` files are custom editor formats.
Do not apply the manual's CharPad/SpritePad extraction modes to them.

`p"…"` produces PETSCII literals, `s"…"` screen codes, and
`#pragma charmap(source, target, count)` changes literal translation.
The project uses a custom mixed-case PETSCII mapping in `src/platform.h`
and converts display text to screen codes in its own renderer. Do not add
`-psci` or conio font changes without checking this existing mapping.

Disk command bytes are a separate protocol. `src/disk_io.c` restores
identity letter mappings for low-PETSCII DOS filenames and mode letters.
See [disk encoding notes](SAVE_DISK_WRITE_BUG.md).

Manual sections: “Embedding binary data”, “Console input and output”;
`<c64/kernalio.h>` is the compiler's disk API, not desktop stdio.

## Optimization and debugging

Prefer bounded unsigned 8-bit values where their range genuinely fits.
The optimizer can narrow arithmetic and reason about enum/value ranges.
Avoid recursion and unnecessary function-pointer dispatch because they limit
static call-graph analysis; existing room/environment pointers are deliberate
interfaces, not inherently invalid under banking.

`__assume(condition)` is a promise, not a runtime check. Only use it for
invariants already guaranteed on every path. `__striped` changes array layout
and pointer semantics; it is not compatible with serialized asset structures
without conversion. Use `#pragma unroll` only after measuring size and cycles.
Scope local optimizer changes with `#pragma optimize(push)`/`pop`.

Inspect `build/game.map` for placement/size, `game.asm` for actual instructions,
and `game.lbl` for VICE labels (`load_labels`). With `-g`, `.dbj` describes
types/functions/variables and their address ranges. A successful link alone
does not validate mapping, startup initialization, raster timing or input.

Run `make test` through Xvfb. Keyboard tests use real XTest key events, not
writes to the KERNAL buffer. Check separate CPU/RAM/I/O views in the monitor;
write raster values explicitly in hex. For sound failures capture real WAV
audio: VICE's `dump` device is a register trace, not PCM. SID register activity
alone does not prove that sound is audible.

Manual sections: “Helping the compiler optimizing”, “Loop unrolling”,
“Striped arrays”, “Additional Optimizer information using __assume()”.
