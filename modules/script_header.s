.setcpu "6502"

.import _script_overlay_run

.segment "ENTRY"
    jmp _script_overlay_run
