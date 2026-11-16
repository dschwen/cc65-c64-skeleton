#ifndef MOONFEN_WALTZ_STATE_H
#define MOONFEN_WALTZ_STATE_H

#include <stdint.h>

/* Mutable state of the Moonfen Waltz player (music/waltz_player.h). Kept
 * in its own header so a banked includer (modules/title.c) can declare it
 * before its bank pragmas - resident RAM, not the read-only bank. */
static uint16_t mel_pos;
static uint16_t mel_left;
static uint16_t mel_pw;
static uint8_t mel_pw_up;

static uint16_t vio_pos;
static uint16_t vio_left;
static uint8_t vio_release;
static uint8_t vio_gated;
static uint16_t vio_freq;
static uint16_t vio_target;
static uint16_t vio_step;
static uint8_t vio_age;
static uint8_t vio_vib_phase;

static uint8_t drum_step_left;
static uint8_t drum_step;
static uint8_t drum_bar;
static uint8_t drum_inst;
static uint8_t drum_frame;

static uint16_t filter_cut;
static uint8_t filter_up;

#endif
