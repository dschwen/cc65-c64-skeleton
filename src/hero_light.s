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

.export _platform_hero_light_set

.segment "MIDCODE"

_platform_hero_light_set:
    sta _object_types_a+69
    rts
