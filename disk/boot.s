.setcpu "6502"

.import __RELOC_LOAD__, __RELOC_RUN__, __RELOC_SIZE__

SETLFS = $ffba
SETNAM = $ffbd
LOAD   = $ffd5
DEVICE = $ba

.segment "LOADADDR"
    .word $0801

.segment "START"
    .word @basic_end
    .word 10
    .byte $9e
    .byte "2061", 0
@basic_end:
    .word 0

start:
    sei
    ldx #<(__RELOC_SIZE__-1)
@copy:
    lda __RELOC_LOAD__,x
    sta __RELOC_RUN__,x
    dex
    bpl @copy
    jmp __RELOC_RUN__

.segment "RELOC"
relocated:
    lda DEVICE
    bne :+
    lda #8
:
    sta @device+1

    lda #6
    ldx #<@engine
    ldy #>@engine
    jsr SETNAM
    lda #1
@device:
    ldx #8
    ldy #1
    jsr SETLFS
    lda #0
    jsr LOAD
    bcs @failed

    lda #4
    ldx #<@text
    ldy #>@text
    jsr SETNAM
    lda #1
    ldx @device+1
    ldy #1
    jsr SETLFS
    lda #0
    jsr LOAD
    bcs @failed
    jmp $080d

@failed:
    lda #2
    sta $d020
    jmp @failed

@engine: .byte "ENGINE"
@text:   .byte "TEXT"

.assert * - relocated < $100, error, "disk boot loader exceeds one page"
