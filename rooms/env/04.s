; Room 04's environment module: an ominous cellar ambience instead of
; music - a slow low drone, a sparse minor-key pluck at irregular
; intervals, and an occasional noise "creak". Linked at ENVCODE_BASE
; (cfg/env_module.cfg); copied there verbatim by platform_room_enter() via
; platform_resource_fetch() - see PLATFORM_API.md's "Room environment
; module" for the fixed 12-byte jump-table header every environment
; module starts with (init/tick/enable/disable).
;
; Voice 1 (drone, triangle): holds one note for a long stretch, then
; glides to the next - alternates D3/A2 (a bare fifth) with a low sustain
; level, never re-gated once started, so it reads as continuous rather
; than re-plucked.
;
; Voice 2 (pluck, triangle): sustain 0, so every gate-on decays back to
; silence on its own regardless of how long the gate is held - lets the
; tick handler just fire-and-forget a note without tracking a separate
; note-off. Interval and pitch are both jittered from voice 3's own
; oscillator output (see below), so the plucks never fall into an audible
; loop.
;
; Voice 3 (creak, noise): kept running silently at all times (frequency
; set, gate off) purely so its oscillator output (V3_OSC) is a free
; hardware noise source, exactly as rooms/env/00.s's rain module already
; relies on - occasionally gated on for a short burst as the audible
; "creak", sustain 0 so it also self-silences.
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
V3_FREQ_LO  = SID+14
V3_FREQ_HI  = SID+15
V3_CTRL     = SID+18
V3_AD       = SID+19
V3_SR       = SID+20
V3_OSC      = SID+27
MODE_VOL    = SID+24

; D3 and A2 (a bare fifth) - same equal-tempered SID values room 02's
; tavern tune uses for its bass, reused here for tonal continuity between
; the two rooms.
drone_lo: .byte $C4,$51
drone_hi: .byte $09,$07
DRONE_HOLD_FRAMES = 220   ; ~4.4s at 50Hz between drone changes

; A small D-minor-ish pluck palette (D4, E4, F4, A3), same equal-tempered
; SID values as room 02's own tables.
pluck_lo: .byte $89,$ED,$3B,$A2
pluck_hi: .byte $13,$15,$17,$0E
PLUCK_COUNT = 4

; ---- persistent state (reinitialized by env_init every room entry) ----
drone_timer:  .byte 0
drone_index:  .byte 0   ; 0-1
pluck_timer:  .byte 0
creak_timer:  .byte 0
randbyte:     .byte 0

; Called once on room entry: silence the SID, hold gates low for a couple
; of frames before re-gating - see rooms/env/00.s's env_init for why (a
; SID envelope left gated on and held at sustain, as room 02's tavern
; melody or room 03's quiet variation both leave every voice they use, does
; not reliably restart from a gate low/high pair only ~100 cycles apart).
; None of these three voices are ever routed through the filter, sidestepping
; that separate stuck-voice quirk too.
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

    lda #$08                 ; attack 0, decay 8 - slow fade into each note
    sta V1_AD
    lda #$84                 ; sustain 8 (quieter than full), release 4
    sta V1_SR
    lda drone_lo
    sta V1_FREQ_LO
    lda drone_hi
    sta V1_FREQ_HI
    lda #$11                 ; triangle + gate, held for the whole visit
    sta V1_CTRL

    lda #$01                 ; attack 0, decay 1 - quick pluck attack
    sta V2_AD
    lda #$06                 ; sustain 0 (self-silences), release 6
    sta V2_SR
    lda #$10                 ; triangle, gate off until the first pluck
    sta V2_CTRL

    lda #$00                 ; instant attack, no decay
    sta V3_AD
    lda #$0A                 ; sustain 0, release 10 - short creak
    sta V3_SR
    lda #$FF                 ; high, hiss-like frequency for both the
    sta V3_FREQ_LO           ; audible creak and the oscillator's use as a
    lda #$FF                 ; free-running noise source below
    sta V3_FREQ_HI
    lda #$80                 ; noise, gate off - keeps the oscillator
    sta V3_CTRL              ; running silently for V3_OSC's sake

    lda #$07                 ; quiet, but present
    sta MODE_VOL

    lda #DRONE_HOLD_FRAMES
    sta drone_timer
    lda #0
    sta drone_index
    lda #90
    sta pluck_timer
    lda #150
    sta creak_timer
    rts

; Called once per frame from the resident raster IRQ dispatch (see
; src/irq.s's weather_animate/ENVCODE_TICK).
env_tick:
    lda V3_OSC
    sta randbyte

    dec drone_timer
    bne @drone_done
    lda #DRONE_HOLD_FRAMES
    sta drone_timer
    lda drone_index
    eor #$01
    sta drone_index
    tax
    lda drone_lo,x
    sta V1_FREQ_LO
    lda drone_hi,x
    sta V1_FREQ_HI
@drone_done:

    dec pluck_timer
    bne @pluck_done
    lda randbyte
    and #$7F
    clc
    adc #$50                 ; ~1.6-4.1s at 50Hz until the next pluck
    sta pluck_timer
    lda randbyte
    lsr a
    lsr a
    and #(PLUCK_COUNT - 1)
    tax
    lda pluck_lo,x
    sta V2_FREQ_LO
    lda pluck_hi,x
    sta V2_FREQ_HI
    lda #$10                 ; gate low first - the envelope generator only
    sta V2_CTRL              ; retriggers attack on a 0-to-1 gate edge, and
    lda #$11                 ; the previous pluck's gate bit is still set
    sta V2_CTRL              ; (sustain 0 only silences the output, not the
@pluck_done:                 ; gate latch)

    dec creak_timer
    bne @creak_done
    lda randbyte
    and #$7F
    clc
    adc #$64                 ; ~2-4.5s at 50Hz until the next creak - stays
    sta creak_timer          ; under 256 so this 8-bit add can't wrap
    lda #$80                 ; gate low first, same 0-to-1 edge requirement
    sta V3_CTRL              ; as the pluck voice above
    lda #$81                 ; noise + gate: brief audible burst, sustain 0
    sta V3_CTRL              ; decays it back to silence on its own
@creak_done:
    rts

; Resume after env_disable: the timers keep counting down silently while
; disabled, so unmuting here just picks the ambience back up in sync.
env_enable:
    lda #$07
    sta MODE_VOL
    rts

; Pause for a portrait/conversation: mute outright.
env_disable:
    lda #$00
    sta MODE_VOL
    rts
