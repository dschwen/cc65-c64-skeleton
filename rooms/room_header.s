.setcpu "6502"

.ifndef ROOM_ID
    .error "ROOM_ID must be defined"
.endif

.import _enter_tile
.import _look_at

.segment "ROOMHEADER"
    jmp _enter_tile
    jmp _look_at
    .byte $52, $43             ; RC
    .byte 2, ROOM_ID           ; ABI, room ID
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    .word 0                    ; reserved
