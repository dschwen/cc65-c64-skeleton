#ifndef GAME_SOUNDS_H
#define GAME_SOUNDS_H

/* Sound-effect IDs for the script DSL's `sound ID` statement (modules/
 * script.c's effect table, played on SID voice 3 over whatever the room's
 * environment module is doing, then the voice is handed back - see
 * platform_sid_v3[] in src/platform_native.h). tools/compile_script.py
 * resolves these names; engine-generic, not story names. */
#define SOUND_CHIME   1u  /* bright rising ring: something magical seats or opens */
#define SOUND_THUD    2u  /* low knock: a heavy object, a locked door */
#define SOUND_CREAK   3u  /* slow falling saw: hinges, wood under strain */
#define SOUND_SPLASH  4u  /* short burst of noise: water, liquid poured */
#define SOUND_BELL    5u  /* long sustained ring with a slow decay */
#define SOUND_SIZZLE  6u  /* high hiss: fire catching, steam */
#define SOUND_BUBBLE  7u  /* rising pulse blip: a pot coming to the boil */
#define SOUND_CLICK   8u  /* tiny tick: a key turning, a latch */

#endif
