/*
 * MONSTER PHASE - raw patch values -> DSP units (see mp_convert.h for the
 * status of every curve).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "mp_convert.h"

#include <math.h>

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float mp_raw01(int raw) { return clamp01((float)raw / 100.0f); }

float mp_sd_time_pos(int raw) { return 1.0f - mp_raw01(raw); }

float mp_sd_time_ms(float pos) { return MP_SD_MAX_MS * clamp01(pos); }

/* Loop gain equals the knob; the loop's soft saturation (mp_dsp.c) keeps
 * FEEDBACK 100 % from running away. */
float mp_sd_feedback(float n) { return clamp01(n); }

float mp_sd_wet(float n) { return clamp01(n); }

float mp_formant_vowel(int raw) {
    float v = (float)raw;
    return v < 0.0f ? 0.0f : v > 4.0f ? 4.0f : v;
}

float mp_tube_drive_db(float n) { return 36.0f * clamp01(n); }

/* Calibrated so the patch's stored OUT LEVEL (75) is unity: an assumption. */
float mp_tube_out_gain(float n) { return clamp01(n) / 0.75f; }

float mp_eq_gain_db(float n) { return (clamp01(n) - 0.5f) * 30.0f; }

float mp_comp_threshold_db(float n) { return -40.0f * clamp01(n); }

float mp_comp_ratio(float n) { return 1.0f + 19.0f * clamp01(n); }

float mp_comp_attack_ms(float n) { return 0.1f * powf(1000.0f, clamp01(n)); }

float mp_comp_release_ms(float n) { return 10.0f * powf(200.0f, clamp01(n)); }

/* Eight octaves down from the host rate at 0. */
float mp_bit_rate_hz(float n, float fs) { return fs * exp2f(-8.0f * (1.0f - clamp01(n))); }

float mp_bit_bits(float n) { return 16.0f - 14.0f * clamp01(n); }

float mp_bit_cutoff_hz(float n) { return 20.0f * powf(1000.0f, clamp01(n)); }

float mp_bit_q(float n) {
    float c = clamp01(n);
    return 0.5f + 24.5f * c * c;
}
