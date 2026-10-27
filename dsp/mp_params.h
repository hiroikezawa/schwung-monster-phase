/*
 * MONSTER PHASE - the knob table shared by the engine, the plugin and the
 * tests.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * module.json's chain_params must agree with this table; scripts/metadata.py
 * checks keys, order, ranges and defaults at build time.
 *
 * Knob 1 MIX is Schwung's own (the pedal has no overall dry/wet). Knobs 2-7
 * are the pedal's six GRIFFER controls GRF1-6 (four knobs, two buttons). What
 * each GRF drives is NOT a knob assignment made here: GRF1-6 are SOURCES in
 * the patch's routing graph, and their cables (traced from the screenshot,
 * see patch/routing_screenshot.json) decide what they move - in this patch
 * SAMPLE RATE, FILTER CUTOFF, FORMANT 1, EQ LOW, LPF/HPF and BYPASS. Knob 8
 * TIME has no GRF behind it: it is the one extra control, moving both SHORT
 * DELAY times together while keeping their stored difference.
 *
 * Defaults of GRF1-4 are NOT in the patch file (they are the pedal's physical
 * knob positions); the values below are chosen here, see docs/ANALYSIS.md.
 */
#ifndef MP_PARAMS_H
#define MP_PARAMS_H

enum {
    MP_P_MIX,      /* knob 1: dry/wet of the whole patch (Schwung, not original) */
    MP_P_GRF1,     /* knob 2: GRF1 knob, 0..1 */
    MP_P_GRF2,     /* knob 3: GRF2 knob, 0..1 */
    MP_P_GRF3,     /* knob 4: GRF3 knob, 0..1 */
    MP_P_GRF4,     /* knob 5: GRF4 knob, 0..1 */
    MP_P_GRF5,     /* knob 6: GRF5 button, 0/1 */
    MP_P_GRF6,     /* knob 7: GRF6 button, 0/1 */
    MP_P_TIME,     /* knob 8: SHORT DELAY TIME, panel position 0..1 */
    MP_P_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int integer;
} mp_param_info;

static const mp_param_info MP_PARAMS[MP_P_COUNT] = {
    {"mix",        0.0f, 1.0f, 1.0f,  0},
    {"g1_srate",   0.0f, 1.0f, 0.7f,  0},
    {"g2_cutoff",  0.0f, 1.0f, 0.9f,  0},
    {"g3_formant", 0.0f, 1.0f, 0.0f,  0},
    {"g4_low",     0.0f, 1.0f, 0.0f,  0},
    {"g5_type",    0.0f, 1.0f, 0.0f,  1},
    {"g6_bypass",  0.0f, 1.0f, 0.0f,  1},
    {"time",       0.0f, 1.0f, 1.0f,  0},
};

/* GRF5 is wired to LPF/HPF: the button inverts the stored type (LPF). */
static const char *const MP_TYPE_NAMES[2] = {"LPF", "HPF"};
static const char *const MP_ONOFF_NAMES[2] = {"OFF", "ON"};

#endif
