; Resident stubs for routines that live in EasyFlash banks and run in place.
;
; Each is an ordinary C-callable __fastcall__ entry point that forwards to its
; banked implementation through FAR_CALL, so callers need not know the routine
; moved. The trampoline passes A/X through and brings A/X back (see
; _platform_far_call in src/banking.s), which covers cc65's argument and return
; convention for these signatures.
.setcpu "6502"

.include "platform.inc"

.import _platform_far_call
.import far_call_bank_operand
.import far_call_target_operand

.export _platform_object_type_info_get

; Bank and entry address must match tools/pack_easyflash.py's
; TYPEINFO_BANK/TYPEINFO_OFFSET and cfg/banked_typeinfo.cfg's BANKED start:
; CPU address = $8000 + offset within the bank's ROML half.
TYPEINFO_BANK  = 47
TYPEINFO_ENTRY = $9A00

.segment "LOWCODE"

; const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id)
; fastcall: type_id in A, returns the scratch record's address in A/X.
_platform_object_type_info_get:
    FAR_CALL TYPEINFO_BANK, TYPEINFO_ENTRY
    rts
