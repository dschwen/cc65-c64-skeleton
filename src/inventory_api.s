.setcpu "6502"
.macpack longbranch

.export _inventory_overlay_run_native
.export _inventory_overlay_validate_native
.export _inventory_validate_bss_bounds
.export _inventory_validate_checksum
.export _inventory_validate_invalid

.importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

INVENTORY_ENTRY = $a4e9
INVENTORY_VALIDATE_POST = $b9ca

.segment "MIDCODE"

; The overlay header starts with JMP inventory_overlay_run. Tail-calling that
; vector lets the C function's RTS return directly to the resident C caller.
_inventory_overlay_run_native:
    jmp INVENTORY_ENTRY

; fastcall: AX = loaded payload size. Validate the fixed overlay header,
; checksum its loadable payload, and clear the linked BSS. Returns a platform
; status in A. This is deliberately native: cc65's generated 16-bit bounds
; and checksum loops cost several hundred resident bytes.
.segment "STATEEXT"
_inventory_overlay_validate_native:
    sta tmp3
    stx tmp4

    ; 16 <= size <= $0ff0.
    cpx #$10
    bcs @state_invalid
    cpx #$00
    bne @check_max
    cmp #$10
    bcc @state_invalid
@check_max:
    cpx #$0f
    bcc @header
    cmp #$f1
    bcs @state_invalid

@header:
    lda INVENTORY_ENTRY
    cmp #$4c
    bne @state_invalid
    lda INVENTORY_ENTRY+3
    cmp #$49
    bne @state_invalid
    lda INVENTORY_ENTRY+4
    cmp #$55
    bne @state_invalid
    lda INVENTORY_ENTRY+5
    cmp #$01
    bne @state_invalid
    lda INVENTORY_ENTRY+6
    cmp tmp3
    bne @state_invalid
    lda INVENTORY_ENTRY+7
    cmp tmp4
    bne @state_invalid

    ; Entry target must lie in [base, base + size).
    lda INVENTORY_ENTRY+1
    sta ptr1
    lda INVENTORY_ENTRY+2
    sta ptr1+1
    cmp #>INVENTORY_ENTRY
    bcc @state_invalid
    jne INVENTORY_VALIDATE_POST
    lda ptr1
    cmp #<INVENTORY_ENTRY
    bcc @state_invalid
    jmp INVENTORY_VALIDATE_POST
@state_invalid:
    jmp _inventory_validate_invalid

.segment "STATEEXT"
_inventory_validate_bss_bounds:
    ; offset + BSS size must stay at or below $0ff0.
    clc
    lda INVENTORY_ENTRY+8
    adc INVENTORY_ENTRY+10
    sta ptr1
    lda INVENTORY_ENTRY+9
    adc INVENTORY_ENTRY+11
    sta ptr1+1
    cmp #$0f
    jcc _inventory_validate_checksum
    jne _inventory_validate_invalid
    lda ptr1
    cmp #$f1
    jcs _inventory_validate_invalid
    jmp _inventory_validate_checksum

.segment "UPPERCODE"
_inventory_validate_checksum:
    lda #<(INVENTORY_ENTRY+16)
    sta ptr1
    lda #>(INVENTORY_ENTRY+16)
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
    cmp INVENTORY_ENTRY+12
    jne _inventory_validate_invalid
    lda tmp2
    cmp INVENTORY_ENTRY+13
    jne _inventory_validate_invalid

    ; BSS pointer = base + relative offset; length = header BSS size.
    clc
    lda #<INVENTORY_ENTRY
    adc INVENTORY_ENTRY+8
    sta ptr1
    lda #>INVENTORY_ENTRY
    adc INVENTORY_ENTRY+9
    sta ptr1+1
    lda INVENTORY_ENTRY+10
    sta ptr2
    lda INVENTORY_ENTRY+11
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
_inventory_validate_invalid:
    lda #2
    ldx #0
    rts
