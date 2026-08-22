.setcpu "6502"
.macpack longbranch

.export _inventory_validate_post

.import _inventory_validate_bss_bounds
.import _inventory_validate_invalid
.importzp ptr1, ptr2, tmp3, tmp4

INVENTORY_ENTRY = $a4e9

.segment "TEXTPOST"

; Continuation of the resident overlay validator. This small routine lives in
; the always-loaded pre-text module because the main PRG's high regions are
; full. The state and checksum continuations remain resident entry points.
_inventory_validate_post:
    clc
    lda #<INVENTORY_ENTRY
    adc tmp3
    sta ptr2
    lda #>INVENTORY_ENTRY
    adc tmp4
    sta ptr2+1
    lda ptr1+1
    cmp ptr2+1
    bcc @bss_offset
    jne _inventory_validate_invalid
    lda ptr1
    cmp ptr2
    jcs _inventory_validate_invalid

@bss_offset:
    lda INVENTORY_ENTRY+8
    cmp tmp3
    lda INVENTORY_ENTRY+9
    sbc tmp4
    jcc _inventory_validate_invalid
    jmp _inventory_validate_bss_bounds
