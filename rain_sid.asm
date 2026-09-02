; ============================================================
; SID RAIN NOISE - Raster IRQ routine
; Assembler: ACME syntax
; Target: real C64 hardware, PAL/NTSC agnostic (raster-driven)
; ============================================================
;
; Voice 1: continuous filtered noise = rain "bed"
; Voice 2: gated noise bursts       = individual droplets
; Voice 3: NOT audible, used purely as a free random number
;          source (its oscillator runs even when volume is 0
;          and the voice isn't routed anywhere)
;
; Filter cutoff on voice 1 is nudged up/down each frame using
; bits pulled from voice 3's noise oscillator so it drifts
; instead of sitting static.
;
; ============================================================

; ---- SID register base ----
SID         = $D400
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
V3_OSC      = SID+27   ; read-only: voice 3 oscillator output

CUTOFF_LO   = SID+21
CUTOFF_HI   = SID+22
RES_FILT    = SID+23   ; resonance / filter routing
MODE_VOL    = SID+24   ; filter mode / master volume

; ---- CIA / IRQ vectors ----
CIA1_ICR    = $DC0D
VIC_RASTER  = $D012
VIC_CTRL1   = $D011
IRQ_LO      = $0314
IRQ_HI      = $0315

; ---- CIA1 keyboard matrix (for footstep key scan) ----
CIA1_PRA    = $DC00     ; column select (write)
CIA1_PRB    = $DC01     ; row read

; ---- Zero page scratch ----
raintimer   = $FB      ; countdown to next droplet trigger
cutofftmp   = $FC       ; current filter cutoff (low byte we vary)
randbyte    = $FD
stepcd      = $FA       ; cooldown frames until the next step is allowed
stepgate    = $FF       ; frames left to hold voice 2 open for a footstep

* = $C000

init:
        sei

        ; --- silence SID, clear registers ---
        ldx #$18
        lda #$00
clrsid: sta SID,x
        dex
        bpl clrsid

        ; --- Voice 3: silent, used only as random source ---
        lda #$80        ; noise waveform, gate off
        sta V3_CTRL
        lda #$FF        ; high frequency = fast, "white" random stream
        sta V3_FREQ_LO
        sta V3_FREQ_HI

        ; --- Voice 1: rain bed (continuous noise, filtered) ---
        lda #$00
        sta V1_FREQ_LO
        lda #$C0
        sta V1_FREQ_HI          ; freq irrelevant for noise waveform
        lda #$09                ; attack=0, decay=9 (slow-ish swell)
        sta V1_AD
        lda #$58                ; sustain=5 (quieter bed), release=8
        sta V1_SR
        lda #$81                ; noise waveform, gate ON (held on)
        sta V1_CTRL

        ; --- Voice 2: droplet hits (short gated noise bursts) ---
        lda #$00
        sta V2_FREQ_LO
        lda #$E0
        sta V2_FREQ_HI
        lda #$00                ; attack=0 (instant), decay=0
        sta V2_AD
        lda #$00                ; sustain=0, release=0 (short click/tick)
        sta V2_SR
        lda #$80                ; noise waveform, gate OFF for now
        sta V2_CTRL

        ; --- Filter setup ---
        lda #$90
        sta cutofftmp
        sta CUTOFF_HI            ; start cutoff mid-range
        lda #$00
        sta CUTOFF_LO
        lda #$1D                 ; route voice1+voice2+voice3 through filter, resonance ~1
        sta RES_FILT
        lda #$1F                 ; low-pass mode, volume 15 (of 15)
        sta MODE_VOL

        lda #$20
        sta raintimer

        lda #$00
        sta stepcd
        sta stepgate

        ; --- Install raster IRQ ---
        lda #$00
        sta IRQ_LO
        lda #>irq_routine
        sta IRQ_HI
        lda IRQ_LO
        ; (fix low byte properly below, ACME two-pass will resolve it)
        lda #<irq_routine
        sta IRQ_LO

        lda #$7B
        sta CIA1_ICR             ; disable CIA IRQs so only raster fires

        lda #$FF
        sta $D01A                ; wait — set below properly
        lda #$01
        sta $D01A                ; enable raster IRQ in VIC

        lda #100
        sta VIC_RASTER            ; trigger partway down the screen
        lda VIC_CTRL1
        and #$7F
        sta VIC_CTRL1             ; clear high bit of raster line

        cli
        rts                       ; return to caller / main loop

; ============================================================
; IRQ ROUTINE - runs once per frame (~50/60 Hz)
; ============================================================
irq_routine:
        pha
        txa
        pha
        tya
        pha

        ; --- acknowledge VIC raster interrupt ---
        lda #$01
        sta $D019

        ; --- grab a random byte from voice 3's noise oscillator ---
        lda V3_OSC
        sta randbyte

        ; --- drift the filter cutoff using low bits of randbyte ---
        lda randbyte
        and #$03                 ; small step size -3..+3ish
        sta $FE
        lda randbyte
        and #$04
        beq cutdown
cutup:
        lda cutofftmp
        clc
        adc $FE
        cmp #$c0                 ; ceiling
        bcc storecut
        lda #$c0
        jmp storecut
cutdown:
        lda cutofftmp
        sec
        sbc $FE
        cmp #$60                 ; floor
        bcs storecut
        lda #$60
storecut:
        sta cutofftmp
        sta CUTOFF_HI

        ; --- droplet trigger countdown ---
        dec raintimer
        bne skipdrop

        ; time for a new droplet: gate voice 2 on, reseed timer.
        ; Skip the gate-on if a footstep currently owns voice 2 (this
        ; drop is silently dropped rather than stomping the footstep).
        lda stepgate
        bne reseed
        lda #$81
        sta V2_CTRL               ; noise waveform, gate ON
        ; a couple wasted cycles later we'll gate it off (see below)

reseed:
        ; reseed timer from random byte so drops arrive irregularly
        lda randbyte
        and #$1F                  ; 0-31 frame range
        clc
        adc #$04                  ; minimum spacing so drops don't overlap
        sta raintimer
        jmp donedrop

skipdrop:
        ; gate voice 2 off a few frames after it was triggered
        ; (crude but effective: turn off once timer is back near top)
        lda raintimer
        cmp #$1E
        bcc donedrop
        lda stepgate
        bne donedrop               ; a footstep owns voice 2 - leave it alone
        lda #$80
        sta V2_CTRL                ; gate off, decay/release finishes click

donedrop:

        ; --- footstep: any held key steals voice 2 on a walking cadence ---
        ; No KERNAL IRQ is chained here, so the keyboard buffer never
        ; fills; the CIA1 matrix has to be polled directly. Selecting
        ; all columns at once (0) and checking for any row line pulled
        ; low is a simple "is any key down" test, no matrix lookup needed.
        ; A key is retriggered on a cooldown (not a press edge) so
        ; holding a movement key down produces a steady stream of
        ; steps instead of one tick followed by silence.
        lda #$00
        sta CIA1_PRA
        lda CIA1_PRB
        ldx #$FF
        stx CIA1_PRA             ; deselect columns again
        cmp #$FF
        beq stepcd_tick           ; nothing held -> just tick timers below

        lda stepcd
        bne stepcd_tick           ; still cooling down from the last step

        lda #$04                 ; attack 0, decay 4 (longer than a droplet tick)
        sta V2_AD
        lda #$C6                 ; sustain C, release 6 -> loud "splash" tail
        sta V2_SR
        ; force gate low then high: a SID envelope only restarts its
        ; attack on a 0->1 edge, and voice 2 may already be gated on
        ; from a droplet, so this guarantees a clean retrigger
        lda #$80
        sta V2_CTRL
        lda #$81
        sta V2_CTRL
        lda #$08
        sta stepgate              ; hold voice 2 open for 8 frames
        lda #$0C
        sta stepcd                ; next step allowed in 12 frames (~walking pace)

stepcd_tick:
        lda stepcd
        beq stepgate_tick
        dec stepcd

stepgate_tick:
        ; release voice 2 once the footstep's hold time elapses, and
        ; put the droplet click envelope back so ticks sound right
        lda stepgate
        beq donestep
        dec stepgate
        bne donestep
        lda #$80
        sta V2_CTRL
        lda #$00
        sta V2_AD
        sta V2_SR
donestep:

        pla
        tay
        pla
        tax
        pla
        rti
