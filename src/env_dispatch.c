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

static void env_null_stub(void) {
}

/* Installs a no-op module at ENVCODE_BASE, for any room with no environment
 * resource of its own - so ENVCODE_TICK/_ENABLE/_DISABLE are always safe to
 * call unconditionally, regardless of which room (if any) last loaded a
 * real one there.
 *
 * This used to hand-write a raw "four 3-byte JMPs to one shared RTS" table,
 * matching the old cc65 design's fixed-offset jump-table convention. That
 * is NOT what ENVCODE_TICK() etc. read any more (see memmap.h): they treat
 * ENVCODE_BASE as a `const EnvTable*` and call through raw 2-byte function-
 * pointer slots at offsets 0/2/4/6, not 3-byte JMP opcodes at offsets
 * 0/3/6/9. Writing the old format left garbage bytes straddling those
 * pointer slots, so ENVCODE_TICK() jumped to a bogus address assembled
 * from a JMP opcode and a stray target byte - confirmed live as the actual
 * root cause of the raster-IRQ-enabled crash: harmless through the first
 * frame (nothing calls the table yet), then wild once weather_tick_pending
 * fires the first ENVCODE_TICK() and PC ends up in unrelated code. */
void env_install_null(void) {
    EnvTable* table = (EnvTable*)ENVCODE_BASE;
    table->init = env_null_stub;
    table->tick = env_null_stub;
    table->enable = env_null_stub;
    table->disable = env_null_stub;
}
