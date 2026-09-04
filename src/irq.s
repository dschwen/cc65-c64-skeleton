; Split the screen between the editor's two charset banks. Gameplay normally
; banks KERNAL out, so the hardware entry saves its own registers. A second
; entry supports periods in which disk code temporarily maps KERNAL back in.

.export _raster_irq_install, _raster_irq_vectors_restore, _raster_irq_resync
.export _raster_irq_suspend, _raster_irq_resume
.export _platform_frame_counter
.export platform_raster_irq_active
.export _platform_text_screen_enter, _platform_text_screen_leave
.export _platform_rain_enable, _platform_rain_activate
.export _platform_rain_disable, _platform_rain_is_active

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
RAIN_X_OFFSET   = 23       ; map's left edge in sprite-X coordinates (tile 0)
RAIN_X_MAX_LO   = 87       ; respawn once x_hi=1 and x_lo>=this: x >= 343, the map's right edge
; Map's bottom edge in sprite-Y coordinates: PAL raster = sprite Y + ~50, and
; TEXT_RASTER (226) is one line before row 22's badline (51+22*8=227), so
; row 22 - the first row below the 22-row map (PLATFORM_MAP_CHAR_HEIGHT) -
; starts at sprite Y ~177. Without this, rain_advance let rain_y run all
; the way to the 8-bit overflow at 256 before respawning, so streaks fell
; through the map's own bottom edge and the reserved text rows below it,
; visibly landing in the border past both - confirmed live in VICE and by
; a screenshot showing streaks well below the map's drawn tiles.
RAIN_Y_MAX      = 176

.segment "BSS"
_platform_frame_counter: .res 1
platform_text_screen_active: .res 1
platform_raster_irq_active: .res 1
platform_rain_active: .res 1
rain_x_lo: .res RAIN_COUNT+1   ; index 0 unused; 1-7 map to sprites 1-7
rain_x_hi: .res RAIN_COUNT+1   ; sprite X's 9th X-position bit (0 or 1)
rain_y: .res RAIN_COUNT+1
rain_seed: .res 1
rain_setup_hook: .res 2

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

; Room transactions call this from ordinary map mode and want the tile
; charset with no split while no status text is visible - clearing rows
; 22-24 first avoids leaving text glyphs on screen when D018 is forced to
; the tile bank. But inventory/save/load close out through this same path
; (see saveload_cleanup() and game_inventory_show()) while still in a full
; text screen: platform_text_screen_enter() was called by the loaded
; overlay module itself, and platform_text_screen_active stays 1 until the
; resident wrapper calls platform_text_screen_leave() - *after* this
; suspend, not before. Forcing TILE_MEMPTR unconditionally here briefly
; reinterpreted rows 0-21 of a still-fully-populated inventory/save screen
; through the wrong charset before the wrapper's own memset cleared them -
; a real, visible one-frame glitch. Pick the charset the same way
; _raster_irq_resync's map-phase branch already does, instead of assuming
; tile.
_raster_irq_suspend:
    jsr _platform_text_area_clear_native
    php
    sei
    lda #0
    sta platform_raster_irq_active
    sta VIC_IRQ_ENABLE
    lda platform_text_screen_active
    beq @suspend_tile
    lda #TEXT_MEMPTR
    bne @suspend_set
@suspend_tile:
    lda #TILE_MEMPTR
@suspend_set:
    sta VIC_MEMPTR
    lda #$01
    sta VIC_IRQ_STATUS
    plp
    rts

.segment "STATEEXT"
_platform_rain_is_active:
    lda platform_rain_active
    rts

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

    ; The tile charset (and the map) is no longer visible below this split,
    ; so hide rain here rather than let it run over the status text rows;
    ; @top_of_frame turns it back on for the map. Rain advances every frame;
    ; water remains at half the frame rate.
    lda platform_rain_active
    beq @text_no_rain
    jsr rain_hide_sprites
@text_no_rain:
    inc _platform_frame_counter
    jsr weather_animate
    jmp @done

@top_of_frame:
    lda platform_text_screen_active
    bne @top_uses_text
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    lda platform_rain_active
    beq @top_schedule_text
    lda VIC_SPR_ENABLE
    ora #RAIN_MASK
    sta VIC_SPR_ENABLE
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

rain_hide_sprites:
    lda VIC_SPR_ENABLE
    and #<~RAIN_MASK
    sta VIC_SPR_ENABLE
    rts

.segment "CODE"
; Returns a uniformly random sprite-X position in [23,342] (the map's full
; pixel width, RAIN_X_OFFSET to RAIN_X_OFFSET+319): A = rain_x_hi (0 or 1),
; Y = rain_x_lo. 320 isn't a power of two, so this draws a 9-bit value 0-511
; from two independent sources and rejects (retries) the 192 values outside
; the target range, leaving every one of the 320 valid positions equally
; likely - PROVIDED the two sources are actually independent.
;
; The low byte (rain_x_lo) can't also supply the half coin flip via a
; second rain_prng() call: for this LFSR (shift left, then XOR #$1d only
; when the shifted-out bit was 1), a fresh call's bit 0 is always exactly
; equal to the *previous* call's bit 7, since #$1d's own bit 7 is 0. An
; earlier version did exactly that - call rain_prng for rain_x_lo (call it
; Y), then and #1 on the very next call for the coin flip - so that coin
; flip was always just bit7(Y), not independent of it at all. That meant
; "high half" was only ever chosen when Y>=128, but the high half's own
; bounds check requires Y<87 to accept - a contradiction that can never be
; satisfied, so it always retried into the low half. Verified live in VICE:
; rain_pick_x returned rain_x_hi=0 on 30/30 sampled calls, and rain streaks
; only ever reached the right half of the screen by slowly drifting there
; over many frames (by which point they'd fallen well past the top), never
; by spawning there.
;
; The fix: draw the coin flip from _platform_frame_counter's low bit
; instead - a completely separate counter (driven by raster timing, not
; the rain LFSR), so it carries no relationship to rain_seed's state.
rain_pick_x:
@retry:
    jsr rain_prng
    tay                      ; Y = candidate rain_x_lo
    lda _platform_frame_counter
    and #1
    bne @high_half
    cpy #RAIN_X_OFFSET
    bcc @retry               ; low half: reject x_lo < 23
    rts
@high_half:
    cpy #RAIN_X_OFFSET+64    ; 342-256+1
    bcs @retry               ; high half: reject x_lo >= 87
    rts


; Rain now owns one dedicated hardware sprite per streak (1-7) instead of
; multiplexing two sprites across bands, because portraits (the only other
; user of sprites 1-5) are never shown while rain is running: room entry
; disables rain before dispatch, and platform_portrait_show()/hide() pause
; and resume it around a conversation. That makes sort order and raster-timed
; repositioning unnecessary - each streak's position is just written straight
; to its own sprite's VIC registers once a frame.
.segment "HIGHCODE"
; A/X = a room's rain_setup() (fastcall: A=low, X=high), or 0/0 to reuse
; whichever setup was last registered (the portrait-resume path's case,
; since it never has a setup of its own to give - see platform.h). Sprite
; pointer/color/VIC-attribute setup lives in room code, not here: it only
; ever needs to run synchronously from room entry or from the
; portrait-resume hook, both of which are guaranteed to have the relevant
; room's own EasyFlash bank still paged in, so it's safe to move out of the
; always-resident budget. The per-frame code below cannot follow it there:
; it runs from the raster IRQ, which can fire while a *different* bank is
; paged in (inventory, save/load), so it must stay resident.
_platform_rain_enable:
    cmp #0
    bne @store
    cpx #0
    beq @use_existing
@store:
    sta rain_setup_hook
    stx rain_setup_hook+1
@use_existing:
    lda rain_setup_hook
    ora rain_setup_hook+1
    beq _platform_rain_activate
    jsr rain_call_hook
; rain_x_lo/rain_y are indexed directly by sprite number (1-7); index 0 is
; unused, letting rain_advance address VIC sprite N's registers ($D000+2N)
; with no +1/-1 translation anywhere.
_platform_rain_activate:
    lda #1
    sta platform_rain_active
    ldx #7
@fill:
    lda #255                ; past the respawn threshold: rain_advance below
    sta rain_y,x             ; will seed a fresh random position for it
    dex
    bne @fill
    jmp rain_advance

; Advance all seven streaks by (RAIN_STEP,RAIN_STEP) and write the result
; straight to sprite X's own VIC registers. Any streak that leaves the screen
; respawns on the top or left edge: on the left edge X is fixed at 0 (just
; off the left border); on the top edge Y is fixed at 0 and X is uniformly
; random across the map's full width via rain_pick_x. Called once per frame
; from weather_animate; returns via the caller's rts (tail call from enable).
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
    cmp #RAIN_Y_MAX
    bcs @respawn             ; also respawn on reaching the map's bottom edge
    lda rain_x_lo,x
    clc
    adc #RAIN_STEP
    sta rain_x_lo,x
    bcc @check_hi
    inc rain_x_hi,x
@check_hi:
    lda rain_x_hi,x
    beq @write
    lda rain_x_lo,x
    cmp #RAIN_X_MAX_LO
    bcc @write
@respawn:
    jsr rain_prng
    bmi @spawn_left
    jsr rain_pick_x          ; A = rain_x_hi, Y = rain_x_lo (final map-relative X)
    sta rain_x_hi,x
    tya
    sta rain_x_lo,x
    lda #0
    sta rain_y,x
    jmp @write
@spawn_left:
    and #$7f                 ; reuse the edge-choice byte's low bits for Y
    sta rain_y,x
    lda #0
    sta rain_x_lo,x
    sta rain_x_hi,x
@write:
    lda rain_bit_mask,x
    ldy rain_x_hi,x
    beq @clear_msb
    ora $d010
    jmp @store_msb
@clear_msb:
    eor #$ff
    and $d010
@store_msb:
    sta $d010
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
; Tail-jumps to the room's registered setup callback; its own rts returns
; to whichever instruction follows the jsr that reached here (native 6502
; indirect JMP - no runtime call-through-pointer helper needed).
rain_call_hook:
    jmp (rain_setup_hook)

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

; 8-bit Galois LFSR; cheap PRNG for streak respawn placement.
rain_prng:
    lda rain_seed
    asl
    bcc :+
    eor #$1d
:
    sta rain_seed
    rts

rain_bit_mask:
    .byte $00, $02, $04, $08, $10, $20, $40, $80

.segment "CODE"
kernal_irq_entry:
    jsr raster_irq_body
    jmp KERNAL_IRQ_OUT

