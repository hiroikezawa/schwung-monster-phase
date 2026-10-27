/*
 * MONSTER PHASE - the knob table shared by the engine, the plugin and the
 * tests.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * module.json's chain_params must agree with this table; scripts/metadata.py
 * checks keys, order, ranges and defaults at build time. Two pages of eight:
 * the sound on page 1, the delays and the stereo image on page 2.
 */
#ifndef MP_PARAMS_H
#define MP_PARAMS_H

enum {
    /* page 1 */
    MP_P_MIX,        /* dry/wet of the whole effect */
    MP_P_CRUSH,      /* sample rate reduction, 0 = fs/256 .. 1 = fs */
    MP_P_CUTOFF,     /* resonant low-pass, 120 Hz .. 18 kHz */
    MP_P_RESO,       /* its resonance */
    MP_P_FORMANT,    /* vowel a .. i .. u .. e .. o */
    MP_P_DRIVE,      /* tube saturation */
    MP_P_BASS,       /* low shelf boost, 0 .. +12 dB */
    MP_P_DELAY,      /* post delay level */
    /* page 2 */
    MP_P_PRE_TIME,   /* pre delay, ms */
    MP_P_PRE_FDBK,
    MP_P_PRE_MIX,
    MP_P_POST_TIME,  /* post delay, ms */
    MP_P_POST_FDBK,
    MP_P_WIDTH,      /* left/right differences, 0 = both sides identical */
    MP_P_FMT_AMT,    /* how much of the formant filter is heard */
    MP_P_LEVEL,      /* output level, dB */
    MP_P_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int integer;
} mp_param_info;

static const mp_param_info MP_PARAMS[MP_P_COUNT] = {
    {"mix",       0.0f,   1.0f,    1.0f,   0},
    {"crush",     0.0f,   1.0f,    0.7f,   0},
    {"cutoff",    0.0f,   1.0f,    0.8f,   0},
    {"reso",      0.0f,   1.0f,    0.4f,   0},
    {"formant",   0.0f,   1.0f,    0.0f,   0},
    {"drive",     0.0f,   1.0f,    0.35f,  0},
    {"bass",      0.0f,   1.0f,    0.4f,   0},
    {"delay",     0.0f,   1.0f,    0.3f,   0},
    {"pre_time",  0.5f,   80.0f,   80.0f,  0},
    {"pre_fdbk",  0.0f,   1.0f,    0.61f,  0},
    {"pre_mix",   0.0f,   1.0f,    0.48f,  0},
    {"post_time", 20.0f,  1000.0f, 300.0f, 0},
    {"post_fdbk", 0.0f,   1.0f,    0.35f,  0},
    {"width",     0.0f,   1.0f,    0.6f,   0},
    {"fmt_amt",   0.0f,   1.0f,    0.6f,   0},
    {"level",     -12.0f, 12.0f,   0.0f,   0},
};

#endif
