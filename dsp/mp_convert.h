/*
 * MONSTER PHASE - raw patch values -> DSP units, versioned.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The patch file stores raw bytes, not milliseconds or decibels. Every
 * conversion from a raw byte (or a 0..1 panel position) to a physical unit
 * lives here and nowhere else, so a better-informed curve replaces one
 * function and bumps MP_CONVERT_VERSION, and the tests pin each curve.
 *
 * Status of each mapping (also in docs/ANALYSIS.md):
 *   RANGE  raw 0..100 = knob travel             INFERRED from the screenshot
 *          (FEEDBACK 61, DRY/WET 48, GAIN 27, OUT LEVEL 75, EQ 50 and the
 *          main module's 100s all sit where 0..100 puts them)
 *   TIME   panel position = 1 - raw/100         INFERRED (screenshot: both TIME
 *          knobs near clockwise maximum while raw = 3 and 0); unproven
 *   TIME   ms = 80 * position                   PROVISIONAL (80 ms maximum is
 *          Roland's figure; the curve shape is unknown, linear chosen)
 *   FORMANT raw = vowel index a,i,u,e,o (0..4)  INFERRED (raw 0 and 1 against
 *          the a/i/u/e/o legend and the pointers); range beyond 1 unknown
 *   everything else                             PROVISIONAL (curves and end
 *          points chosen here, not Roland's)
 */
#ifndef MP_CONVERT_H
#define MP_CONVERT_H

#define MP_CONVERT_VERSION 1

#define MP_SD_MAX_MS 80.0f   /* Roland: SHORT DELAY up to 80 ms */

float mp_raw01(int raw);                  /* raw 0..100 -> 0..1 (clamped) */

/* SHORT DELAY */
float mp_sd_time_pos(int raw);            /* raw -> panel position (reversed) */
float mp_sd_time_ms(float pos);           /* panel position -> ms */
float mp_sd_feedback(float n);            /* 0..1 -> loop gain */
float mp_sd_wet(float n);                 /* 0..1 -> wet share (linear crossfade) */

/* FORMANT FILTER */
float mp_formant_vowel(int raw);          /* raw -> vowel index 0..4 */
#define MP_FORMANT_CV_SPAN 4.0f           /* CV 0..1 sweeps all five vowels */

/* TUBE CLIP */
float mp_tube_drive_db(float n);          /* GAIN 0..1 -> input drive dB */
float mp_tube_out_gain(float n);          /* OUT LEVEL 0..1 -> linear */
#define MP_TUBE_BIAS 0.25f                /* asymmetry -> even harmonics */

/* 3 BAND EQ */
float mp_eq_gain_db(float n);             /* 0..1 -> -15..+15 dB, 0.5 = flat */
#define MP_EQ_LOW_HZ 120.0f
#define MP_EQ_MID_HZ 1000.0f
#define MP_EQ_MID_Q  0.7f
#define MP_EQ_HI_HZ  6000.0f

/* COMPRESSOR */
float mp_comp_threshold_db(float n);      /* 0 -> 0 dBFS, 1 -> -40 dBFS */
float mp_comp_ratio(float n);             /* 0 -> 1:1, 1 -> 20:1 */
float mp_comp_attack_ms(float n);         /* 0.1 .. 100 ms, exponential */
float mp_comp_release_ms(float n);        /* 10 .. 2000 ms, exponential */

/* BITRAZER main module */
float mp_bit_rate_hz(float n, float fs);  /* SAMPLE RATE 0..1 -> hold rate */
float mp_bit_bits(float n);               /* BIT DOWN 0..1 -> 16 .. 2 bits */
float mp_bit_cutoff_hz(float n);          /* FILTER CUTOFF 0..1 -> 20 Hz .. 20 kHz */
float mp_bit_q(float n);                  /* RESONANCE 0..1 -> Q 0.5 .. 25 */

#endif
