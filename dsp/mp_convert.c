/*
 * MONSTER PHASE - knob values -> DSP units (see mp_convert.h).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "mp_convert.h"

#include <math.h>

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float mp_crush_hz(float n, float fs) { return fs * exp2f(-8.0f * (1.0f - clamp01(n))); }

/* 120 Hz at 0: the bottom of the sweep still passes the bass, so the sound
 * never vanishes (0.1.0 went down to 20 Hz). */
float mp_cutoff_hz(float n) { return 120.0f * powf(150.0f, clamp01(n)); }

float mp_reso_q(float n) {
    float c = clamp01(n);
    return 0.6f + 13.4f * c * c;
}

float mp_vowel(float n) { return 4.0f * clamp01(n); }

float mp_drive_db(float n) { return 30.0f * clamp01(n); }

float mp_bass_db(float n) { return 12.0f * clamp01(n); }

float mp_db_to_gain(float db) { return powf(10.0f, db / 20.0f); }
