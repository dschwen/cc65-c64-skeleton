; Fallback for room 05's use_at, spliced by tools/compile_room.py's
; `default -> asm "path"` support. Reached via a plain `jsr`, with the use
; target's tile_x/tile_y still sitting on the software stack exactly where
; the generated dispatch's own `jsr pusha` left them (offset 1 = tile_x,
; offset 0 = tile_y - see gen_dispatch()'s own ABI comment); must return its
; result in A (X is zeroed by the caller) using the same
; GAME_USE_DEFAULT/HANDLED convention a matched `at` entry would.
;
; Handles Pell Thimble's tile (4,7) - the Blue Seal check the script DSL
; cannot express (no "has item"/"consume item" opcode, see
; game_inventory_has_one/game_inventory_remove_one in src/game_support.c):
;   - already helped (STORY_STATE_ROOM_05_PELL_HELPED set): always just
;     talk (STORY_ROOM05_TALK_PELL handles the "thanks again" repeat text
;     itself via check_flag).
;   - not yet helped, carrying the Blue Seal (object type 43): if both
;     evidence flags are set (invitation read, blue door examined),
;     consume the seal and run STORY_ROOM05_PELL_RESOLVE (which gives the
;     Brass Key and sets PELL_HELPED); otherwise run
;     STORY_ROOM05_PELL_EARLY_SEAL (refusal, seal kept).
;   - not carrying the seal: talk (STORY_ROOM05_TALK_PELL).
; Any other tile falls through to the ordinary default - the three
; decorative guest doors keep their own plain `at X,Y` entries in
; rooms/05.rc, ahead of this fallback.
.setcpu "6502"

.import _game_state
.import _game_room_script_entry
.import _game_inventory_has_one
.import _game_inventory_remove_one
.importzp sp

.export _use_at_fallback

BLUE_SEAL_TYPE = 43
PELL_X = 4
PELL_Y = 7

FLAGS_BASE       = _game_state+84
PELL_HELPED      = FLAGS_BASE+13
PELL_INVITE_SEEN = FLAGS_BASE+11
BLUE_DOOR_SEEN   = FLAGS_BASE+12

.segment "BSS"
use_fallback_x: .res 1
use_fallback_y: .res 1

.segment "CODE"

_use_at_fallback:
    ldy #1
    lda (sp),y
    sta use_fallback_x
    ldy #0
    lda (sp),y
    sta use_fallback_y

    lda use_fallback_x
    cmp #PELL_X
    bne @default
    lda use_fallback_y
    cmp #PELL_Y
    bne @default

    lda PELL_HELPED
    bne @talk

    lda #BLUE_SEAL_TYPE
    jsr _game_inventory_has_one
    cmp #0                       ; Z from the call is unreliable: cc65's
                                  ; own incspN cleanup after a __fastcall__
                                  ; return re-touches the flags via its own
                                  ; internal inc, so re-test A explicitly
                                  ; rather than trusting the jsr's Z.
    beq @talk

    lda PELL_INVITE_SEEN
    beq @early_seal
    lda BLUE_DOOR_SEEN
    beq @early_seal

    lda #BLUE_SEAL_TYPE
    jsr _game_inventory_remove_one
    lda #10                      ; STORY_ROOM05_PELL_RESOLVE
    jsr _game_room_script_entry
    lda #1                       ; GAME_USE_HANDLED
    rts

@early_seal:
    lda #9                       ; STORY_ROOM05_PELL_EARLY_SEAL
    jsr _game_room_script_entry
    lda #1                       ; GAME_USE_HANDLED
    rts

@talk:
    lda #6                       ; STORY_ROOM05_TALK_PELL
    jsr _game_room_script_entry
    lda #1                       ; GAME_USE_HANDLED
    rts

@default:
    lda #0                       ; GAME_USE_DEFAULT
    rts
