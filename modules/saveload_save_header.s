.setcpu "6502"

.import _saveload_save_overlay_run

.segment "LOADADDR"
    .word $a4e9

.segment "SAVELOADSAVEHEADER"
    jmp _saveload_save_overlay_run
    .byte $53, $56             ; SV
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
