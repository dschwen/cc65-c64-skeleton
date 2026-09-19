#include <stdint.h>
#include "memmap.h"

/*
 * Native (oscar64 C) replacement for src/env_dispatch.s: C-callable
 * trampolines into the current room's environment module (see
 * PLATFORM_API.md's "Room environment module"). src/irq.c's
 * weather_animate() calls ENVCODE_TICK directly since it's already C;
 * these three exist only because platform.c (platform_room_enter(),
 * platform_portrait_show()/_hide()) needs ordinary callable functions,
 * not raw fixed addresses.
 */

void env_init(void) {
    ENVCODE_INIT();
}

void env_enable(void) {
    ENVCODE_ENABLE();
}

void env_disable(void) {
    ENVCODE_DISABLE();
}

/* Installs a no-op module (all four vectors just rts) at ENVCODE_BASE, for
 * any room with no environment resource of its own - so ENVCODE_TICK/
 * _ENABLE/_DISABLE are always safe to call unconditionally, regardless of
 * which room (if any) last loaded a real one there. Same 12-byte layout as
 * the original: four 3-byte JMPs to one shared RTS. */
void env_install_null(void) {
    uint8_t* base = ENVCODE_BASE;
    uint8_t i;
    for (i = 0u; i < 4u; ++i) {
        base[i * 3u] = 0x4cu;              /* JMP */
        base[i * 3u + 1u] = 11u;           /* low byte of ENVCODE_BASE+11 */
        base[i * 3u + 2u] = (uint8_t)(((uint16_t)ENVCODE_BASE + 11u) >> 8);
    }
    base[11] = 0x60u; /* RTS */
}
