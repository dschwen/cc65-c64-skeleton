; Resident stubs for routines that live in EasyFlash banks and run in place.
;
; Each is an ordinary C-callable __fastcall__ entry point that forwards to its
; banked implementation through FAR_CALL, so callers need not know the routine
; moved. The trampoline passes A/X through and brings A/X back (see
; _platform_far_call in src/banking.s), which covers cc65's argument and return
; convention for these signatures.
.setcpu "6502"

.include "easyflash_layout.inc"
.include "platform.inc"

.import _platform_far_call
.import far_call_bank_operand
.import far_call_target_operand
.import far_call_cpu_map_operand
.import far_call_control_operand

.export _platform_object_type_info_get
.export _platform_inventory_run_banked
.export _platform_look_helpers_run_banked
.export _platform_room_helpers_run_banked
.export _platform_script_run_banked
.export _script_host_inventory_add
.export _script_host_lightning
.export _script_host_portrait_hide
.export _script_host_portrait_show
.export _script_host_transition_request
.export _script_host_transition_show_message

.import _game_inventory_add
.import _game_transition_request
.import _game_transition_show_message
.import _platform_lightning
.import _platform_portrait_hide
.import _platform_portrait_show

; Run resident RAM hidden by ROML/BASIC while preserving the outer banked
; caller. The six public gates select a target in Y, then tail-call this
; shared dispatcher. A/X are saved while the descriptor is patched because
; they carry cc65's fastcall argument; the far-call trampoline returns
; directly to the original C caller and preserves its A/X result.
script_host_ram_call:
    pha
    txa
    pha
    tya
    tax
    lda #0
    sta far_call_bank_operand
    lda script_host_target_lo,x
    sta far_call_target_operand
    lda script_host_target_hi,x
    sta far_call_target_operand+1
    lda #CPU_MAP_GAME
    sta far_call_cpu_map_operand
    lda #EASYFLASH_OFF
    sta far_call_control_operand
    pla
    tax
    pla
    jmp _platform_far_call

script_host_target_lo:
    .byte <_platform_portrait_show
    .byte <_platform_portrait_hide
    .byte <_game_inventory_add
    .byte <_game_transition_request
    .byte <_game_transition_show_message
    .byte <_platform_lightning
script_host_target_hi:
    .byte >_platform_portrait_show
    .byte >_platform_portrait_hide
    .byte >_game_inventory_add
    .byte >_game_transition_request
    .byte >_game_transition_show_message
    .byte >_platform_lightning

; Bank and CPU entry come from the same generated layout consumed by the
; packer. tools/validate_easyflash_layout.py additionally checks the linked
; ENTRY segment and final packed bytes.

; HIGHCODE is equally visible to both execution modes and has more margin than
; PROGRAM. Keeping these generated-mode stubs out of LOWCODE avoids spending
; the final bytes below $2000 merely to patch a far-call descriptor.
.segment "HIGHCODE"

; const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id)
; fastcall: type_id in A, returns the scratch record's address in A/X.
_platform_object_type_info_get:
    FAR_CALL EF_LAYOUT_TYPEINFO_BANK, EF_LAYOUT_TYPEINFO_ENTRY, EF_LAYOUT_TYPEINFO_CPU_MAP, EF_LAYOUT_TYPEINFO_CONTROL
    rts

; void platform_inventory_run_banked(void)
; Inventory is a complete bank-local service: it retains control until the
; user closes the screen, while the resident raster IRQ continues to run.
_platform_inventory_run_banked:
    FAR_CALL EF_LAYOUT_INVENTORY_BANK, EF_LAYOUT_INVENTORY_ENTRY, EF_LAYOUT_INVENTORY_CPU_MAP, EF_LAYOUT_INVENTORY_CONTROL
    rts

; void platform_room_helpers_run_banked(void)
; Parameters and result live in low resident DATA; see src/platform.c.
_platform_room_helpers_run_banked:
    FAR_CALL EF_LAYOUT_ROOM_HELPERS_BANK, EF_LAYOUT_ROOM_HELPERS_ENTRY, EF_LAYOUT_ROOM_HELPERS_CPU_MAP, EF_LAYOUT_ROOM_HELPERS_CONTROL
    rts

; void platform_look_helpers_run_banked(void)
; Parameters and room-staging workspace pointer live in low resident DATA.
_platform_look_helpers_run_banked:
    FAR_CALL EF_LAYOUT_LOOK_HELPERS_BANK, EF_LAYOUT_LOOK_HELPERS_ENTRY, EF_LAYOUT_LOOK_HELPERS_CPU_MAP, EF_LAYOUT_LOOK_HELPERS_CONTROL
    rts

; Script interpreter and its explicit host-action boundary.  The interpreter
; itself stays mapped; only actions whose code/data closure is not bank-safe
; pass through RAM_CALL.
_platform_script_run_banked:
    FAR_CALL EF_LAYOUT_SCRIPT_BANK, EF_LAYOUT_SCRIPT_ENTRY, EF_LAYOUT_SCRIPT_CPU_MAP, EF_LAYOUT_SCRIPT_CONTROL
    rts

_script_host_portrait_show:
    ldy #0
    jmp script_host_ram_call

_script_host_portrait_hide:
    ldy #1
    jmp script_host_ram_call

_script_host_inventory_add:
    ldy #2
    jmp script_host_ram_call

_script_host_transition_request:
    ldy #3
    jmp script_host_ram_call

_script_host_transition_show_message:
    ldy #4
    jmp script_host_ram_call

_script_host_lightning:
    ldy #5
    jmp script_host_ram_call
