; Constants shared by the platform's assembly modules. Keep the serialized
; layout values synchronized with platform.h.

CPU_DDR             = $00
CPU_PORT            = $01
CPU_PORT_MASK       = $07
CPU_MAP_ALL_RAM     = $04
CPU_MAP_GAME        = $05
CPU_MAP_CART_8K     = $06
CPU_MAP_CART_16K    = $07
; KERNAL and I/O visible, BASIC hidden so $A000-$BFFF remains usable RAM.
CPU_MAP_KERNAL      = $06

SCREEN_RAM          = $0400
COLOR_RAM           = $d800
TILE_DATA           = $3000
TILE_PROPERTIES     = $3800
; Resident PlatformObjectType layout (src/platform.h): dimensions, hotspot,
; chars[16], colors[16], light. Keep these byte offsets synchronized with it.
OBJECT_TYPE_CHARS   = 2
OBJECT_TYPE_COLORS  = 18

ROOM_TILES          = 13
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
EASYFLASH_16K       = $07

CART_MARKER         = $033c
CART_MARKER_0       = $ef
CART_MARKER_1       = $64
