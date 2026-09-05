.setcpu "6502"

.import _look_helpers_overlay_run

.segment "LOADADDR"
    .word $a4e9

.segment "LOOKHELPERSHEADER"
    jmp _look_helpers_overlay_run
    .byte $4c, $48             ; LH
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
