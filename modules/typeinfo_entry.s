; Entry vector for the banked object-type-info module. FAR_CALL targets this
; fixed address, so the module can be relinked freely without every call site
; having to know where the function itself landed - the same reason the loaded
; overlays start with a jump table.
.setcpu "6502"
.import _platform_object_type_info_banked
.segment "ENTRY"
    jmp _platform_object_type_info_banked
