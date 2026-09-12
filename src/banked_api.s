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
.export _platform_saveload_run_banked
.export _platform_saveload_save_run_banked
.export _platform_disk_read_block
.export _platform_disk_write_block
.export _platform_disk_index_read_block
.export _platform_disk_index_write_block
.export _saveload_host_capture_current
.export _script_host_inventory_add
.export _script_host_lightning
.export _script_host_portrait_hide
.export _script_host_portrait_show
.export _script_host_transition_request
.export _script_host_transition_show_message

.import _game_inventory_add
.import _game_transition_request
.import _game_transition_show_message
.import _game_world_capture_current
.import _platform_disk_index_read_block_native
.import _platform_disk_index_write_block_native
.import _platform_disk_read_block_native
.import _platform_disk_write_block_native
.import _platform_lightning
.import _platform_portrait_hide
.import _platform_portrait_show

; Run resident RAM hidden by ROML/BASIC while preserving the outer banked
; caller. Public gates select a target in Y, then tail-call this
; shared dispatcher. A/X are saved while the descriptor is patched because
; they carry cc65's fastcall argument; the far-call trampoline returns
; directly to the original C caller and preserves its A/X result.
.segment "HIGHCODE"
host_ram_call:
    pha
    txa
    pha
    tya
    tax
    lda #0
    sta far_call_bank_operand
    lda host_target_lo,x
    sta far_call_target_operand
    lda host_target_hi,x
    sta far_call_target_operand+1
    lda host_cpu_map,x
    sta far_call_cpu_map_operand
    lda #EASYFLASH_OFF
    sta far_call_control_operand
    pla
    tax
    pla
    jmp _platform_far_call

host_target_lo:
    .byte <_platform_portrait_show
    .byte <_platform_portrait_hide
    .byte <_game_inventory_add
    .byte <_game_transition_request
    .byte <_game_transition_show_message
    .byte <_platform_lightning
    .byte <_game_world_capture_current
    .byte <_platform_disk_read_block_native
    .byte <_platform_disk_write_block_native
    .byte <_platform_disk_index_read_block_native
    .byte <_platform_disk_index_write_block_native
host_target_hi:
    .byte >_platform_portrait_show
    .byte >_platform_portrait_hide
    .byte >_game_inventory_add
    .byte >_game_transition_request
    .byte >_game_transition_show_message
    .byte >_platform_lightning
    .byte >_game_world_capture_current
    .byte >_platform_disk_read_block_native
    .byte >_platform_disk_write_block_native
    .byte >_platform_disk_index_read_block_native
    .byte >_platform_disk_index_write_block_native
host_cpu_map:
    .byte CPU_MAP_GAME, CPU_MAP_GAME, CPU_MAP_GAME, CPU_MAP_GAME
    .byte CPU_MAP_GAME, CPU_MAP_GAME, CPU_MAP_GAME
    .byte CPU_MAP_KERNAL, CPU_MAP_KERNAL, CPU_MAP_KERNAL, CPU_MAP_KERNAL

; Bank and CPU entry come from the same generated layout consumed by the
; packer. tools/validate_easyflash_layout.py additionally checks the linked
; ENTRY segment and final packed bytes.

; HIGHCODE is equally visible to both execution modes and has more margin than
; PROGRAM. Keeping these generated-mode stubs out of LOWCODE avoids spending
; the final bytes below $2000 merely to patch a far-call descriptor.
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
    jmp host_ram_call

_script_host_portrait_hide:
    ldy #1
    jmp host_ram_call

_script_host_inventory_add:
    ldy #2
    jmp host_ram_call

_script_host_transition_request:
    ldy #3
    jmp host_ram_call

_script_host_transition_show_message:
    ldy #4
    jmp host_ram_call

_script_host_lightning:
    ldy #5
    jmp host_ram_call

; Save/load UI services share bank 48 ROML. Disk calls and world capture use
; the explicit RAM gates below; no service code is copied to WORKBSS.
_platform_saveload_run_banked:
    FAR_CALL EF_LAYOUT_SAVELOAD_BANK, EF_LAYOUT_SAVELOAD_ENTRY, EF_LAYOUT_SAVELOAD_CPU_MAP, EF_LAYOUT_SAVELOAD_CONTROL
    rts

_platform_saveload_save_run_banked:
    FAR_CALL EF_LAYOUT_SAVELOAD_SAVE_BANK, EF_LAYOUT_SAVELOAD_SAVE_ENTRY, EF_LAYOUT_SAVELOAD_SAVE_CPU_MAP, EF_LAYOUT_SAVELOAD_SAVE_CONTROL
    rts

_saveload_host_capture_current:
    ldy #6
    jmp host_ram_call

_platform_disk_read_block:
    ldy #7
    jmp host_ram_call

_platform_disk_write_block:
    ldy #8
    jmp host_ram_call

_platform_disk_index_read_block:
    ldy #9
    jmp host_ram_call

_platform_disk_index_write_block:
    ldy #10
    jmp host_ram_call
