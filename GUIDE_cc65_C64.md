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

This project's compile rules also pass `--create-full-dep` and include the
resulting `.d` files. Do not remove that dependency generation: structure and
ABI declarations in headers affect many independently linked resident and
overlay objects, and a stale object can still link successfully while using an
old layout.

Fixed EasyFlash module coordinates are declared once in
`cfg/easyflash_layout.json`. The build generates C and ca65 constants from it,
then `tools/validate_easyflash_layout.py` compares every fixed module against
the final packed image and verifies the in-place module's linked entry address.

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

### CPU-port banking contract

`$0000` is the 6510 port data-direction register and `$0001` is its data
register. Bits 0-2 of `$0000` must be outputs before `$0001` can reliably drive
`LORAM`, `HIRAM`, and `CHAREN`:

```asm
lda $00
ora #$07
sta $00
```

Do not casually replace the upper five bits of `$01`; they include cassette
port state. Save the old value or replace only the low three bits. With no
cartridge ROM selected, useful mappings are:

| `$01` low bits | Common value | `$A000-$BFFF` | `$D000-$DFFF` | `$E000-$FFFF` |
|---|---:|---|---|---|
| `111` | `$37` | BASIC ROM | I/O | KERNAL ROM |
| `110` | `$36` | RAM | I/O | KERNAL ROM |
| `101` | `$35` | RAM | I/O | RAM |
| `100` | `$34` | RAM | RAM | RAM |
| `011` | `$33` | BASIC ROM | character ROM | KERNAL ROM |
| `000` | `$30` | RAM | RAM | RAM |

The table assumes the common upper bits `$30`; code should normally preserve
the actual upper bits and apply the listed low-bit pattern. When both `LORAM`
and `HIRAM` are clear, `CHAREN` no longer selects I/O/character ROM and RAM is
visible at `$D000-$DFFF`.

The VIC reads display memory independently of this CPU mapping, but the CPU
cannot access VIC/SID/CIA/Color RAM while the I/O layer is hidden. EasyFlash
bank/control registers at `$DE00/$DE02` are also I/O and disappear in an
all-RAM or character-ROM mapping. Cartridge `GAME` and `EXROM` signals add PLA
states on top of this baseline table.

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

The game keeps the VIC-II in bank 3 and uses these fixed addresses:

`MEMORY_MAP.md` is the authoritative detailed allocation, including actual
linker tails, RAM hidden by BASIC/I/O/KERNAL, and all eight sprite slots.

| Address range | Use |
|---|---|
| `$2000-$23FF` | current room |
| `$2400-$27FF` | destination-room staging |
| `$2800-$2BFF` | sparse world-delta journal |
| `$2C00-$2FFF` | resident BSS and compact helpers |
| `$3000-$37FF` | 256 tile definitions from `tiles.ctil` |
| `$3800-$38FF` | 256 tile property bytes from `tiles.ctil` |
| `$3A00-$3BFF` | independently loaded helpers and bottom-text pager |
| `$3C00-$7FFF` | resident platform code and read-only tables |
| `$8000-$85FF` | file-backed load-image fill |
| `$8600-$8B47` | resident world-state code |
| `$8B48-$98FF` | resident game/main and shared room-API code |
| `$9900-$9CFF` | active 1 KiB room-specific code overlay |
| `$9D00-$9FFF` | pristine current-room object baseline |
| `$A000-$A479` | save-record/index scratch |
| `$B000-$BFFF` | rebuildable work RAM, staging, or one copied overlay |
| `$C000-$C0FF` | cc65 software stack |
| `$C100-$C173` | fixed resident `GameState` |
| `$C174-$C178` | mutable in-place Inventory service state |
| `$C180-$E482` | 256 hot object-type records, partly beneath I/O/KERNAL |
| `$E800-$EFFF` | tile charset in VIC bank 3 |
| `$F000-$F7FF` | text charset in VIC bank 3 |
| `$F800-$FBFF` | screen matrix and sprite pointers |
| `$FC00-$FDFF` | cursor/portrait/rain sprite bitmaps |

`$D018` is `$EA` for tiles and `$EC` for text. The raster IRQ switches to
the text charset at screen row 22 and restores the tile charset at raster 0.
Room transitions clear rows 22-24, disable the VIC raster source, and force
`$D018=$EA` until the destination has been completely drawn. Their epilogue
resynchronizes the split before reenabling the source. The handler also
uses `$D011` bit 7 with `$D012`: `$D012` alone wraps at raster line 256 and is
not enough to distinguish vertical blank from the top of the next frame.
The IRQ is implemented entirely in `src/irq.s`. Rainy rooms use hardware
sprites 1-7 directly, one dedicated sprite per streak; no raster multiplexing
is needed since there are exactly as many sprites as streaks. Its
bottom-of-map branch rotates a low-RAM shadow of charset character 14 and
writes it to `$E870-$E877` every
second frame (25 Hz PAL, 30 Hz NTSC) and advances all seven streaks every
frame (50 Hz PAL, 60 Hz NTSC). Both happen only after the VIC has switched
away from the tile charset, avoiding visible partial writes. The water source
is an eight-byte shadow below `$8000`, so the IRQ never reads charset RAM while
KERNAL hides it.
Row 22 is left blank as spacing above the text on rows 23-24. A full room draw
clears all three rows in both screen and Color RAM before drawing the new room.
It disables CIA1 interrupts, so the KERNAL jiffy clock does not advance while
the demo runs. Rework the IRQ chaining if a game needs KERNAL timekeeping or
other CIA1 interrupt services.

The custom IRQ also bypasses normal KERNAL keyboard scanning. The platform's
frame-driven game loop calls the assembly `platform_input_poll()` wrapper once
per frame; it invokes the KERNAL `SCNKEY` and `GETIN` entry points. Do not poll
`SCNKEY` in an unrestricted busy loop because its debounce and repeat timing
assume roughly one call per video frame.

The raster handler has a standalone RAM-vector entry at `$FFFE/$FFFF` that
saves A/X/Y and returns with `RTI`, plus a `$0314` entry for KERNAL-mapped disk
intervals. Short keyboard calls map KERNAL around the call. Accessing RAM under
`$D000-$DFFF` remains a short critical section because VIC/SID/CIA and Color
RAM are unavailable in that mapping.

### EasyFlash cartridge build

`make cartridge` builds the normal PRG first, splits its payload across
EasyFlash banks 0-2, and creates `build/game.crt` with VICE `cartconv`. The cartridge has
both the standard `CBM80` header at `$8000` and Ultimax vectors in the final
six bytes of physical ROMH. Its bootstrap selects 16 KiB mode, initializes the
KERNAL, copies the PRG to its linked RAM layout, copies the helper/text module
from the unused tail of executable bank 2 to `$3A00`, and disables the cartridge before
entering the cc65 startup at `$080D`.

### Split text module

The native/text module is linked separately from the resident engine.
`build/text.prg` is a normal fixed-address PRG with a `$3A00` load header.
The EasyFlash bootstrap copies that module from the executable cartridge
banks before entering the resident cc65 startup. This leaves the pager
replaceable without relinking the engine ABI. Disk is used only for save files.

The pager is linked after the resident label file exists. Its calls to
`platform_wait_frame()` and `platform_input_poll()`, and its references to the
resident color/line parameters, are resolved from `build/game.lbl`. The fixed
pager entry is `$3A73` (`src/text_api.s`).

The game now maintains a backend-neutral sparse room-object journal in RAM but
does not yet write save data to flash. EasyFlash programming
requires RAM-resident driver code, sector erase handling, and correct polling;
ordinary C stores to banked ROM are not sufficient.

See `EASYFLASH_CARTRIDGE.md` for the complete cartridge-generation guide,
including boot vectors, CRT CHIP layout, validation, dynamic room banks,
object-type RAM placement, native drawing plans, and flash-save constraints.

The current room occupies `$2000-$23E8`; destination staging is `$2400-$27E9`
and the world journal is `$2800-$2BE7`. Resident world-state code is loaded at
`$8600`; game/main and the shared room API occupy `$8B48-$98FF`.
Independently linked room code has a 1 KiB window at
`$9900-$9CFF`, and the current-room pristine object baseline occupies
`$9D00-$9FFF`. Ordinary BSS is at `$2C00-$2EDD`; independently loaded helpers
and the bottom-text pager occupy `$3A00-$3BEF`. The C software
stack, `GameState`, and Inventory's five mutable service bytes sit at
`$C000-$C0FF` and `$C100-$C178` instead - that
region is the only RAM above `$8000` never covered by a cartridge bank, which
is what lets banked code use them (see `PLATFORM_API.md`'s "Banked code").
KERNAL-only calls use CPU mapping `$36`, which keeps KERNAL and I/O visible
while leaving BASIC hidden. In-place cartridge execution instead requires
`$01=$37`: use `$DE02=$06` for ROML-only or `$07` for ROML+ROMH. Even the
ROML-only case reads BASIC ROM, not underlying RAM, at `$A000-$BFFF`. The RH
service now executes at `$84C0` in that mode; its resident wrapper performs
mapping-changing hot-type access before entry and all `$B000` redraw work
after return.

`WORKBSS` currently uses `$B000-$B7BE` for base colors, tile brightness,
visibility buffers, and caches. Room-code staging deliberately overwrites a prefix of this
rebuildable data; a subsequent room draw reconstructs it. EasyFlash 16 KiB
room-code copies are implemented in assembly because ROMH temporarily hides
the destination. `$B7BF-$BFFF` is the normal-play WORKRAM tail; copied overlays
may use the complete `$B000-$BFFF` page after the resident caller has staged
arguments and accepted that renderer work data will be rebuilt.
Check `HIGHCODE`, `UPPERCODE`, `BSS`, `WORKBSS`, and
the overlay map files whenever adding fixed buffers or resident APIs.

The current `HIGH` segment has 218 bytes of linker margin, `UPPER` has 34,
and `PROGRAM` has 42. Inspect `build/game.map` before adding resident logic.
The VIC display lives beneath KERNAL in bank 3: charsets at `$E800/$F000`,
screen at `$F800`, and sprite data at `$FC00-$FDFF`.

Generic room-callable functions live in resident `src/game_support.c` and are
resolved by address when each room is linked. Room binaries contain only their
header, `enter_room()`, `enter_tile()`, `look_at()`, private helpers, strings,
and BSS. This
keeps the 1 KiB overlay available for room-specific behavior without repeating
inventory, text, transition, or save-aware object code in every room file.

ROML asset reads are also native assembly. ROML requires CPU mapping `$37`;
mapping `$36` exposes underlying RAM instead. Because `$37` hides the C stack
under BASIC ROM, C `memcpy()` must not run while the cartridge window is live.

See `PLATFORM_API.md` for room/object binary formats and the public C API for
map drawing, object movement, transitions, bottom text, lighting, and the Look
cursor.
Editor-authored room text and object names remain ASCII in `assets/`; the build
stages PETSCII copies in `build/assets/` for the EasyFlash image.
See `ROOM_CODE_API.md` for `GameState`, per-room hooks, and room-code overlay
constraints.
See `SAVE_GAME.md` for the room-delta invariants and versioned save record.

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
