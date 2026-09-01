.setcpu "6502"

.include "platform.inc"

.export _platform_memory_game
.export _platform_memory_kernal
.export _platform_memory_all_ram
.export _platform_easyflash_enable
.export _platform_easyflash_disable
.export _platform_easyflash_copy_roml
.export _platform_easyflash_copy_romh
.export _platform_ef_copy_bank
.export _platform_ef_copy_offset
.export _platform_ef_copy_destination
.export _platform_ef_copy_size
.export _platform_irq_save_disable
.export _platform_irq_restore
.export _platform_object_types_clear
.export _platform_boot_is_easyflash

.export ef_push_switch, ef_pop_restore

.import _raster_irq_resync

.segment "DATA"
_platform_ef_copy_bank:
    .byte 0
_platform_ef_copy_offset:
    .word 0
_platform_ef_copy_destination:
    .word 0
_platform_ef_copy_size:
    .word 0

; EASYFLASH_BANK is write-only on real hardware (no readback), so a nested
; bank switch needs a readable shadow to know what to restore. Initialized
; to EASYFLASH_OFF (matching what cart/ef_boot.s leaves it as) by
; _platform_boot_is_easyflash below, before anything can call ef_push_switch.
;
; Deliberately in "DATA", NOT "BSS": BSSRAM ($B500-$B80C, see cfg/myc64.cfg)
; sits inside the $8000-$BFFF EasyFlash ROML/ROMH banking window. Writes
; there always land in the underlying RAM regardless of banking (true on
; real 6510 hardware), but ef_push_switch's *reads* of these shadow bytes
; happen while the caller (easyflash_copy_window) has already switched
; CPU_PORT to bank the cartridge ROM in for reading - so a BSS-resident
; shadow byte would read back stale flash content instead of the value we
; stored, not the RAM byte. This was a real, since-fixed bug: ef_shadow_bank
; came back as garbage on its very first read (traced live in VICE - $7B
; instead of the $00 confirmed present in RAM moments earlier). DATA loads
; into PROGRAM, well below $8000, so it stays readable as ordinary RAM no
; matter what's banked in above it.
.segment "DATA"
ef_shadow_bank:
    .byte 0
ef_shadow_control:
    .byte 0
ef_new_bank_tmp:
    .byte 0

.segment "LOWCODE"

; All public mapping functions preserve the interrupt flag and the cassette
; control bits in the upper part of the 6510 port.
.macro SET_MAP value
    php
    sei
    lda CPU_DDR
    ora #CPU_PORT_MASK
    sta CPU_DDR
    lda CPU_PORT
    and #$f8
    ora #value
    sta CPU_PORT
    plp
    rts
.endmacro

_platform_memory_game:
    SET_MAP CPU_MAP_GAME

_platform_memory_kernal:
    SET_MAP CPU_MAP_KERNAL

_platform_memory_all_ram:
    SET_MAP CPU_MAP_ALL_RAM

; fastcall: A = EasyFlash bank. The gameplay map keeps I/O visible.
; Unused by the current codebase (kept as public API); doesn't update
; ef_shadow_bank/control, so don't mix calls to this with ef_push_switch's
; nesting until it does.
_platform_easyflash_enable:
    tax
    php
    sei
    lda CPU_PORT
    and #$f8
    ora #CPU_MAP_CART_8K
    sta CPU_PORT
    txa
    sta EASYFLASH_BANK
    lda #EASYFLASH_8K
    sta EASYFLASH_CONTROL
    plp
    rts

_platform_easyflash_disable:
    php
    sei
    lda #EASYFLASH_OFF
    sta EASYFLASH_CONTROL
    lda CPU_PORT
    and #$f8
    ora #CPU_MAP_GAME
    sta CPU_PORT
    plp
    rts

.segment "HIGHCODE"

; A = new EasyFlash bank, X = new mode (EASYFLASH_8K or EASYFLASH_16K).
; Returns the PREVIOUS bank in A and previous mode in Y - the caller is
; responsible for holding onto them (e.g. in zero page, or by not
; clobbering A/Y across its own copy loop) and passing them back to
; ef_pop_restore, in A/Y, when unwinding. This makes nesting to any depth
; the caller cares to arrange for safe: unlike an earlier version of this
; routine, nothing is pushed onto the 6502 hardware stack across the call
; boundary. A jsr/rts pair only stays balanced if nothing extra is left on
; the stack in between - pushing the previous bank/mode there desynced
; ef_push_switch's own rts, which ended up popping the just-pushed shadow
; byte as part of a bogus return address instead of the real one (traced
; live in VICE: execution landed at a garbage address after the rts). The
; pha/pla below is balanced within this one call, so it's safe.
; Does NOT touch CPU_PORT (whether the cart is mapped in at $8000-$9FFF at
; all) - callers that need that changed too (copy_roml/romh do, since they
; run from ordinary gameplay mapping) save/restore it themselves. Does NOT
; disable interrupts itself either: the caller must keep interrupts off for
; the whole push...pop window (copy_roml/romh already wrap themselves in
; their own php/sei/plp).
ef_push_switch:
    sta ef_new_bank_tmp
    lda ef_shadow_bank
    pha
    lda ef_shadow_control
    tay
    lda ef_new_bank_tmp
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    stx ef_shadow_control
    stx EASYFLASH_CONTROL
    pla
    rts

; Undoes an ef_push_switch. A = bank to restore (as returned by
; ef_push_switch in A), Y = mode to restore (as returned in Y).
ef_pop_restore:
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    sty ef_shadow_control
    sty EASYFLASH_CONTROL
    rts

; Copy ROML/ROMH into underlying RAM without touching C stack or BSS while the
; cartridge mapping hides $8000-$BFFF.
_platform_easyflash_copy_roml:
    lda #$80
    bne easyflash_copy_window

_platform_easyflash_copy_romh:
    lda #$a0
easyflash_copy_window:
    sta $f8
    php
    sei
    lda CPU_PORT
    pha
    and #$f8
    ora #CPU_MAP_CART_16K
    sta CPU_PORT

    lda _platform_ef_copy_offset
    sta $fb
    lda _platform_ef_copy_offset+1
    clc
    adc $f8
    sta $fc
    lda _platform_ef_copy_destination
    sta $fd
    lda _platform_ef_copy_destination+1
    sta $fe

    lda _platform_ef_copy_bank
    ldx #EASYFLASH_16K
    jsr ef_push_switch
    sta $f8
    sty $f9

    lda _platform_ef_copy_size
    ora _platform_ef_copy_size+1
    beq @romh_done
@romh_byte:
    ldy #0
    lda ($fb),y
    sta ($fd),y
    inc $fb
    bne @romh_source_ok
    inc $fc
@romh_source_ok:
    inc $fd
    bne @romh_destination_ok
    inc $fe
@romh_destination_ok:
    lda _platform_ef_copy_size
    bne @romh_decrement_low
    dec _platform_ef_copy_size+1
@romh_decrement_low:
    dec _platform_ef_copy_size
    lda _platform_ef_copy_size
    ora _platform_ef_copy_size+1
    bne @romh_byte

@romh_done:
    lda $f8
    ldy $f9
    jsr ef_pop_restore
    pla
    sta CPU_PORT
    jsr _raster_irq_resync
    plp
    rts

.segment "LOWCODE"

; Return the previous status byte in A, then leave IRQs disabled.
_platform_irq_save_disable:
    php
    pla
    sei
    rts

; fastcall: restore a status byte returned by platform_irq_save_disable().
_platform_irq_restore:
    pha
    plp
    rts

; Called once, first thing, from platform_init(): establishes the shadow's
; initial value to match what cart/ef_boot.s leaves EASYFLASH_CONTROL as,
; before anything can call ef_push_switch/ef_pop_restore or the copy
; routines that now use them.
_platform_boot_is_easyflash:
    lda #EASYFLASH_OFF
    sta ef_shadow_control
    lda CART_MARKER
    cmp #CART_MARKER_0
    bne @not_cartridge
    lda CART_MARKER+1
    cmp #CART_MARKER_1
    bne @not_cartridge
    lda #1
    bne @clear_marker
@not_cartridge:
    lda #0
@clear_marker:
    ldx #0
    stx CART_MARKER
    stx CART_MARKER+1
    ldx #0
    rts

; Clear exactly $C000-$FFFF with I/O/KERNAL hidden for the complete operation.
_platform_object_types_clear:
    php
    sei
    lda CPU_PORT
    pha
    and #$f8
    ora #CPU_MAP_ALL_RAM
    sta CPU_PORT
    lda #$00
    sta $fb
    lda #$c0
    sta $fc
    ldx #64
    lda #0
@clear_page:
    ldy #0
@clear_byte:
    sta ($fb),y
    iny
    bne @clear_byte
    inc $fc
    dex
    bne @clear_page
    pla
    sta CPU_PORT
    plp
    rts

