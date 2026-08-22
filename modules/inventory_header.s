.setcpu "6502"

.import _inventory_overlay_run

.segment "LOADADDR"
    .word $a4e9

.segment "INVENTORYHEADER"
    jmp _inventory_overlay_run
    .byte $49, $55             ; IU
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
