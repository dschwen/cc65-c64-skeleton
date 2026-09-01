; Split the screen between the editor's two charset banks. Gameplay normally
; banks KERNAL out, so the hardware entry saves its own registers. A second
; entry supports periods in which disk code temporarily maps KERNAL back in.

.export _raster_irq_install, _raster_irq_vectors_restore, _raster_irq_resync
.export _raster_irq_suspend, _raster_irq_resume
.export _platform_frame_counter
.export _platform_text_screen_enter, _platform_text_screen_leave
.export _platform_rain_enable, _platform_rain_disable, _platform_rain_is_active

.import _platform_text_area_clear_native

.include "platform.inc"

VIC_CTRL1      = $d011
VIC_RASTER     = $d012
VIC_MEMPTR     = $d018
VIC_IRQ_STATUS = $d019
VIC_IRQ_ENABLE = $d01a
VIC_SPR_ENABLE = $d015
VIC_SPR_YEXP   = $d017
VIC_SPR_PRIO   = $d01b
VIC_SPR_MC     = $d01c
VIC_SPR_XEXP   = $d01d
CIA1_IRQ       = $dc0d
IRQ_VECTOR     = $0314
KERNAL_IRQ_OUT = $ea81
RAM_NMI_VECTOR = $fffa
RAM_RST_VECTOR = $fffc
RAM_IRQ_VECTOR = $fffe

TILE_MEMPTR    = $18       ; screen $0400, charset $2000
TEXT_MEMPTR    = $1a       ; screen $0400, charset $2800
TEXT_RASTER    = 226       ; one line before row 22's badline
WATER_CHAR     = $2000 + 14 * 8
SPRITE_POINTERS = $07f8
RAIN_BITMAP     = $3b80
RAIN_POINTER    = RAIN_BITMAP / 64
RAIN_MASK       = $fe      ; sprites 1-7; portraits borrow 1-5 (mutually exclusive with rain)
RAIN_COUNT      = 7
RAIN_STEP       = 20

.segment "BSS"
_platform_frame_counter: .res 1
platform_text_screen_active: .res 1
platform_raster_irq_active: .res 1
platform_rain_active: .res 1
rain_x_lo: .res RAIN_COUNT+1   ; index 0 unused; 1-7 map to sprites 1-7
rain_y: .res RAIN_COUNT+1
rain_seed: .res 1

.segment "CODE"

_raster_irq_install:
    sei
    lda #0
    sta _platform_frame_counter
    sta platform_text_screen_active
    sta platform_rain_active
    lda #$ac                ; an all-zero LFSR state never escapes zero
    sta rain_seed
    lda #1
    sta platform_raster_irq_active

    ; Own the IRQ source. This intentionally stops the KERNAL jiffy clock.
    lda #$7f
    sta CIA1_IRQ
    lda CIA1_IRQ

    lda #<kernal_irq_entry
    sta IRQ_VECTOR
    lda #>kernal_irq_entry
    sta IRQ_VECTOR+1

    jsr _raster_irq_vectors_restore

    lda VIC_CTRL1
    and #$7f               ; compare against a raster line below 256
    sta VIC_CTRL1
    lda #TEXT_RASTER
    sta VIC_RASTER

    lda #$01
    sta VIC_IRQ_STATUS     ; clear a pending raster request
    lda VIC_IRQ_ENABLE
    ora #$01
    sta VIC_IRQ_ENABLE
    cli
    rts

; Rewrite the RAM vectors after loading type 255. Stores reach RAM even while
; KERNAL ROM is visible.
_raster_irq_vectors_restore:
    lda #<direct_nmi_entry
    sta RAM_NMI_VECTOR
    lda #>direct_nmi_entry
    sta RAM_NMI_VECTOR+1
    lda #<direct_reset_entry
    sta RAM_RST_VECTOR
    lda #>direct_reset_entry
    sta RAM_RST_VECTOR+1
    lda #<direct_irq_entry
    sta RAM_IRQ_VECTOR
    lda #>direct_irq_entry
    sta RAM_IRQ_VECTOR+1
    rts

; Full-screen text users prepare and restore screen RAM themselves. The IRQ
; keeps using the text charset at line zero until leave restores the map split.
_platform_text_screen_enter:
    lda #1
    sta platform_text_screen_active
    lda #TEXT_MEMPTR
    sta VIC_MEMPTR
    rts

_platform_text_screen_leave:
    lda #0
    sta platform_text_screen_active
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    rts

; Re-establish the split after a long IRQ-disabled operation, such as copying
; an EasyFlash room bank. Choose the next event from the VIC's full 9-bit
; raster position so a pending interrupt cannot leave the text charset over
; the map for a frame.
.segment "UPPERCODE"
_raster_irq_resync:
    php
    sei
    lda platform_raster_irq_active
    bne @resync_active
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    lda #$01
    sta VIC_IRQ_STATUS
    plp
    rts
@resync_active:
    lda VIC_CTRL1
    bpl @resync_low_raster
    ; Avoid scheduling line zero while it may be only a few cycles away.
    ; Vertical blank is invisible, so wait for the 9-bit raster to wrap and
    ; establish the map phase directly.
@resync_wait_top:
    lda VIC_CTRL1
    bmi @resync_wait_top
    jmp @resync_map_phase
@resync_low_raster:
    lda VIC_RASTER
    cmp #TEXT_RASTER
    bcc @resync_map_phase
    beq @resync_wait_text
    bcs @resync_text_phase
@resync_wait_text:
    lda VIC_RASTER
    cmp #TEXT_RASTER+1
    bne @resync_wait_text
@resync_text_phase:
    lda #TEXT_MEMPTR
    sta VIC_MEMPTR
    lda #0
    sta VIC_RASTER
    beq @resync_finish
@resync_map_phase:
    lda platform_text_screen_active
    beq :+
    lda #TEXT_MEMPTR
    bne :++
:
    lda #TILE_MEMPTR
:
    sta VIC_MEMPTR
    lda #TEXT_RASTER
    sta VIC_RASTER
@resync_finish:
    lda VIC_CTRL1
    and #$7f
    sta VIC_CTRL1
    lda #$01
    sta VIC_IRQ_STATUS
    plp
    rts

; Room transactions keep the tile charset selected and do not need the split
; while no status text is visible. Clearing first avoids leaving text glyphs
; on screen when D018 is forced to the tile bank.
_raster_irq_suspend:
    jsr _platform_text_area_clear_native
    php
    sei
    lda #0
    sta platform_raster_irq_active
    sta VIC_IRQ_ENABLE
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    lda #$01
    sta VIC_IRQ_STATUS
    plp
    rts

.segment "STATEEXT"
_raster_irq_resume:
    php
    sei
    lda #1
    sta platform_raster_irq_active
    jsr _raster_irq_resync
    lda #$01
    sta VIC_IRQ_ENABLE
    plp
    rts

.segment "CODE"
direct_irq_entry:
    pha
    txa
    pha
    tya
    pha
    jsr raster_irq_body
    pla
    tay
    pla
    tax
    pla
    rti

raster_irq_body:
    ; D012 wraps at line 256, so consult D011 first. A late IRQ in the
    ; vertical blank belongs to the text phase and must schedule line zero.
    lda VIC_CTRL1
    bmi @switch_to_text
    lda VIC_RASTER
    beq @top_of_frame

    ; If a top-of-frame IRQ was delayed into the map, recover immediately.
    ; This avoids waiting almost a complete frame with the text charset still
    ; selected. Lines 227-255 are already safe for the text charset.
    cmp #TEXT_RASTER
    bcc @late_top_of_frame
    cmp #TEXT_RASTER+1
    bcs @switch_to_text

    ; Entering through the KERNAL vector lands partway through line 226.
    ; Switch at the start of 227: after row 21's final glyph fetch and
    ; before the row 22 badline takes the CPU bus.
@wait_for_text_row:
    lda VIC_RASTER
    cmp #TEXT_RASTER+1
    bne @wait_for_text_row

@switch_to_text:
    lda #TEXT_MEMPTR
    sta VIC_MEMPTR
    lda #0
    sta VIC_RASTER

    ; The tile charset is no longer visible below this split. Rain sprites
    ; are ordinary (non-multiplexed) VIC sprites now, so they stay wherever
    ; rain_advance last placed them. Rain advances every frame; water
    ; remains at half the frame rate.
    inc _platform_frame_counter
    jsr weather_animate
    jmp @done

@top_of_frame:
    lda platform_text_screen_active
    bne @top_uses_text
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    jmp @top_schedule_text
@top_uses_text:
    lda #TEXT_MEMPTR
    bne @top_store_memptr

@late_top_of_frame:
    lda #TILE_MEMPTR
@top_store_memptr:
    sta VIC_MEMPTR
@top_schedule_text:
    lda #TEXT_RASTER
    sta VIC_RASTER

@done:
    lda #$01
    sta VIC_IRQ_STATUS
    rts

direct_nmi_entry:
    pha
    txa
    pha
    tya
    pha
    lda $dd0d               ; acknowledge a possible CIA2 NMI
    pla
    tay
    pla
    tax
    pla
    rti

.segment "UPPERCODE"
direct_reset_entry:
    lda #$2f
    sta CPU_DDR
    lda #$37
    sta CPU_PORT
    jmp $fce2

; Rain now owns one dedicated hardware sprite per streak (1-7) instead of
; multiplexing two sprites across bands, because portraits (the only other
; user of sprites 1-5) are never shown while rain is running: room entry
; disables rain before dispatch, and platform_portrait_show()/hide() pause
; and resume it around a conversation. That makes sort order and raster-timed
; repositioning unnecessary - each streak's position is just written straight
; to its own sprite's VIC registers once a frame.
.segment "HIGHCODE"
; rain_x_lo/rain_y are indexed directly by sprite number (1-7); index 0 is
; unused. That lets the setup loop below double as the initial-seed fill
; (both walk sprites 7..1), and rain_advance address VIC sprite N's
; registers ($D000+2N) with no +1/-1 translation anywhere.
_platform_rain_enable:
    lda #1
    sta platform_rain_active
    ldx #7
@setup:
    lda #RAIN_POINTER
    sta SPRITE_POINTERS,x
    lda #6                  ; dark blue
    sta $d027,x
    lda #255                ; past the respawn threshold: rain_advance below
    sta rain_y,x             ; will seed a fresh random position for it
    dex
    bne @setup
    lda VIC_SPR_YEXP
    and #<~RAIN_MASK
    sta VIC_SPR_YEXP
    lda VIC_SPR_PRIO
    ora #RAIN_MASK
    sta VIC_SPR_PRIO
    lda VIC_SPR_MC
    and #<~RAIN_MASK
    sta VIC_SPR_MC
    lda VIC_SPR_XEXP         ; native width: the bitmap is already a true
    and #<~RAIN_MASK         ; 45-degree diagonal, one bit per row
    sta VIC_SPR_XEXP
    lda VIC_SPR_ENABLE
    ora #RAIN_MASK
    sta VIC_SPR_ENABLE
    jmp rain_advance

; Advance all seven streaks by (RAIN_STEP,RAIN_STEP) and write the result
; straight to sprite X's own VIC registers. Any streak that leaves the screen
; respawns on the top or left edge: the edge coordinate is fixed at 0, the
; position along the edge is random. Called once per frame from
; weather_animate; returns via the caller's rts (tail call from enable).
rain_advance:
    lda platform_rain_active
    bne :+
    rts
:
    ldx #7
@loop:
    lda rain_y,x
    clc
    adc #RAIN_STEP
    sta rain_y,x
    bcs @respawn
    lda rain_x_lo,x
    clc
    adc #RAIN_STEP
    sta rain_x_lo,x
    bcc @write
@respawn:
    jsr rain_prng
    bmi @spawn_left
    sta rain_x_lo,x
    lda #0
    sta rain_y,x
    jmp @write
@spawn_left:
    and #$7f
    sta rain_y,x
    lda #0
    sta rain_x_lo,x
@write:
    txa
    asl
    tay
    lda rain_x_lo,x
    sta $d000,y             ; sprite X's X register
    lda rain_y,x
    sta $d001,y              ; sprite X's Y register
    dex
    bne @loop
    rts

.segment "RAINCODE"
; Animate water once every other frame and advance rain every frame. This
; segment shares sprite slot 7's old bitmap storage, which is free now that
; rain uses sprites 1-7 directly instead of multiplexing through slot 7.
weather_animate:
    lda _platform_frame_counter
    and #$01
    bne @rain
    ldx #7
@roll_water:
    lda WATER_CHAR,x
    asl
    bcc :+
    ora #$01                ; wrap the old bit 7 into bit 0
:
    sta WATER_CHAR,x
    dex
    bpl @roll_water

@rain:
    jmp rain_advance

_platform_rain_disable:
    lda #0
    sta platform_rain_active
    lda VIC_SPR_ENABLE
    and #<~RAIN_MASK
    sta VIC_SPR_ENABLE
    rts

_platform_rain_is_active:
    lda platform_rain_active
    rts

; 8-bit Galois LFSR; cheap PRNG for streak respawn placement.
rain_prng:
    lda rain_seed
    asl
    bcc :+
    eor #$1d
:
    sta rain_seed
    rts

.segment "CODE"
kernal_irq_entry:
    jsr raster_irq_body
    jmp KERNAL_IRQ_OUT

