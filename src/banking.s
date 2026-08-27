.setcpu "6502"

.include "platform.inc"

.export _platform_memory_game
.export _platform_memory_kernal
.export _platform_memory_all_ram
.export _platform_easyflash_enable
.export _platform_easyflash_enable_16
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

; fastcall: A = EasyFlash bank, exposing both ROML and ROMH.
_platform_easyflash_enable_16:
    tax
    php
    sei
    lda CPU_PORT
    and #$f8
    ora #CPU_MAP_CART_16K
    sta CPU_PORT
    txa
    sta EASYFLASH_BANK
    lda #EASYFLASH_16K
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

; Copy ROML/ROMH into underlying RAM without touching C stack or BSS while the
; cartridge mapping hides $8000-$BFFF.
.segment "HIGHCODE"
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
    sta EASYFLASH_BANK
    lda #EASYFLASH_16K
    sta EASYFLASH_CONTROL

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
    lda #EASYFLASH_OFF
    sta EASYFLASH_CONTROL
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

_platform_boot_is_easyflash:
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

