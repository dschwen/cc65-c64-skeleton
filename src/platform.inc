; Constants shared by the platform's assembly modules. Keep the serialized
; layout values synchronized with platform.h.

CPU_DDR             = $00
CPU_PORT            = $01
CPU_PORT_MASK       = $07
CPU_MAP_ALL_RAM     = $04
CPU_MAP_GAME        = $05
CPU_MAP_CART_8K     = $06
CPU_MAP_CART_16K    = $07
; KERNAL and I/O visible, BASIC hidden so $A000-$BFFF remains usable RAM.
CPU_MAP_KERNAL      = $06

SCREEN_RAM          = $0400
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
; far_call_target_operand" in the including file.
;
;   FAR_CALL 12, $8123      ; bank 12, offset $8123
;
; Reentrant: a banked routine may FAR_CALL again, because the trampoline
; consumes both patched operands before its inner jsr and keeps the caller's
; bank/mode/map on the CPU stack. See _platform_far_call in src/banking.s for
; the full argument, and for the constraint that the callee must not touch
; $8000-$BFFF while it runs.
;
; Not safe against an interrupt that itself far-calls between the patch and
; the jsr below - the codebase contract is that interrupt code never
; bank-switches at all.
.macro FAR_CALL bank, target
    lda #(bank)
    sta far_call_bank_operand
    lda #<(target)
    sta far_call_target_operand
    lda #>(target)
    sta far_call_target_operand+1
    jsr _platform_far_call
.endmacro
