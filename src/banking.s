.setcpu "6502"

.include "platform.inc"

.export _platform_memory_game
.export _platform_memory_kernal
.export _platform_memory_all_ram
.export _platform_object_type_stage
.export _platform_easyflash_enable
.export _platform_easyflash_disable
.export _platform_irq_save_disable
.export _platform_irq_restore
.export _platform_object_types_clear
.export _platform_boot_is_easyflash

.import _platform_object_type_scratch

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
    ora #CPU_MAP_CART
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

; fastcall: A = type ID in the $D000-$DFFF quarter (64-127). Copy its
; 64-byte record to always-visible scratch while I/O and IRQs are hidden.
_platform_object_type_stage:
    sec
    sbc #64
    pha
    and #$03
    tax
    lda stage_low,x
    sta @read+1
    pla
    lsr
    lsr
    clc
    adc #$d0
    sta @read+2

    php
    sei
    lda CPU_PORT
    pha
    and #$f8
    ora #CPU_MAP_ALL_RAM
    sta CPU_PORT
    ldx #0
@copy:
@read:
    lda $d000,x
    sta _platform_object_type_scratch,x
    inx
    cpx #OBJECT_TYPE_SIZE
    bne @copy
    pla
    sta CPU_PORT
    plp
    rts

.segment "RODATA"
stage_low:
    .byte $00, $40, $80, $c0
