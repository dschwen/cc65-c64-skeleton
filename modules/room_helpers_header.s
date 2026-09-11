.setcpu "6502"

.import _room_helpers_banked_run

.segment "ENTRY"
    jmp _room_helpers_banked_run
