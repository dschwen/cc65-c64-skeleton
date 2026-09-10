.setcpu "6502"

.import _saveload_overlay_run

.segment "LOADADDR"
    .word $b000

.segment "SAVELOADHEADER"
    jmp _saveload_overlay_run
    .byte $53, $4c             ; SL
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
