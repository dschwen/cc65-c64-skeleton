.setcpu "6502"

.import _look_helpers_banked_run

.segment "ENTRY"
    jmp _look_helpers_banked_run
