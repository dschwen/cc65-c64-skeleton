.setcpu "6502"

.import _room_helpers_overlay_run

.segment "LOADADDR"
    .word $a4e9

.segment "ROOMHELPERSHEADER"
    jmp _room_helpers_overlay_run
    .byte $52, $48             ; RH
    .byte 1                    ; ABI version
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
