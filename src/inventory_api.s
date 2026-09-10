.setcpu "6502"
.macpack longbranch

; Generic loaded-overlay entry/validator, shared by every overlay that uses
; the 16-byte header/ABI convention at $B000 (save/load and helper overlays):
; JMP vector, 2-byte magic, ABI byte, size/BSS-offset/BSS-size/checksum
; words. platform_overlay_magic0/1 (src/platform.c) select which overlay's
; magic is expected; each caller sets them before loading.

.export _platform_overlay_run_native
.export _platform_overlay_validate_native
.export _platform_overlay_validate_bss_bounds
.export _platform_overlay_validate_checksum
.export _platform_overlay_validate_invalid

.import _platform_overlay_magic0
.import _platform_overlay_magic1

.importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

OVERLAY_ENTRY = $b000
OVERLAY_VALIDATE_POST = $3bbd

.segment "MIDCODE"

; The overlay header starts with JMP <overlay entry>. Tail-calling that
; vector lets the loaded C function's RTS return directly to the resident
; C caller.
_platform_overlay_run_native:
    jmp OVERLAY_ENTRY

; fastcall: AX = loaded payload size. Validate the fixed overlay header,
; checksum its loadable payload, and clear the linked BSS. Returns a platform
; status in A. This is deliberately native: cc65's generated 16-bit bounds
; and checksum loops cost several hundred resident bytes.
.segment "STATEEXT"
_platform_overlay_validate_native:
    sta tmp3
    stx tmp4

    ; 16 <= size <= $1000.
    cpx #$11
    bcs @state_invalid
    cpx #$00
    bne @check_max
    cmp #$10
    bcc @state_invalid
@check_max:
    cpx #$10
    bcc @header
    bne @state_invalid
    cmp #$01
    bcs @state_invalid

@header:
    lda OVERLAY_ENTRY
    cmp #$4c
    bne @state_invalid
    lda OVERLAY_ENTRY+3
    cmp _platform_overlay_magic0
    bne @state_invalid
    lda OVERLAY_ENTRY+4
    cmp _platform_overlay_magic1
    bne @state_invalid
    lda OVERLAY_ENTRY+5
    cmp #$01
    bne @state_invalid
    lda OVERLAY_ENTRY+6
    cmp tmp3
    bne @state_invalid
    lda OVERLAY_ENTRY+7
    cmp tmp4
    bne @state_invalid

    ; Entry target must lie in [base, base + size).
    lda OVERLAY_ENTRY+1
    sta ptr1
    lda OVERLAY_ENTRY+2
    sta ptr1+1
    cmp #>OVERLAY_ENTRY
    bcc @state_invalid
    jne OVERLAY_VALIDATE_POST
    lda ptr1
    cmp #<OVERLAY_ENTRY
    bcc @state_invalid
    jmp OVERLAY_VALIDATE_POST
@state_invalid:
    jmp _platform_overlay_validate_invalid

.segment "STATEEXT"
_platform_overlay_validate_bss_bounds:
    ; offset + BSS size must stay at or below $1000.
    clc
    lda OVERLAY_ENTRY+8
    adc OVERLAY_ENTRY+10
    sta ptr1
    lda OVERLAY_ENTRY+9
    adc OVERLAY_ENTRY+11
    sta ptr1+1
    cmp #$10
    jcc _platform_overlay_validate_checksum
    jne _platform_overlay_validate_invalid
    lda ptr1
    jne _platform_overlay_validate_invalid
    jmp _platform_overlay_validate_checksum

.segment "UPPERCODE"
_platform_overlay_validate_checksum:
    lda #<(OVERLAY_ENTRY+16)
    sta ptr1
    lda #>(OVERLAY_ENTRY+16)
    sta ptr1+1
    sec
    lda tmp3
    sbc #16
    sta ptr2
    lda tmp4
    sbc #0
    sta ptr2+1
    lda #0
    sta tmp1
    sta tmp2
@checksum_next:
    lda ptr2
    ora ptr2+1
    beq @checksum_done
    ldy #0
    clc
    lda tmp1
    adc (ptr1),y
    sta tmp1
    bcc :+
    inc tmp2
:
    inc ptr1
    bne :+
    inc ptr1+1
:
    lda ptr2
    bne :+
    dec ptr2+1
:
    dec ptr2
    jmp @checksum_next

@checksum_done:
    lda tmp1
    cmp OVERLAY_ENTRY+12
    jne _platform_overlay_validate_invalid
    lda tmp2
    cmp OVERLAY_ENTRY+13
    jne _platform_overlay_validate_invalid

    ; BSS pointer = base + relative offset; length = header BSS size.
    clc
    lda #<OVERLAY_ENTRY
    adc OVERLAY_ENTRY+8
    sta ptr1
    lda #>OVERLAY_ENTRY
    adc OVERLAY_ENTRY+9
    sta ptr1+1
    lda OVERLAY_ENTRY+10
    sta ptr2
    lda OVERLAY_ENTRY+11
    sta ptr2+1
    lda #0
@clear_next:
    ldx ptr2
    bne @clear_byte
    ldx ptr2+1
    beq @valid
@clear_byte:
    ldy #0
    sta (ptr1),y
    inc ptr1
    bne :+
    inc ptr1+1
:
    ldx ptr2
    bne :+
    dec ptr2+1
:
    dec ptr2
    jmp @clear_next

@valid:
    lda #0
    tax
    rts
_platform_overlay_validate_invalid:
    lda #2
    ldx #0
    rts
