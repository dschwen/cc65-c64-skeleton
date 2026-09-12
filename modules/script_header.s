.setcpu "6502"

.import _script_banked_run

.segment "ENTRY"
    jmp _script_banked_run
