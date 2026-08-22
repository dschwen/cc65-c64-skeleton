.setcpu "6502"

.ifndef ROOM_ID
    .error "ROOM_ID must be defined"
.endif

.import _enter_tile
.import _look_at
.import _enter_room
.import _use_at

.segment "ROOMHEADER"
    jmp _enter_tile
    jmp _look_at
    jmp _enter_room
    .byte $52, $43             ; RC
    .byte 4, ROOM_ID           ; ABI, room ID
    .word 0                    ; patched load size
    .word 0                    ; patched BSS offset
    .word 0                    ; patched BSS size
    .word 0                    ; patched payload checksum
    jmp _use_at
