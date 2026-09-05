; Room 00's environment module: rain (visual sprites, existing mechanism)
; unified with rain-bed/droplet/footstep sound (new - see the measurement
; this was designed from, a translation of the standalone rain_sid.asm
; prototype). Linked at ENVCODE_BASE (cfg/env_module.cfg); copied there
; verbatim by platform_room_enter() via platform_resource_fetch() - see
; PLATFORM_API.md's "Room environment module" for the fixed 12-byte
; jump-table header every environment module starts with (init/tick/
; enable/disable), and why this must be reserved, always-RAM space rather
; than a banked $A4E9-style overlay (the raster IRQ calls the tick vector
; every frame and can never bank-switch to reach it).
.setcpu "6502"

.import _platform_rain_enable
.import _platform_rain_disable

.segment "CODE"

; ---- fixed jump-table header (offsets 0/3/6/9) ----
    jmp env_init
    jmp env_tick
    jmp env_enable
    jmp env_disable

; ---- SID register layout ----
SID         = $d400
V1_FREQ_LO  = SID+0
V1_FREQ_HI  = SID+1
V1_CTRL     = SID+4
V1_AD       = SID+5
V1_SR       = SID+6
V2_FREQ_LO  = SID+7
V2_FREQ_HI  = SID+8
V2_CTRL     = SID+11
V2_AD       = SID+12
V2_SR       = SID+13
V3_FREQ_LO  = SID+14
V3_FREQ_HI  = SID+15
V3_CTRL     = SID+18
V3_OSC      = SID+27
CUTOFF_LO   = SID+21
CUTOFF_HI   = SID+22
RES_FILT    = SID+23
MODE_VOL    = SID+24

; ---- VIC sprite registers (rain's 7 dedicated sprites, 1-7) ----
VIC_SPRITE_POINTERS = $07f8
VIC_SPRITE_ENABLE   = $d015
VIC_SPRITE_YEXPAND  = $d017
VIC_SPRITE_PRIORITY = $d01b
VIC_SPRITE_XEXPAND  = $d01d
VIC_SPRITE0_COLOR   = $d027
RAIN_BITMAP_POINTER = $3b80 / 64
RAIN_SPRITE_MASK     = $fe   ; sprites 1-7
RAIN_SPRITE_MASK_INV = $01   ; ~RAIN_SPRITE_MASK & $ff

; ---- persistent state (reinitialized by env_init every room entry) ----
raintimer:        .byte 0
cutofftmp:        .byte 0
randbyte:         .byte 0
cutstep:          .byte 0
stepcd:           .byte 0
stepgate:         .byte 0
footstep_pending: .byte 0

; Called once on room entry. Registers and runs the sprite setup (same
; mechanism platform_rain_enable() already provides - see its own doc
; comment in src/platform.h for why the callback runs from here rather
; than staying resident), then sets up the SID rain-bed/droplet voices.
env_init:
    lda #<sprite_setup
    ldx #>sprite_setup
    jsr _platform_rain_enable
    jmp sound_init            ; tail call: sound_init's own rts returns to
                                ; env_init's caller

sprite_setup:
    ldx #1
@loop:
    lda #RAIN_BITMAP_POINTER
    sta VIC_SPRITE_POINTERS,x
    lda #6                      ; dark blue
    sta VIC_SPRITE0_COLOR,x
    inx
    cpx #8
    bne @loop

    lda VIC_SPRITE_YEXPAND
    and #RAIN_SPRITE_MASK_INV
    sta VIC_SPRITE_YEXPAND

    lda VIC_SPRITE_PRIORITY
    ora #RAIN_SPRITE_MASK
    sta VIC_SPRITE_PRIORITY

    lda VIC_SPRITE_XEXPAND
    and #RAIN_SPRITE_MASK_INV
    sta VIC_SPRITE_XEXPAND

    lda VIC_SPRITE_ENABLE
    ora #RAIN_SPRITE_MASK
    sta VIC_SPRITE_ENABLE
    rts

sound_init:
    ldx #$18
    lda #$00
@clrsid:
    sta SID,x
    dex
    bpl @clrsid

    lda #$80
    sta V3_CTRL
    lda #$ff
    sta V3_FREQ_LO
    sta V3_FREQ_HI

    lda #$00
    sta V1_FREQ_LO
    lda #$c0
    sta V1_FREQ_HI
    lda #$09
    sta V1_AD
    lda #$58
    sta V1_SR
    lda #$81
    sta V1_CTRL

    lda #$00
    sta V2_FREQ_LO
    lda #$e0
    sta V2_FREQ_HI
    lda #$00
    sta V2_AD
    sta V2_SR
    lda #$80
    sta V2_CTRL

    lda #$90
    sta cutofftmp
    sta CUTOFF_HI
    lda #$00
    sta CUTOFF_LO
    lda #$1d
    sta RES_FILT
    lda #$1f
    sta MODE_VOL

    lda #$20
    sta raintimer
    lda #$00
    sta stepcd
    sta stepgate
    sta footstep_pending
    rts

; Called once per frame from the resident raster IRQ (weather_animate).
; Advances the rain-bed filter drift, gates droplet/footstep hits.
env_tick:
    lda V3_OSC
    sta randbyte

    lda randbyte
    and #$03
    sta cutstep
    lda randbyte
    and #$04
    beq @cutdown
@cutup:
    lda cutofftmp
    clc
    adc cutstep
    cmp #$c0
    bcc @storecut
    lda #$c0
    jmp @storecut
@cutdown:
    lda cutofftmp
    sec
    sbc cutstep
    cmp #$60
    bcs @storecut
    lda #$60
@storecut:
    sta cutofftmp
    sta CUTOFF_HI

    dec raintimer
    bne @skipdrop

    lda stepgate
    bne @reseed
    lda #$81
    sta V2_CTRL

@reseed:
    lda randbyte
    and #$1f
    clc
    adc #$04
    sta raintimer
    jmp @donedrop

@skipdrop:
    lda raintimer
    cmp #$1e
    bcc @donedrop
    lda stepgate
    bne @donedrop
    lda #$80
    sta V2_CTRL

@donedrop:
    lda footstep_pending
    beq @stepcd_tick
    lsr footstep_pending
    lda stepcd
    bne @stepcd_tick

    lda #$04
    sta V2_AD
    lda #$c6
    sta V2_SR
    lda #$80
    sta V2_CTRL
    lda #$81
    sta V2_CTRL
    lda #$08
    sta stepgate
    lda #$0c
    sta stepcd

@stepcd_tick:
    lda stepcd
    beq @stepgate_tick
    dec stepcd

@stepgate_tick:
    lda stepgate
    beq @donestep
    dec stepgate
    bne @donestep
    lda #$80
    sta V2_CTRL
    lda #$00
    sta V2_AD
    sta V2_SR
@donestep:
    rts

; Resume after env_disable (e.g. a portrait/conversation just closed):
; redo the sprite setup (portrait_show() reassigned sprites 1-5 for its
; own use - see platform_rain_enable()'s "0/0 reuses whichever setup was
; last registered" convention) and unmute the SID.
env_enable:
    lda #0
    ldx #0
    jsr _platform_rain_enable
    lda #$1f
    sta MODE_VOL
    rts

; Pause for a portrait/conversation: hide the visual rain (existing
; mechanism) and mute the SID outright (simpler and more robust than
; gating each voice off individually - whatever envelope/gate state is
; mid-flight picks back up unchanged once env_enable restores volume).
env_disable:
    jsr _platform_rain_disable
    lda #$00
    sta MODE_VOL
    rts
