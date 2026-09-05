.setcpu "6502"

.include "platform.inc"

.export _env_init
.export _env_enable
.export _env_disable
.export _env_install_null

.segment "HIGHCODE"

; C-callable trampolines into the current room's environment module (see
; PLATFORM_API.md's "Room environment module"). weather_animate (src/irq.s)
; calls ENVCODE_TICK directly, since it's already assembly; these three
; exist only because platform.c (platform_room_enter, platform_portrait_
; show/_hide) needs ordinary callable functions, not raw fixed addresses.
_env_init:
    jmp ENVCODE_INIT

_env_enable:
    jmp ENVCODE_ENABLE

_env_disable:
    jmp ENVCODE_DISABLE

; Installs a no-op module (all four vectors just rts) at ENVCODE_BASE, for
; any room with no environment resource of its own - so ENVCODE_TICK/
; _ENABLE/_DISABLE are always safe to call unconditionally, regardless of
; which room (if any) last loaded a real one there.
_env_install_null:
    ldx #11
@loop:
    lda null_stub,x
    sta ENVCODE_BASE,x
    dex
    bpl @loop
    rts

null_stub:
    jmp shared_rts
    jmp shared_rts
    jmp shared_rts
    jmp shared_rts
shared_rts:
    rts
