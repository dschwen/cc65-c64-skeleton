#ifndef MOONFEN_WALTZ_PLAYER_H
#define MOONFEN_WALTZ_PLAYER_H

#include <stdint.h>

/*
 * Moonfen Waltz score and player, shared by the cartridge title screen
 * (modules/title.c) and the standalone preview (music/title_music.c).
 * PAL, 50 Hz frame-driven: the caller ticks it once per frame.
 *
 * "The Moonfen Waltz" - D minor, 3/4, ~107 BPM, about 100 seconds per loop:
 *
 *   intro    2 bars  violin alone over a sparse kick
 *   theme   16 bars  Dm Dm Gm A7 | Dm Bb E° A7 | Dm F Gm Dm | Bb E° A7 Dm
 *   var. 1  16 bars  melody in flowing eighths; violin plays a chromatic
 *                    lament bass line (D C# C B Bb A G A ...)
 *   var. 2  16 bars  "danse macabre": staccato melody built on tritone
 *                    leaps; violin wavers between A and Bb above it
 *   reprise  8 bars  second half of the theme
 *   coda     2 bars
 *
 * Voice 1: piano melody - pulse wave, instant attack, long decay to
 *          silence, slow pulse-width sweep.
 * Voice 2: violin backing - sawtooth through a resonant, slowly swept
 *          low-pass filter; legato (gate stays on) with a short glide
 *          between notes and delayed vibrato.
 * Voice 3: percussion - kick, snare and two hi-hats, each a per-frame
 *          waveform/pitch program.
 *
 * Include music/waltz_state.h first. waltz_init() once, then waltz_tick()
 * once per PAL frame.
 */

#define SID ((volatile uint8_t*)0xd400u)
#define V1_FREQ_LO SID[0]
#define V1_FREQ_HI SID[1]
#define V1_PW_LO   SID[2]
#define V1_PW_HI   SID[3]
#define V1_CTRL    SID[4]
#define V1_AD      SID[5]
#define V1_SR      SID[6]
#define V2_FREQ_LO SID[7]
#define V2_FREQ_HI SID[8]
#define V2_CTRL    SID[11]
#define V2_AD      SID[12]
#define V2_SR      SID[13]
#define V3_FREQ_LO SID[14]
#define V3_FREQ_HI SID[15]
#define V3_PW_LO   SID[16]
#define V3_PW_HI   SID[17]
#define V3_CTRL    SID[18]
#define V3_AD      SID[19]
#define V3_SR      SID[20]
#define FILTER_LO  SID[21]
#define FILTER_HI  SID[22]
#define FILTER_RES SID[23]
#define MODE_VOL   SID[24]

#define FRAMES_PER_STEP 7u  /* one sixteenth note */
#define STEPS_PER_BAR 12u   /* 3/4 */
#define GLIDE_FRAMES 5u
#define VIBRATO_DELAY 10u

/* Note numbers: 12 * octave + semitone, A4 = 57 = 440 Hz. 0 is a rest and
 * 0xff ends a track (it loops). On the violin track, REL() releases the
 * note two frames early so the next one bows in fresh instead of gliding. */
#define C_(o) (12u * (o) + 0u)
#define Cs(o) (12u * (o) + 1u)
#define Db(o) (12u * (o) + 1u)
#define D_(o) (12u * (o) + 2u)
#define Eb(o) (12u * (o) + 3u)
#define E_(o) (12u * (o) + 4u)
#define F_(o) (12u * (o) + 5u)
#define Fs(o) (12u * (o) + 6u)
#define G_(o) (12u * (o) + 7u)
#define Gs(o) (12u * (o) + 8u)
#define Ab(o) (12u * (o) + 8u)
#define A_(o) (12u * (o) + 9u)
#define Bb(o) (12u * (o) + 10u)
#define B_(o) (12u * (o) + 11u)
#define R 0u
#define REL(n) ((n) | 0x80u)
#define END 0xffu

/* PAL (985248 Hz) SID frequencies for note numbers 0-95. */
static const uint8_t freq_lo[96] = {
    0x16, 0x27, 0x39, 0x4B, 0x5F, 0x74, 0x8A, 0xA1, 0xBA, 0xD4, 0xF0, 0x0E,
    0x2D, 0x4E, 0x71, 0x96, 0xBE, 0xE7, 0x14, 0x42, 0x74, 0xA9, 0xE0, 0x1B,
    0x5A, 0x9C, 0xE2, 0x2D, 0x7B, 0xCF, 0x27, 0x85, 0xE8, 0x51, 0xC1, 0x37,
    0xB4, 0x38, 0xC4, 0x59, 0xF7, 0x9D, 0x4E, 0x0A, 0xD0, 0xA2, 0x81, 0x6D,
    0x67, 0x70, 0x89, 0xB2, 0xED, 0x3B, 0x9C, 0x13, 0xA0, 0x45, 0x02, 0xDA,
    0xCE, 0xE0, 0x11, 0x64, 0xDA, 0x76, 0x39, 0x26, 0x40, 0x89, 0x04, 0xB4,
    0x9C, 0xC0, 0x23, 0xC8, 0xB4, 0xEB, 0x72, 0x4C, 0x80, 0x12, 0x08, 0x68,
    0x39, 0x80, 0x45, 0x90, 0x68, 0xD6, 0xE3, 0x99, 0x00, 0x24, 0x10, 0xFF,
};
static const uint8_t freq_hi[96] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04,
    0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x08,
    0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x1A, 0x1B, 0x1D, 0x1F, 0x20,
    0x22, 0x24, 0x27, 0x29, 0x2B, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3E, 0x41,
    0x45, 0x49, 0x4E, 0x52, 0x57, 0x5C, 0x62, 0x68, 0x6E, 0x75, 0x7C, 0x83,
    0x8B, 0x93, 0x9C, 0xA5, 0xAF, 0xB9, 0xC4, 0xD0, 0xDD, 0xEA, 0xF8, 0xFF,
};

/* ---- score: (note, length in sixteenths) pairs, one bar per line ---- */

static const uint8_t melody[] = {
    /* intro */
    R,12,
    R,12,
    /* theme */
    A_(4),4, D_(5),6, E_(5),2,
    F_(5),4, E_(5),4, D_(5),4,
    G_(5),4, Bb(5),4, A_(5),2, G_(5),2,
    E_(5),4, Cs(5),4, A_(4),4,
    D_(5),4, F_(5),4, A_(5),4,
    D_(6),6, C_(6),2, Bb(5),4,
    A_(5),2, G_(5),2, F_(5),2, E_(5),2, D_(5),2, Cs(5),2,
    E_(5),4, A_(4),8,
    A_(4),4, D_(5),6, E_(5),2,
    F_(5),4, A_(5),4, C_(6),4,
    Bb(5),6, A_(5),2, G_(5),4,
    F_(5),4, D_(5),4, A_(4),4,
    Bb(4),4, D_(5),4, F_(5),4,
    G_(5),4, E_(5),4, Cs(5),4,
    E_(5),2, G_(5),2, F_(5),2, E_(5),2, Cs(5),4,
    D_(5),12,
    /* variation 1: flowing eighths */
    A_(4),2, D_(5),2, F_(5),2, A_(5),2, F_(5),2, D_(5),2,
    F_(5),2, G_(5),2, F_(5),2, E_(5),2, D_(5),2, E_(5),2,
    G_(5),2, Bb(5),2, D_(6),2, Bb(5),2, A_(5),2, G_(5),2,
    E_(5),2, Cs(5),2, A_(4),2, Cs(5),2, E_(5),2, G_(5),2,
    D_(5),2, F_(5),2, A_(5),2, D_(6),2, A_(5),2, F_(5),2,
    D_(6),2, C_(6),2, Bb(5),2, F_(5),2, D_(5),2, F_(5),2,
    Bb(5),2, A_(5),2, G_(5),2, F_(5),2, E_(5),2, Cs(5),2,
    A_(5),2, G_(5),2, E_(5),2, Cs(5),2, A_(4),4,
    D_(5),2, A_(4),2, D_(5),2, E_(5),2, F_(5),2, A_(5),2,
    C_(6),2, A_(5),2, F_(5),2, C_(5),2, F_(5),2, A_(5),2,
    Bb(5),2, A_(5),2, G_(5),2, D_(5),2, G_(5),2, Bb(5),2,
    A_(5),2, F_(5),2, D_(5),2, A_(4),2, D_(5),2, F_(5),2,
    Bb(4),2, D_(5),2, F_(5),2, Bb(5),2, F_(5),2, D_(5),2,
    G_(5),2, E_(5),2, Cs(5),2, E_(5),2, G_(5),2, Bb(5),2,
    A_(5),2, G_(5),2, F_(5),2, E_(5),2, D_(5),2, Cs(5),2,
    D_(5),4, A_(4),4, D_(5),4,
    /* variation 2: danse macabre */
    D_(5),2, R,2, Ab(4),2, R,2, A_(4),4,
    D_(5),2, R,2, F_(5),2, R,2, E_(5),2, D_(5),2,
    G_(5),2, R,2, Db(5),2, R,2, D_(5),4,
    Cs(5),2, E_(5),2, G_(5),2, Bb(5),2, A_(5),4,
    D_(6),2, R,2, Ab(5),2, R,2, A_(5),4,
    Bb(5),2, R,2, F_(5),2, R,2, D_(5),4,
    E_(5),2, G_(5),2, Bb(5),2, G_(5),2, E_(5),2, Cs(5),2,
    A_(4),2, R,2, E_(5),2, R,2, A_(5),4,
    F_(5),2, R,2, E_(5),2, R,2, D_(5),4,
    C_(5),2, F_(5),2, A_(5),2, C_(6),2, A_(5),4,
    Bb(5),2, R,2, A_(5),2, R,2, G_(5),4,
    F_(5),2, E_(5),2, D_(5),2, Cs(5),2, D_(5),4,
    F_(5),2, R,2, Bb(5),2, R,2, D_(6),4,
    Cs(6),2, Bb(5),2, G_(5),2, E_(5),2, Cs(5),4,
    E_(5),2, F_(5),2, E_(5),2, D_(5),2, Cs(5),4,
    D_(5),4, A_(4),4, R,4,
    /* reprise: theme bars 9-16 */
    A_(4),4, D_(5),6, E_(5),2,
    F_(5),4, A_(5),4, C_(6),4,
    Bb(5),6, A_(5),2, G_(5),4,
    F_(5),4, D_(5),4, A_(4),4,
    Bb(4),4, D_(5),4, F_(5),4,
    G_(5),4, E_(5),4, Cs(5),4,
    E_(5),2, G_(5),2, F_(5),2, E_(5),2, Cs(5),4,
    D_(5),12,
    /* coda */
    D_(5),4, A_(4),4, F_(4),4,
    D_(4),8, R,4,
    END
};

static const uint8_t violin[] = {
    /* intro */
    D_(4),12,
    REL(Cs(4)),12,
    /* theme */
    F_(4),12,
    F_(4),8, E_(4),4,
    D_(4),12,
    Cs(4),6, E_(4),6,
    F_(4),12,
    F_(4),12,
    E_(4),6, G_(4),6,
    E_(4),6, REL(Cs(4)),6,
    D_(4),12,
    C_(4),6, A_(3),6,
    Bb(3),6, D_(4),6,
    D_(4),6, F_(4),6,
    F_(4),12,
    G_(4),6, E_(4),6,
    G_(4),6, E_(4),6,
    REL(F_(4)),12,
    /* variation 1: chromatic lament */
    D_(4),12,
    Cs(4),12,
    C_(4),12,
    B_(3),12,
    Bb(3),12,
    A_(3),12,
    G_(3),12,
    REL(A_(3)),12,
    D_(4),12,
    C_(4),12,
    Bb(3),12,
    A_(3),12,
    Bb(3),12,
    G_(3),12,
    A_(3),12,
    REL(D_(4)),12,
    /* variation 2: wavering drone */
    A_(4),12,
    A_(4),12,
    Bb(4),12,
    A_(4),12,
    A_(4),12,
    Bb(4),12,
    Bb(4),12,
    REL(A_(4)),12,
    A_(4),12,
    A_(4),12,
    Bb(4),12,
    A_(4),12,
    Bb(4),12,
    Bb(4),12,
    A_(4),12,
    REL(A_(4)),12,
    /* reprise */
    D_(4),12,
    C_(4),6, A_(3),6,
    Bb(3),6, D_(4),6,
    D_(4),6, F_(4),6,
    F_(4),12,
    G_(4),6, E_(4),6,
    G_(4),6, E_(4),6,
    F_(4),12,
    /* coda: sink to the low D and let it fade */
    A_(3),12,
    D_(3),8, R,4,
    END
};

/* Drum instruments. */
#define K 1u /* kick */
#define S 2u /* snare */
#define H 3u /* closed hi-hat */
#define h 4u /* soft hi-hat */
#define _ 0u

static const uint8_t drum_patterns[6][STEPS_PER_BAR] = {
    { K,_,_,_, H,_,_,_, H,_,_,_ }, /* 0 waltz */
    { K,_,h,_, H,_,h,_, H,_,h,_ }, /* 1 waltz with off-beats */
    { K,_,_,_, S,_,_,_, S,_,h,h }, /* 2 macabre */
    { K,_,h,_, S,_,S,_, S,S,S,S }, /* 3 fill */
    { K,_,_,_, _,_,_,_, _,_,_,_ }, /* 4 downbeat only */
    { _,_,_,_, _,_,_,_, _,_,_,_ }, /* 5 silent */
};

#undef K
#undef S
#undef H
#undef h
#undef _

#define BAR_COUNT 60u
static const uint8_t drum_bars[BAR_COUNT] = {
    4, 0,                                           /* intro */
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,3,             /* theme */
    1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,3,             /* variation 1 */
    2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,3,             /* variation 2 */
    0,0,0,0, 0,0,0,0,                               /* reprise */
    4, 5,                                           /* coda */
};

/* Per-instrument envelope plus a six-frame waveform/pitch program; a
 * control byte with the gate bit clear releases the note. */
#define DRUM_FRAMES 6u
static const uint8_t drum_ad[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t drum_sr[5] = { 0x00, 0xf4, 0xc4, 0x81, 0x41 };
static const uint8_t drum_ctrl[5][DRUM_FRAMES] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x81, 0x41, 0x41, 0x41, 0x41, 0x40 }, /* kick: noise click, falling pulse */
    { 0x41, 0x81, 0x81, 0x81, 0x80, 0x80 }, /* snare: pulse crack, noise body */
    { 0x81, 0x81, 0x80, 0x80, 0x80, 0x80 }, /* closed hat */
    { 0x81, 0x80, 0x80, 0x80, 0x80, 0x80 }, /* soft hat */
};
static const uint8_t drum_freq[5][DRUM_FRAMES] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x18, 0x0c, 0x08, 0x06, 0x04, 0x03 },
    { 0x0c, 0x30, 0x28, 0x22, 0x20, 0x20 },
    { 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0 },
    { 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0 },
};

static const int8_t vibrato[8] = { 0, 2, 3, 2, 0, -2, -3, -2 };

static uint16_t note_freq(uint8_t note) {
    return (uint16_t)freq_hi[note] << 8 | freq_lo[note];
}

static void melody_next(void) {
    uint8_t note = melody[mel_pos];
    if (note == END) {
        mel_pos = 0u;
        note = melody[0];
    }
    mel_left = (uint16_t)melody[mel_pos + 1u] * FRAMES_PER_STEP;
    mel_pos += 2u;
    if (note == R) {
        V1_CTRL = 0x40u;
        return;
    }
    V1_FREQ_LO = freq_lo[note];
    V1_FREQ_HI = freq_hi[note];
    V1_CTRL = 0x41u; /* pulse + gate */
}

static void melody_tick(void) {
    if (mel_left == 0u) melody_next();
    /* Drop the gate one frame early so the next note re-attacks cleanly. */
    if (mel_left == 1u) V1_CTRL = 0x40u;
    --mel_left;

    if (mel_pw_up) {
        mel_pw += 12u;
        if (mel_pw >= 0x0a00u) mel_pw_up = 0u;
    } else {
        mel_pw -= 12u;
        if (mel_pw <= 0x0300u) mel_pw_up = 1u;
    }
    V1_PW_LO = (uint8_t)mel_pw;
    V1_PW_HI = (uint8_t)(mel_pw >> 8);
}

static void violin_next(void) {
    uint8_t note = violin[vio_pos];
    if (note == END) {
        vio_pos = 0u;
        note = violin[0];
    }
    vio_left = (uint16_t)violin[vio_pos + 1u] * FRAMES_PER_STEP;
    vio_pos += 2u;
    vio_release = note & 0x80u;
    note &= 0x7fu;
    if (note == R) {
        V2_CTRL = 0x20u;
        vio_gated = 0u;
        return;
    }
    vio_target = note_freq(note);
    if (!vio_gated) {
        /* Fresh bow stroke: start on pitch, no glide. */
        vio_freq = vio_target;
        vio_step = 0u;
        vio_age = 0u;
        V2_CTRL = 0x21u; /* sawtooth + gate */
        vio_gated = 1u;
    } else if (vio_target > vio_freq) {
        vio_step = (vio_target - vio_freq) / GLIDE_FRAMES + 1u;
    } else {
        vio_step = (vio_freq - vio_target) / GLIDE_FRAMES + 1u;
    }
    if (vio_target != vio_freq) vio_age = 0u;
}

static void violin_tick(void) {
    uint16_t out;
    int16_t offset;

    if (vio_left == 0u) violin_next();
    if (vio_release && vio_left == 2u) {
        V2_CTRL = 0x20u;
        vio_gated = 0u;
    }
    --vio_left;

    if (vio_freq < vio_target) {
        vio_freq = vio_target - vio_freq <= vio_step ? vio_target : vio_freq + vio_step;
    } else if (vio_freq > vio_target) {
        vio_freq = vio_freq - vio_target <= vio_step ? vio_target : vio_freq - vio_step;
    }

    out = vio_freq;
    if (vio_age < VIBRATO_DELAY) {
        ++vio_age;
    } else {
        vio_vib_phase = (uint8_t)((vio_vib_phase + 1u) & 7u);
        offset = (int16_t)vibrato[vio_vib_phase] * (int16_t)(vio_freq >> 8);
        out = (uint16_t)((int16_t)out + offset);
    }
    V2_FREQ_LO = (uint8_t)out;
    V2_FREQ_HI = (uint8_t)(out >> 8);

    /* Slow resonant low-pass sweep: the violin "breathes". */
    if (filter_up) {
        filter_cut += 2u;
        if (filter_cut >= 0x0480u) filter_up = 0u;
    } else {
        filter_cut -= 2u;
        if (filter_cut <= 0x0200u) filter_up = 1u;
    }
    FILTER_LO = (uint8_t)(filter_cut & 7u);
    FILTER_HI = (uint8_t)(filter_cut >> 3);
}

static void drum_tick(void) {
    uint8_t inst;

    if (drum_step_left == 0u) {
        drum_step_left = FRAMES_PER_STEP;
        inst = drum_patterns[drum_bars[drum_bar]][drum_step];
        if (inst != 0u) {
            drum_inst = inst;
            drum_frame = 0u;
            V3_AD = drum_ad[inst];
            V3_SR = drum_sr[inst];
        }
        if (++drum_step == STEPS_PER_BAR) {
            drum_step = 0u;
            if (++drum_bar == BAR_COUNT) drum_bar = 0u;
        }
    }
    --drum_step_left;

    if (drum_frame < DRUM_FRAMES) {
        V3_FREQ_HI = drum_freq[drum_inst][drum_frame];
        V3_CTRL = drum_ctrl[drum_inst][drum_frame];
        ++drum_frame;
    }
}

static void waltz_init(void) {
    uint8_t i;
    for (i = 0u; i <= 0x18u; ++i) SID[i] = 0u;

    V1_AD = 0x0au; V1_SR = 0x05u;  /* piano: instant hit, 1.5 s decay */
    V2_AD = 0x96u; V2_SR = 0xa9u;  /* violin: bowed swell, held */
    V3_PW_HI = 0x08u;
    V3_FREQ_LO = 0u;

    FILTER_RES = 0xa2u;            /* resonance 10, filter voice 2 only */
    MODE_VOL = 0x1fu;              /* low-pass, full volume */

    mel_pos = 0u;    mel_left = 0u; mel_pw = 0x0600u; mel_pw_up = 1u;
    vio_pos = 0u;    vio_left = 0u; vio_gated = 0u;   vio_freq = 0u;
    vio_target = 0u; vio_step = 0u; vio_age = 0u;     vio_vib_phase = 0u;
    drum_step_left = 0u; drum_step = 0u; drum_bar = 0u;
    drum_inst = 0u; drum_frame = DRUM_FRAMES;
    filter_cut = 0x0300u; filter_up = 1u;
}

static void waltz_tick(void) {
    melody_tick();
    violin_tick();
    drum_tick();
}

#endif
