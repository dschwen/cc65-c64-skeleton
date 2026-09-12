.setcpu "6502"

.import _saveload_save_banked_run

.segment "ENTRY"
    jmp _saveload_save_banked_run
