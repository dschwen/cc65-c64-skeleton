; Constants shared by the platform's assembly modules. Keep the serialized
; layout values synchronized with platform.h.

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
TILE_PROPERTIES     = $3800
OBJECT_TYPES        = $c000
OBJECT_TYPE_SIZE    = 64
OBJECT_TYPE_CHARS   = 16
OBJECT_TYPE_COLORS  = 32

ROOM_TILES          = 9
MAP_WIDTH_TILES     = 20
MAP_HEIGHT_TILES    = 11
MAP_WIDTH_CHARS     = 40
MAP_HEIGHT_CHARS    = 22
ROOM_OBJECTS        = ROOM_TILES + MAP_WIDTH_TILES * MAP_HEIGHT_TILES
ROOM_TEXT           = ROOM_OBJECTS + 256 * 3
ROOM_FILE_BYTES     = ROOM_TEXT + 256

EASYFLASH_BANK      = $de00
EASYFLASH_CONTROL   = $de02
EASYFLASH_OFF       = $04
EASYFLASH_8K        = $06

CART_MARKER         = $033c
CART_MARKER_0       = $ef
CART_MARKER_1       = $64
