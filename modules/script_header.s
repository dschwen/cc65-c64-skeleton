.setcpu "6502"

.import _script_overlay_run

.segment "LOADADDR"
    .word $b000

.segment "SCRIPTHEADER"
    jmp _script_overlay_run
    .byte $53, $43             ; SC
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
