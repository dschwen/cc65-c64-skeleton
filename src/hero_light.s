; platform_hero_light_set() - hand-written because cc65 insists on pushing
; its single uint8_t argument onto the software stack and reading it back
; via (sp),y even though it's already sitting in A (~13 bytes for what
; should be a 3-byte store), and every fixed code region in this cartridge
; is at 100% capacity (see git history around this file's introduction) -
; there is no room left anywhere else to absorb even a small C function.
;
; void platform_hero_light_set(uint8_t light)
;
; light arrives in A per cc65's own convention for a single-parameter
; function (fastcall or not makes no difference for exactly one param).
; Directly overwrites object type 1's ("The Hero") light byte in the
; resident Zone A table (src/platform.c's object_types_a - offset 69 is
; sizeof(PlatformObjectType)*1 + offsetof(PlatformObjectType, light): 35
; bytes/record, light is the last byte at record offset 34, so
; 35 + 34 = 69). Does not trigger a lighting rebuild - the caller must
; call platform_lighting_rebuild() afterward (see rooms/asm/02_use_at.s).
.setcpu "6502"

.import _object_types_a
.import _game_inventory_has_one

.export _platform_hero_light_set
.export _game_player_light_sync

.segment "MIDCODE"

_platform_hero_light_set:
    sta _object_types_a+69
    rts

; void game_player_light_sync(void) - re-derives the Hero's light radius
; from the current inventory (see rooms/asm/02_use_at.s's own call for why
; this exists as a direct type-record patch rather than a per-call check
; inside light_source_apply()). Needed anywhere the resident type table
; could be stale relative to inventory without the ignite path having run
; in this session - currently just src/saveload_runtime.c's load restore,
; called right after it overwrites game_state.inventory from the save
; record; placed in DISKCODE (the save/load disk driver's own segment,
; ~49 spare bytes there versus zero everywhere else this session's other
; additions have needed to fit) since it's thematically a save/load
; helper anyway and every general-purpose code region is full.
; game_inventory_has_one() is a real C boolean (0 or 1 only, never 0xFF)
; so ANDing with 6 - rather than branching - maps {0,1} to {0,6} exactly,
; matching platform_hero_light_set()'s own radius constant.
.segment "DISKCODE"
_game_player_light_sync:
    lda #53                     ; LIT_TAPER_TYPE
    jsr _game_inventory_has_one
    and #6
    jmp _platform_hero_light_set
