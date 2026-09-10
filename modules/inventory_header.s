.setcpu "6502"

.import _inventory_banked_run

.segment "ENTRY"
    jmp _inventory_banked_run
