.setcpu "6502"
.macpack longbranch

.export _inventory_validate_post

.import _platform_overlay_validate_bss_bounds
.import _platform_overlay_validate_invalid
.importzp ptr1, ptr2, tmp3, tmp4

OVERLAY_ENTRY = $a4e9

.segment "TEXTPOST"

; Continuation of the resident, generic loaded-overlay validator shared by
; every $A4E9 overlay (inventory/story, save/load). This small routine lives
; in the always-loaded pre-text module because the main PRG's high regions
; are full. The state and checksum continuations remain resident entry
; points (src/inventory_api.s).
_inventory_validate_post:
    clc
    lda #<OVERLAY_ENTRY
    adc tmp3
    sta ptr2
    lda #>OVERLAY_ENTRY
    adc tmp4
    sta ptr2+1
    lda ptr1+1
    cmp ptr2+1
    bcc @bss_offset
    jne _platform_overlay_validate_invalid
    lda ptr1
    cmp ptr2
    jcs _platform_overlay_validate_invalid

@bss_offset:
    lda OVERLAY_ENTRY+8
    cmp tmp3
    lda OVERLAY_ENTRY+9
    sbc tmp4
    jcc _platform_overlay_validate_invalid
    jmp _platform_overlay_validate_bss_bounds
