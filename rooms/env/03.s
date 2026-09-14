; Room 03's environment module: the tavern's melody (room 02) as heard
; faintly through the kitchen wall - same D Dorian tune and tempo, thinner
; (melody + bass only, no pad voice) and much quieter. Linked at
; ENVCODE_BASE (cfg/env_module.cfg); copied there verbatim by
; platform_room_enter() via platform_resource_fetch() - see
; PLATFORM_API.md's "Room environment module" for the fixed 12-byte
; jump-table header every environment module starts with (init/tick/
; enable/disable).
;
; Voice 1 (lead, triangle) plays the same 12-step melody as room 02, one
; note per beat, legato (gate held continuously, no per-note re-attack).
; Voice 2 (bass, sawtooth) changes once per 3-beat bar, same as room 02.
; Voice 3 is left silent - dropping the pad is what makes this read as
; "the same tune, but muffled" rather than a full room-02 volume dip.
.setcpu "6502"

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
MODE_VOL    = SID+24

STEP_COUNT  = 12
BAR_COUNT   = 4
STEP_FRAMES_SHORT = 40   ; ~75 BPM at 50Hz PAL - same tempo as room 02
STEP_FRAMES_LONG  = 80

; ---- persistent state (reinitialized by env_init every room entry) ----
step_timer:   .byte 0
step_index:   .byte 0   ; 0-11
beat_in_bar:  .byte 0   ; 0-2, wraps every 3 steps
bar_index:    .byte 0   ; 0-3

; Identical to room 02's tables (rooms/env/02.s) - same tune.
melody_lo: .byte $89,$3B,$45,$13,$3B,$ED,$89,$ED,$3B,$ED,$89,$89
melody_hi: .byte $13,$17,$1D,$1A,$17,$15,$13,$15,$17,$15,$13,$13
bass_lo:   .byte $C4,$B4,$C4,$51
bass_hi:   .byte $09,$08,$09,$07

; Called once on room entry: silence the SID, hold both voices' gates low
; for a couple of frames before re-gating - a SID envelope left gated on
; and held at sustain (exactly how room 02's melody leaves voice 1/2 when
; the player walks straight from the tavern into the kitchen) does not
; reliably restart from a gate low/high pair only ~100 cycles apart; see
; rooms/env/00.s's env_init for the same fix and the real-hardware
; measurement that found it. Neither voice here is ever routed through the
; filter, sidestepping that whole separate class of stuck-voice quirk too.
env_init:
    ldx #$18
    lda #$00
@clrsid:
    sta SID,x
    dex
    bpl @clrsid

    ldx #$20
@gatelow:
    ldy #$00
@gatelow_inner:
    dey
    bne @gatelow_inner
    dex
    bne @gatelow

    lda #$18                 ; attack 1, decay 8
    sta V1_AD
    lda #$F0                 ; sustain 15, release 0
    sta V1_SR
    lda melody_lo
    sta V1_FREQ_LO
    lda melody_hi
    sta V1_FREQ_HI
    lda #$11                 ; triangle + gate
    sta V1_CTRL

    lda #$28
    sta V2_AD
    lda #$C0
    sta V2_SR
    lda bass_lo
    sta V2_FREQ_LO
    lda bass_hi
    sta V2_FREQ_HI
    lda #$21                 ; sawtooth + gate
    sta V2_CTRL

    lda #$03                 ; ~20% of full volume
    sta MODE_VOL

    lda #STEP_FRAMES_SHORT
    sta step_timer
    lda #0
    sta step_index
    sta beat_in_bar
    sta bar_index
    rts

; Called once per frame from the resident raster IRQ dispatch (see
; src/irq.s's weather_animate/ENVCODE_TICK). Same sequencer shape as room
; 02's, minus the pad voice.
env_tick:
    dec step_timer
    bne @done

    ldx step_index
    inx
    cpx #STEP_COUNT
    bne @step_stored
    ldx #0
@step_stored:
    stx step_index

    cpx #(STEP_COUNT - 1)
    bne @short_step
    lda #STEP_FRAMES_LONG
    jmp @store_timer
@short_step:
    lda #STEP_FRAMES_SHORT
@store_timer:
    sta step_timer

    lda melody_lo,x
    sta V1_FREQ_LO
    lda melody_hi,x
    sta V1_FREQ_HI

    ldy beat_in_bar
    iny
    cpy #3
    bne @beat_stored
    ldy #0
    ldx bar_index
    inx
    cpx #BAR_COUNT
    bne @bar_stored
    ldx #0
@bar_stored:
    stx bar_index
    lda bass_lo,x
    sta V2_FREQ_LO
    lda bass_hi,x
    sta V2_FREQ_HI
@beat_stored:
    sty beat_in_bar

@done:
    rts

; Resume after env_disable: the sequencer keeps advancing silently while
; disabled, so unmuting here picks it back up already in sync.
env_enable:
    lda #$03
    sta MODE_VOL
    rts

; Pause for a portrait/conversation: mute outright.
env_disable:
    lda #$00
    sta MODE_VOL
    rts
