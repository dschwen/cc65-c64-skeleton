; EasyFlash bank 0 bootstrap.
;
; The loader supports both EasyFlash's Ultimax reset path and the standard
; KERNAL CBM80 cartridge detection path. It copies the regular PRG payload
; into RAM, disables the cartridge, then enters the normal cc65 startup.

.setcpu "6502"

EASYFLASH_BANK    = $de00
EASYFLASH_CONTROL = $de02
EASYFLASH_16K     = $07
EASYFLASH_OFF     = $04
CART_MARKER       = $033c

IOINIT = $ff84
RAMTAS = $ff87
RESTOR = $ff8a
CINT   = $ff81

COPY_SRC = $fb
COPY_DST = $fd
COPY_LEN = $f9
COPY_MORE_BANKS = $f8
PRG_START = $0801
CC65_START = $080d
TRAMPOLINE = $0400
BANK_STAGE = $c000
PAYLOAD0_BYTES = $3d00
PAYLOAD1_BYTES = $4000

.segment "CART_HEADER"
    .word cold_start
    .word cold_start
    .byte $c3, $c2, $cd, $38, $30 ; "CBM80" in PETSCII

.segment "BOOT"
cold_start:
    sei
    cld
    ldx #$ff
    txs

    ; Establish the normal C64 CPU port mapping before calling the KERNAL.
    ; Write the data register before enabling its output bits, as recommended
    ; by the EasyFlash boot sequence.
    lda #$37
    sta $01
    lda #$2f
    sta $00

    lda #0
    sta EASYFLASH_BANK
    lda #EASYFLASH_16K
    sta EASYFLASH_CONTROL

    ; Establish the same machine state expected by a normally loaded PRG.
    jsr IOINIT
    jsr RAMTAS
    jsr RESTOR
    jsr CINT

    lda #<prg_payload0
    sta COPY_SRC
    lda #>prg_payload0
    sta COPY_SRC+1
    lda #<PRG_START
    sta COPY_DST
    lda #>PRG_START
    sta COPY_DST+1

    lda #<PAYLOAD0_SIZE
    sta COPY_LEN
    lda #>PAYLOAD0_SIZE
    sta COPY_LEN+1
    jsr copy_block

    ldx #BANK_STAGE_SIZE
@copy_bank_stage:
    lda bank1_stage-1,x
    sta BANK_STAGE-1,x
    dex
    bne @copy_bank_stage
    jmp BANK_STAGE

; This block is copied to $C000. It must use only relative internal branches
; and absolute references outside itself, because its linked ROM address and
; execution address differ.
bank1_stage:
    lda #2
    sta COPY_MORE_BANKS
    lda #1
    sta EASYFLASH_BANK
    lda #<prg_payload1
    sta COPY_SRC
    lda #>prg_payload1
    sta COPY_SRC+1
    lda #<(PRG_START+PAYLOAD0_BYTES)
    sta COPY_DST
    lda #>(PRG_START+PAYLOAD0_BYTES)
    sta COPY_DST+1
    lda #<PAYLOAD1_SIZE
    sta COPY_LEN
    lda #>PAYLOAD1_SIZE
    sta COPY_LEN+1

    lda COPY_LEN
    ora COPY_LEN+1
    beq @stage_copy_done
    ldy #0
@stage_copy_byte:
    lda (COPY_SRC),y
    sta (COPY_DST),y
    inc COPY_SRC
    bne @stage_source_advanced
    inc COPY_SRC+1
@stage_source_advanced:
    inc COPY_DST
    bne @stage_destination_advanced
    inc COPY_DST+1
@stage_destination_advanced:
    lda COPY_LEN
    bne @stage_decrement_low
    dec COPY_LEN+1
@stage_decrement_low:
    dec COPY_LEN
    lda COPY_LEN
    ora COPY_LEN+1
    bne @stage_copy_byte

@stage_copy_done:
    dec COPY_MORE_BANKS
    bmi @all_payload_copied
    beq @copy_text_module

    lda #2
    sta EASYFLASH_BANK
    lda #<prg_payload2
    sta COPY_SRC
    lda #>prg_payload2
    sta COPY_SRC+1
    lda #<(PRG_START+PAYLOAD0_BYTES+PAYLOAD1_BYTES)
    sta COPY_DST
    lda #>(PRG_START+PAYLOAD0_BYTES+PAYLOAD1_BYTES)
    sta COPY_DST+1
    lda #<PAYLOAD2_SIZE
    sta COPY_LEN
    lda #>PAYLOAD2_SIZE
    sta COPY_LEN+1
    bne @stage_copy_byte

@copy_text_module:
    lda #2
    sta EASYFLASH_BANK
    lda #<text_module
    sta COPY_SRC
    lda #>text_module
    sta COPY_SRC+1
    lda #<$b880
    sta COPY_DST
    lda #>$b880
    sta COPY_DST+1
    lda #<TEXT_MODULE_SIZE
    sta COPY_LEN
    lda #>TEXT_MODULE_SIZE
    sta COPY_LEN+1
    bne @stage_copy_byte

@all_payload_copied:
    ; Disabling the cartridge must execute from RAM because ROML disappears
    ; as soon as $DE02 is written.
    lda #$a9                ; lda #$EF
    sta TRAMPOLINE
    lda #$ef
    sta TRAMPOLINE+1
    lda #$8d                ; sta CART_MARKER
    sta TRAMPOLINE+2
    lda #<CART_MARKER
    sta TRAMPOLINE+3
    lda #>CART_MARKER
    sta TRAMPOLINE+4
    lda #$a9                ; lda #$64
    sta TRAMPOLINE+5
    lda #$64
    sta TRAMPOLINE+6
    lda #$8d                ; sta CART_MARKER+1
    sta TRAMPOLINE+7
    lda #<(CART_MARKER+1)
    sta TRAMPOLINE+8
    lda #>(CART_MARKER+1)
    sta TRAMPOLINE+9
    lda #$a9                ; lda #EASYFLASH_OFF
    sta TRAMPOLINE+10
    lda #EASYFLASH_OFF
    sta TRAMPOLINE+11
    lda #$8d                ; sta EASYFLASH_CONTROL
    sta TRAMPOLINE+12
    lda #<EASYFLASH_CONTROL
    sta TRAMPOLINE+13
    lda #>EASYFLASH_CONTROL
    sta TRAMPOLINE+14
    lda #$4c                ; jmp CC65_START
    sta TRAMPOLINE+15
    lda #<CC65_START
    sta TRAMPOLINE+16
    lda #>CC65_START
    sta TRAMPOLINE+17
    jmp TRAMPOLINE
bank1_stage_end:

BANK_STAGE_SIZE = bank1_stage_end - bank1_stage

.assert BANK_STAGE_SIZE < 256, error, "bank 1 RAM stage exceeds one page"

copy_block:
    lda COPY_LEN
    ora COPY_LEN+1
    beq @copy_done
    ldy #0
@copy_byte:
    lda (COPY_SRC),y
    sta (COPY_DST),y
    inc COPY_SRC
    bne @source_advanced
    inc COPY_SRC+1
@source_advanced:
    inc COPY_DST
    bne @destination_advanced
    inc COPY_DST+1
@destination_advanced:
    lda COPY_LEN
    bne @decrement_low
    dec COPY_LEN+1
@decrement_low:
    dec COPY_LEN
    lda COPY_LEN
    ora COPY_LEN+1
    bne @copy_byte

@copy_done:
    rts

.segment "PAYLOAD0"
prg_payload0:
    .incbin "build/game.prg", 2, PAYLOAD0_BYTES
prg_payload0_end:

PAYLOAD0_SIZE = prg_payload0_end - prg_payload0

.segment "PAYLOAD1"
prg_payload1:
    .incbin "build/game.prg", 2 + PAYLOAD0_BYTES, PAYLOAD1_BYTES
prg_payload1_end:

PAYLOAD1_SIZE = prg_payload1_end - prg_payload1

.segment "PAYLOAD2"
prg_payload2:
    .incbin "build/game.prg", 2 + PAYLOAD0_BYTES + PAYLOAD1_BYTES
prg_payload2_end:

PAYLOAD2_SIZE = prg_payload2_end - prg_payload2

.segment "TEXTMODULE"
text_module:
    .incbin "build/text.prg", 2
text_module_end:

TEXT_MODULE_SIZE = text_module_end - text_module

.assert PAYLOAD0_SIZE = PAYLOAD0_BYTES, error, "PRG payload is too small"
.assert PAYLOAD1_SIZE = PAYLOAD1_BYTES, error, "second payload bank is incomplete"
.assert PAYLOAD2_SIZE > 0, error, "third payload bank is empty"
.assert prg_payload2_end <= $c000, error, "PRG payload exceeds three banks"
.assert TEXT_MODULE_SIZE <= $0180, error, "text module exceeds reserved RAM"

.segment "VECTORS"
    .word cold_start        ; NMI at $FFFA in Ultimax mode
    .word cold_start        ; RESET at $FFFC in Ultimax mode
    .word cold_start        ; IRQ at $FFFE in Ultimax mode
