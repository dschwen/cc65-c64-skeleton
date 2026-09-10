; Constants shared by the platform's assembly modules. Keep the serialized
; layout values synchronized with platform.h.

CPU_DDR             = $00
CPU_PORT            = $01
CPU_PORT_MASK       = $07
CPU_MAP_ALL_RAM     = $04
CPU_MAP_GAME        = $05
; Cartridge ROML is asserted only with LORAM+HIRAM set. This is $07 for both
; EasyFlash 8 KiB and 16 KiB control modes; $06 exposes RAM at $8000 instead.
CPU_MAP_CART_8K     = $07
CPU_MAP_CART_16K    = $07
; KERNAL and I/O visible, BASIC hidden so $A000-$BFFF remains usable RAM.
CPU_MAP_KERNAL      = $06

SCREEN_RAM          = $f800
COLOR_RAM           = $d800
TILE_DATA           = $3000
TILE_PROPERTIES     = $3800
; Resident PlatformObjectType layout (src/platform.h): dimensions, hotspot,
; chars[16], colors[16], light. Keep these byte offsets synchronized with it.
OBJECT_TYPE_CHARS   = 2
OBJECT_TYPE_COLORS  = 18

ROOM_TILES          = 13
MAP_WIDTH_TILES     = 20
MAP_HEIGHT_TILES    = 11
MAP_WIDTH_CHARS     = 40
MAP_HEIGHT_CHARS    = 22
ROOM_OBJECTS        = ROOM_TILES + MAP_WIDTH_TILES * MAP_HEIGHT_TILES
; Room text is a separate same-ID resource now (assets/resources/<id>), not
; part of the room file - see PlatformRoom in src/platform.h.
ROOM_FILE_BYTES     = ROOM_OBJECTS + 256 * 3

EASYFLASH_BANK      = $de00
EASYFLASH_CONTROL   = $de02
EASYFLASH_OFF       = $04
EASYFLASH_8K        = $06
EASYFLASH_16K       = $07

CART_MARKER         = $033c
CART_MARKER_0       = $ef
CART_MARKER_1       = $64

; The current room's environment module (weather + ambient sound) - see
; PLATFORM_API.md's "Room environment module". Copied in from EasyFlash at
; room entry, called every frame from the resident raster IRQ, so it lives
; in reserved, always-RAM address space (cfg/myc64.cfg's ENVCODE), never a
; banked window. Fixed jump-table layout, 3 bytes per vector (a plain JMP):
; offset 0 init, 3 tick, 6 enable, 9 disable.
ENVCODE_BASE    = $7E00
ENVCODE_SIZE    = $0200
ENVCODE_INIT    = ENVCODE_BASE
ENVCODE_TICK    = ENVCODE_BASE + 3
ENVCODE_ENABLE  = ENVCODE_BASE + 6
ENVCODE_DISABLE = ENVCODE_BASE + 9

; Thin call wrapper for any resident routine that needs to run banked code:
; switches to the given EasyFlash bank (16 KiB mode), makes the call, then
; unwinds back to whatever bank/mode/CPU map was active before - safe to
; nest. See src/banking.s's _platform_bank_call_enter/_leave for the
; contract this depends on (interrupt code never bank-switches). Requires
; ".import _platform_bank_call_enter, _platform_bank_call_leave" in the
; including file.
;
;   BANK_CALL #MY_BANK, my_banked_routine
.macro BANK_CALL bank, routine
    lda bank
    jsr _platform_bank_call_enter
    jsr routine
    jsr _platform_bank_call_leave
.endmacro

; Call a routine by *far address* - a bank byte plus a 16-bit offset inside
; the banked window - rather than by a link-time-resolved symbol. Patches the
; bank and target straight into the shared trampoline's own operands, so a
; call costs no per-callee stub and the whole 1 MiB is reachable uniformly.
; Requires ".import _platform_far_call, far_call_bank_operand,
; far_call_target_operand, far_call_cpu_map_operand,
; far_call_control_operand" in the including file.
;
;   FAR_CALL 12, $8123, CPU_MAP_CART_8K, EASYFLASH_8K
;
; Reentrant: a banked routine may FAR_CALL again, because the trampoline
; consumes all patched operands before its inner jsr and keeps the caller's
; bank/mode/map on the CPU stack. See _platform_far_call in src/banking.s for
; the full argument. An 8 KiB ROML call exposes ROML and BASIC ROM; a 16 KiB
; ROMH call exposes both cartridge halves. Neither can read upper RAM.
;
; Not safe against an interrupt that itself far-calls between the patch and
; the jsr below - the codebase contract is that interrupt code never
; bank-switches at all.
; A is carried through in Y while the operands are patched - the patching
; itself needs A, so without this the callee would receive the target's high
; byte instead of its argument. Y is the trampoline's scratch either way and
; is not preserved in either direction; A and X are.
.macro FAR_CALL bank, target, cpu_map, control
    .assert (cpu_map) = CPU_MAP_CART_8K, error, "FAR_CALL CPU map must assert cartridge ROML"
    .assert ((control) = EASYFLASH_8K) .or ((control) = EASYFLASH_16K), error, "FAR_CALL control must select EasyFlash 8K or 16K mode"
    .if (control) = EASYFLASH_8K
        .assert (target) >= $8000 .and (target) < $A000, error, "8 KiB FAR_CALL target must be in ROML"
    .else
        .assert (target) >= $A000 .and (target) < $C000, error, "16 KiB FAR_CALL target must be in ROMH"
    .endif
    tay
    lda #(bank)
    sta far_call_bank_operand
    lda #<(target)
    sta far_call_target_operand
    lda #>(target)
    sta far_call_target_operand+1
    lda #(cpu_map)
    sta far_call_cpu_map_operand
    lda #(control)
    sta far_call_control_operand
    tya
    jsr _platform_far_call
.endmacro
