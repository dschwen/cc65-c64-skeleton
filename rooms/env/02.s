; Room 02's environment module: a looping 3-voice medieval-tavern melody
; (D Dorian, no rain/no sprites) instead of weather. Linked at ENVCODE_BASE
; (cfg/env_module.cfg); copied there verbatim by platform_room_enter() via
; platform_resource_fetch() - see PLATFORM_API.md's "Room environment
; module" for the fixed 12-byte jump-table header every environment module
; starts with (init/tick/enable/disable).
;
; Voice 1 (lead, triangle) plays a 12-step melody, one note per beat; voice
; 2 (bass, sawtooth) and voice 3 (pad, triangle, an open fifth above the
; bass root) change once per 3-beat bar. All three voices hold their gate
; on continuously from env_init onward - pitch changes are legato (no
; per-note re-attack), both for the "stately" character and to avoid any
; envelope/gate bookkeeping per step.
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
MODE_VOL    = SID+24

STEP_COUNT  = 12
BAR_COUNT   = 4
STEP_FRAMES_SHORT = 40   ; ~75 BPM at 50Hz PAL
STEP_FRAMES_LONG  = 80   ; final step of the phrase, held as a cadence

; ---- persistent state (reinitialized by env_init every room entry) ----
step_timer:   .byte 0
step_index:   .byte 0   ; 0-11
beat_in_bar:  .byte 0   ; 0-2, wraps every 3 steps
bar_index:    .byte 0   ; 0-3

; D Dorian 4-bar phrase, 12 melody steps (D4 F4 A4 | G4 F4 E4 | D4 E4 F4 |
; E4 D4 D4-held). SID 16-bit frequencies at PAL clock, equal temperament.
melody_lo: .byte $89,$3B,$45,$13,$3B,$ED,$89,$ED,$3B,$ED,$89,$89
melody_hi: .byte $13,$17,$1D,$1A,$17,$15,$13,$15,$17,$15,$13,$13

; Bass root per bar (D3 C3 D3 A2) and pad (open fifth above the root: A3 G3
; A3 E3) - re-struck only at the start of each bar.
bass_lo:   .byte $C4,$B4,$C4,$51
bass_hi:   .byte $09,$08,$09,$07
pad_lo:    .byte $A2,$0A,$A2,$F7
pad_hi:    .byte $0E,$0D,$0E,$0A

; Called once on room entry: silence the SID, configure each voice's
; waveform/envelope and leave its gate held on, preload the first
; step's/bar's frequencies so there is no silent gap or pop before the
; first tick, and reset the sequencer state.
env_init:
    ldx #$18
    lda #$00
@clrsid:
    sta SID,x
    dex
    bpl @clrsid

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

    lda #$38
    sta V3_AD
    lda #$A0
    sta V3_SR
    lda pad_lo
    sta V3_FREQ_LO
    lda pad_hi
    sta V3_FREQ_HI
    lda #$11                 ; triangle + gate
    sta V3_CTRL

    lda #$0F
    sta MODE_VOL

    lda #STEP_FRAMES_SHORT
    sta step_timer
    lda #0
    sta step_index
    sta beat_in_bar
    sta bar_index
    rts

; Called once per frame from the resident raster IRQ dispatch (see
; src/irq.s's weather_animate/ENVCODE_TICK). Advances the step sequencer;
; every beat writes the lead's frequency, and every third beat (a new bar)
; also rewrites the bass/pad frequencies.
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
    lda pad_lo,x
    sta V3_FREQ_LO
    lda pad_hi,x
    sta V3_FREQ_HI
@beat_stored:
    sty beat_in_bar

@done:
    rts

; Resume after env_disable (e.g. a portrait/conversation just closed): the
; sequencer keeps advancing silently while disabled (env_tick doesn't touch
; MODE_VOL), so unmuting here picks it back up already in sync.
env_enable:
    lda #$0F
    sta MODE_VOL
    rts

; Pause for a portrait/conversation: mute outright rather than gating each
; voice off individually, same rationale as room 00's env_disable.
env_disable:
    lda #$00
    sta MODE_VOL
    rts
