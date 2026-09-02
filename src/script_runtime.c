#include <stdint.h>

#include "game.h"

/* Resident trigger for the script/conversation interpreter overlay
 * (modules/script.c). Every bank through 48 is already spoken for, so like
 * room-helpers this shares TYPE_BANK_1's ROML half (with the object-type
 * Zone C table and the room-helpers overlay) at a fixed offset past both -
 * see tools/pack_easyflash.py's SCRIPT_EF_OFFSET.
 *
 * Script/conversation *content* (the compiled bytecode - see
 * tools/compile_script.py) is separate from this overlay's own code: it's
 * fetched by the overlay itself, at run time, from the generic sparse
 * resource directory, using resource IDs 240-255 (reserved for this - room
 * text already claims 0-239, one per possible room ID). See
 * modules/script.c.
 */
#define SCRIPT_EF_BANK    47u
#define SCRIPT_EF_OFFSET  2048u
#define SCRIPT_MAGIC_0    0x53u /* 'S' */
#define SCRIPT_MAGIC_1    0x43u /* 'C' */

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_overlay_run_native(void);
uint8_t __fastcall__ platform_overlay_load(uint8_t bank, uint8_t use_romh,
                                           uint16_t offset, uint8_t magic0,
                                           uint8_t magic1);

/* Set by the caller before game_script_play(); which resource the overlay
 * should fetch and run. */
uint8_t script_resource_id = 0;

#pragma code-name (push, "LOWCODE")
void game_script_play(uint8_t resource_id) {
    uint8_t result;

    platform_look_cursor_hide();
    script_resource_id = resource_id;
    result = platform_overlay_load(SCRIPT_EF_BANK, 0u, SCRIPT_EF_OFFSET,
                                   SCRIPT_MAGIC_0, SCRIPT_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();

    /* The overlay borrowed the room-text scratch buffer (see
     * platform_room_scratch() in platform.h) and any portrait it showed;
     * put both back the way normal gameplay expects to find them. */
    platform_portrait_hide();
    platform_room_text_reload();
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    raster_irq_suspend();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
}
#pragma code-name (pop)
