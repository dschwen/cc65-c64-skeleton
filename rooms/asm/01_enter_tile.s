; Hand-written enter_tile for room 01: keeps the tile-entry-count bump the
; DSL's "flag ... increment" directive used to produce (see
; rooms/asm/00_enter_tile.s for the same pattern), then adds the one thing
; the DSL has no statement for - swapping the player's own object type to
; 0x31 ("submerged hero") while standing on a water tile (tile ID 2, see
; assets/tiles.ctil), and back to 0x01 ("The Hero") otherwise. Runs
; unconditionally every tile-entry event (room arrival, save/load reentry,
; and ordinary movement - see game_player_step()/game_enter_room() in
; src/game.c) and relies on platform_player_set_type()'s own no-op-if-
; unchanged check, so there is no separate "was I submerged last frame"
; state to track here. Spliced verbatim into the DSL-generated room-01
; output by tools/compile_room.py (enter_tile: asm
; "rooms/asm/01_enter_tile.s").
.setcpu "6502"

.import _platform_room
.import _platform_player_set_type

.export _enter_tile

; GameState.flags[] base offset within _game_state (see
; tools/compile_room.py's GAME_STATE_FLAGS_OFFSET comment - re-check this if
; GameState's field order/size in src/game.h ever changes).
ENTER_TILE_FLAGS_BASE = _game_state+84
ENTER_TILE_PLAYER_X   = _game_state+10    ; half-tile coordinates
ENTER_TILE_PLAYER_Y   = _game_state+11
ENTER_TILE_ROOM_TILES = _platform_room+13 ; 13-byte header precedes the tile grid
ENTER_TILE_WATER_TILE = 2
HERO_TYPE      = $01
SUBMERGED_TYPE = $31

.segment "BSS"
enter_tile_index: .res 1

.segment "CODE"

_enter_tile:
    inc ENTER_TILE_FLAGS_BASE+1 ; STORY_STATE_ROOM_01_TILE_ENTRY_COUNT

    ; tile_index = (player_y >> 1) * 20 + (player_x >> 1)
    lda ENTER_TILE_PLAYER_Y
    lsr a
    asl a
    asl a
    sta enter_tile_index         ; tile_y * 4
    asl a
    asl a                        ; tile_y * 16
    clc
    adc enter_tile_index          ; tile_y*16 + tile_y*4 = tile_y*20
    sta enter_tile_index
    lda ENTER_TILE_PLAYER_X
    lsr a
    clc
    adc enter_tile_index
    tax
    lda ENTER_TILE_ROOM_TILES,x
    cmp #ENTER_TILE_WATER_TILE
    bne @on_land

    lda #SUBMERGED_TYPE
    jsr _platform_player_set_type
    rts

@on_land:
    lda #HERO_TYPE
    jsr _platform_player_set_type
    rts
