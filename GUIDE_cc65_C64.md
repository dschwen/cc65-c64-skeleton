# cc65-based C programming on the Commodore 64 (C64)

This guide is written for an automated coding agent: it focuses on **what to do**, **what to include**, and **what to avoid** when targeting the C64 with **cc65**.

---

## 1) Toolchain overview (cc65 on C64)

### Core tools
- **`cc65`**: C compiler (C → assembly)
- **`ca65`**: 6502 assembler
- **`ld65`**: linker (uses a `.cfg` memory layout)
- **`cl65`**: convenience driver (compile + assemble + link)

### Typical build command
```bash
cl65 -t c64 -Oirs -o game.prg main.c
```
Common flags:
- `-t c64`: selects the C64 target runtime + default config
- `-Oirs`: optimization (good default starting point for 6502)
- `-m game.map`: emits a link map (highly recommended)
- `-Ln labels.lbl`: exports labels (useful for debugging/monitor tools)

### Runtime + memory layout
cc65 ships a **C64 runtime** and describes its memory layout and platform specifics in its C64 target documentation.  
The default linker configuration file for the target is `cfg/c64.cfg`.

**Agent note:** If you need custom screen buffers, sprite data, or music tables, you will often edit or replace `c64.cfg` so you can place segments at fixed addresses.

---

## 2) C64 memory map essentials (what you’ll touch constantly)

### “Must-know” fixed regions
- **Zero page** (`$0000–$00FF`): extremely performance-critical; cc65 uses parts of it.
- **Hardware I/O page(s)**:
  - **VIC‑II registers**: base **`$D000`** (mirrored across `$D000–$D3FF`)
  - **SID registers**: base **`$D400`**
- **Color RAM**: **`$D800–$DBE7`** (1 nibble per character cell color)
- **Default screen RAM**: **`$0400–$07E7`** (1000 chars)
- **BASIC ROM / KERNAL ROM / I/O banking**: affected by the CPU port at `$0001` (banking is a whole topic; keep in mind when reading ROM character data vs using RAM charsets).

**Agent note:** Most “why is my code slow / glitchy / black screen” bugs are: wrong bank, wrong VIC memory pointer (`$D018`), forgetting color RAM, or clobbering zeropage/stack.

---

## 3) VIC‑II (graphics) quick map: base `$D000`

### VIC‑II register base and mirroring
- VIC‑II registers are CPU-visible starting at **`$D000`** and the register area is repeated every `$40` bytes through `$D3FF`.

### The short list of VIC registers you’ll use most
(Addresses are absolute CPU addresses.)

#### Sprite positions and enables
- `$D000–$D00F`: sprite 0–7 X/Y (pairs)
- `$D010`: sprite X MSB bits (9th X bit for sprites)
- `$D015`: sprite enable (bit i enables sprite i)

#### Scrolling, screen on/off, modes
- `$D011`: control reg 1 (vertical scroll, 24/25 rows, screen enable, bitmap flag, etc.)
- `$D016`: control reg 2 (horizontal scroll, 38/40 cols, multicolor flag, etc.)

#### Memory pointers (critical)
- `$D018`: **VIC memory pointers** (screen base + character base within the active VIC bank)

#### IRQ / raster basics
- `$D012`: raster line compare (low 8 bits)
- `$D019`: IRQ status/ack
- `$D01A`: IRQ enable

#### Colors
- `$D020`: border color
- `$D021`: background 0
- `$D022–$D024`: extra background colors (multicolor modes)
- `$D027–$D02E`: sprite 0–7 colors

### Screen memory, character memory, and VIC banking (practical rules)
- The VIC sees **16 KB at a time** (“VIC bank”). Your screen/charset pointers in `$D018` are **offsets within that 16 KB bank**.
- Common beginner setup:
  - screen at `$0400`
  - charset at `$1000` (or ROM charset via banking tricks)
- Color RAM is **always** at `$D800` (not banked like screen RAM).

**Agent note:** When writing a text-mode engine, you’ll typically maintain:
- `char* screen = (char*)0x0400;`
- `unsigned char* color = (unsigned char*)0xD800;`
…and then set `$D018` accordingly when you move screen/charset.

---

## 4) SID (sound) quick map: base `$D400`

### SID base and voice layout
SID is memory-mapped at **`$D400`**.  
There are 3 voices; each voice has frequency, pulse width, control, and ADSR registers.

### Voice 1 (repeat pattern for voices 2 and 3)
- `$D400/$D401`: frequency low/high (voice 1)
- `$D402/$D403`: pulse width low / high nibble
- `$D404`: control (gate + waveform bits etc.)
- `$D405`: attack/decay
- `$D406`: sustain/release

### Global / filter / volume (most used)
- `$D418`: volume (lower 4 bits) and some analog quirks; commonly used just to set volume 0–15.

**Agent note:** For “serious” music, you’ll usually use an assembly music driver. In C, you often:
- call a driver’s init routine once
- call a `music_play()` routine each frame (50/60 Hz), typically from a raster IRQ or main loop tick

---

## 5) cc65 idiosyncrasies that matter in real projects

### Integer sizes and performance reality
On 6502 targets, cc65’s type sizes follow “small machine” practicality:
- `char` / `unsigned char`: 8-bit
- `int` / `unsigned int`: typically 16-bit on cc65 targets
- `long` / `unsigned long`: 32-bit

**Performance rule:** 16-bit math is OK; 32-bit math is expensive; avoid `long` in inner loops.

### The stack, parameter passing, and calling conventions
cc65 uses a **software C stack** and has specific calling conventions.  
Practical implications:
- Function calls are relatively costly.
- Passing many parameters can be costly.
- Prefer:
  - small structs passed by pointer
  - fewer parameters
  - tight loops in a single function
- Consider `__fastcall__` for hot functions (pair with careful prototypes).

### Zero page and “register variables”
- cc65 will allocate internal pointers and temporaries, often in **zero page**, for speed.
- You can also reserve your own zero-page locations in the linker config or via cc65 pragmas/segments.

**Agent note:** If you mix C and assembly, document which zero-page addresses are “owned” by which module.

### Libraries are “retro-targeted” (not POSIX)
The cc65 standard library is not like modern libc; it’s geared to 8-bit systems.  
Most C64 programs use these heavily:
- `conio.h` (text screen I/O: `clrscr`, `cputc`, `cputs`, `gotoxy`, `cprintf`, etc.)
- `cbm.h` (C64/CBM KERNAL and device I/O helpers)
- `peekpoke.h` (typed `PEEK/POKE` style accessors)
- `joystick.h` (if using cc65 drivers)
- `tgi.h` (graphics interface, if you choose a driver-based approach)
- target headers (`c64.h`) for symbols (when available)

**Agent note:** If you’re doing custom raster or sprites, you will likely bypass TGI and write VIC registers directly.

### Memory layout, segments, and `c64.cfg`
- On C64 you routinely place:
  - code in a contiguous region
  - data tables in another
  - screen/sprite/charset assets at fixed addresses
- cc65’s linker (`ld65`) is segment-driven; the **linker config** controls where segments land (`c64.cfg` is the starting point).

**Best practice for agents:** produce a project with:
- `cfg/mygame.cfg` (custom)
- `src/` for C/asm
- `assets/` for charset/sprites/music
- a `Makefile` that emits a `.map`

---

## 6) Hardware access patterns in C (safe + idiomatic)

### Direct memory-mapped I/O access
Use `volatile` to prevent the compiler from optimizing out register writes:
```c
#define VIC_BASE  ((volatile unsigned char*)0xD000)
#define SID_BASE  ((volatile unsigned char*)0xD400)

#define VIC(reg)  (VIC_BASE[(reg)])
#define SID(reg)  (SID_BASE[(reg)])

/* Example: border color */
VIC(0x20) = 6;   /* $D020 = 6 */
```

### Screen and color RAM writes
```c
#define SCREEN ((unsigned char*)0x0400)
#define COLOR  ((unsigned char*)0xD800)

void putxy(unsigned char x, unsigned char y, unsigned char ch, unsigned char col) {
    unsigned int i = (unsigned int)y * 40u + x;
    SCREEN[i] = ch;
    COLOR[i]  = col & 0x0F;
}
```

---

## 7) Suggested “minimum viable” project structure for an agent

```
cc65-c64-skeleton/
  Makefile
  cfg/
    myc64.cfg
  src/
    main.c
    vic.h / vic.c
    sid.h / sid.c
    irq.s        (optional raster IRQ)
  assets/
    (place binary assets here)
  build/
```

---

## 8) Practical gotchas checklist (C64 + cc65)

- **VIC bank mismatch**: screen pointer looks correct in CPU space but VIC is reading a different bank.
- **Forgetting color RAM**: chars appear but colors are wrong/uninitialized.
- **Raster IRQ not acked**: forgot to write to `$D019` to acknowledge → repeated interrupts / lockups.
- **Calling heavy C in IRQ**: keep IRQ handler tiny; set flags and return.
- **Accidental `long` in tight loops**: kills performance.
- **Too many globals**: pushes data into inconvenient memory; use a map file and control segments with cfg.
- **Clobbering zeropage**: when mixing asm/C without a clear contract.

---

## 9) Reference: key base addresses (one-glance)

### Current skeleton layout

The demo keeps the VIC-II in bank 0 and uses these fixed addresses:

| Address range | Use |
|---|---|
| `$0400-$07E7` | 40x25 screen matrix |
| `$2000-$27FF` | tile charset (`charset.cchr` bank 0) |
| `$2800-$2FFF` | text charset (`charset.cchr` bank 1) |
| `$3000-$37FF` | 256 tile definitions from `tiles.ctil` |
| `$3800-$38FF` | 256 tile property bytes from `tiles.ctil` |

`$D018` is `$18` for tiles and `$1A` for text. The raster IRQ switches to
the text charset at screen row 22 and restores the tile charset at raster 0.
Row 22 is left blank as spacing above the text on rows 23-24.
It disables CIA1 interrupts, so the KERNAL jiffy clock does not advance while
the demo runs. Rework the IRQ chaining if a game needs KERNAL timekeeping or
other CIA1 interrupt services.

### EasyFlash cartridge build

`make cartridge` builds the normal PRG first, embeds its payload in EasyFlash
bank 0, and creates `build/game.crt` with VICE `cartconv`. The cartridge has
both the standard `CBM80` header at `$8000` and Ultimax vectors in the final
six bytes of physical ROMH. Its bootstrap selects 16 KiB mode, initializes the
KERNAL, copies the PRG to its normal `$0801-$38FF` RAM layout, and disables the
cartridge before entering the cc65 startup at `$080D`.

The game does not currently write save data to flash. EasyFlash programming
requires RAM-resident driver code, sector erase handling, and correct polling;
ordinary C stores to banked ROM are not sufficient.

See `EASYFLASH_CARTRIDGE.md` for the complete cartridge-generation guide,
including boot vectors, CRT CHIP layout, validation, multi-bank growth, and
flash-save constraints.

### VIC‑II
- **VIC register base**: `$D000` (mirrored through `$D3FF`)
- **Border**: `$D020`
- **Background 0**: `$D021`
- **IRQ enable/status**: `$D01A` / `$D019`
- **Raster compare**: `$D012`
- **Control**: `$D011`, `$D016`
- **Memory pointers**: `$D018`

### SID
- **SID base**: `$D400`
- **Voice 1 freq**: `$D400/$D401`
- **Voice 1 control**: `$D404`
- **Master volume**: `$D418`

### RAM commonly used
- **Screen RAM (default)**: `$0400`
- **Color RAM**: `$D800`
