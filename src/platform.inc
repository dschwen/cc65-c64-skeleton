; Constants shared by the platform's assembly modules. Keep these synchronized
; with the compile-time assertions in platform.c/platform.h.

CPU_DDR             = $00
CPU_PORT            = $01
CPU_PORT_MASK       = $07
CPU_MAP_ALL_RAM     = $04
CPU_MAP_GAME        = $05
CPU_MAP_CART        = $07
CPU_MAP_KERNAL      = $07

SCREEN_RAM          = $0400
COLOR_RAM           = $d800
TILE_DATA           = $3000
OBJECT_TYPES        = $c000
OBJECT_TYPE_SIZE    = 64
OBJECT_TYPE_CHARS   = 16
OBJECT_TYPE_COLORS  = 32

ROOM_TILES          = 4
MAP_WIDTH_TILES     = 20
MAP_HEIGHT_TILES    = 11
MAP_WIDTH_CHARS     = 40
MAP_HEIGHT_CHARS    = 22

EASYFLASH_BANK      = $de00
EASYFLASH_CONTROL   = $de02
EASYFLASH_OFF       = $04
EASYFLASH_8K        = $06

CART_MARKER         = $033c
CART_MARKER_0       = $ef
CART_MARKER_1       = $64
