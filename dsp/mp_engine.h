/*
 * MONSTER PHASE - the effect chain.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 *   in -> PRE DELAY -> FORMANT -> CRUSH -> RESONANT LPF -> BASS -> TUBE
 *      -> + POST DELAY -> LEVEL -> MIX with the dry input -> out
 *
 * Every stage is stereo. WIDTH sets how far the two sides differ: the pre
 * delay times (3 %, as the patch's two SHORT DELAYs), the vowel, the
 * crusher's sampling clock, the post delay times and its cross feedback.
 * At WIDTH 0 a mono input stays mono.
 *
 * Coefficients follow the (smoothed) knobs every MP_CR samples; the control
 * clock counts samples, so the output does not depend on the block size.
 */
#ifndef MP_ENGINE_H
#define MP_ENGINE_H

#include <stdint.h>

#include "mp_dsp.h"
#include "mp_params.h"

#define MP_CR 16               /* control-rate period in samples */
#define MP_FORMANT_GAIN 5.0f   /* the vowel bank's make-up gain */

typedef struct {
    float fs;
    float p[MP_P_COUNT];       /* knob targets */
    float s[MP_P_COUNT];       /* smoothed (control rate) */
    float mix, level;          /* per-sample smoothed */
    float level_target;
    float k_smooth, k_mix;
    float fmt_amt;

    mp_delay pre[2];
    mp_vowel_bank vowel[2];
    mp_crush crush;
    mp_svf lpf[2];
    mp_biquad bass[2];
    mp_tube tube;
    mp_pingpong post;

    uint32_t sample;
    int nan_resets;
} mp_engine;

void mp_engine_init(mp_engine *e, float fs);
void mp_engine_set(mp_engine *e, int idx, float v);
void mp_engine_snap(mp_engine *e);    /* jump smoothing to the targets */
void mp_engine_reset(mp_engine *e);   /* clear all audio state, keep settings */
void mp_engine_process(mp_engine *e, int16_t *audio, int frames);
/* float in/out, for the tests: interleaved stereo */
void mp_engine_process_f(mp_engine *e, const float *in, float *out, int frames);

#endif
