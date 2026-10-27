/*
 * MONSTER PHASE - the seven processing blocks of the patch: SHORT DELAY,
 * FORMANT FILTER, TUBE CLIP, 3 BAND EQ, COMPRESSOR and the BITRAZER main
 * module. Each is an approximation written from the public descriptions;
 * Roland's DSP is not published (see docs/ANALYSIS.md for what is assumed).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every block has a *_set() that recomputes coefficients from 0..1 controls
 * (called at control rate, every MP_CR samples) and a per-sample tick. No
 * block allocates; all state is inline.
 */
#ifndef MP_DSP_H
#define MP_DSP_H

#include <stdint.h>

float mp_soft(float x);   /* rational tanh, |x| -> 1 */

/* ---- state-variable filter (topology-preserving transform) ---- */
typedef struct {
    float a1, a2, a3, k;
    float ic1, ic2;
} mp_svf;
void mp_svf_set(mp_svf *f, float hz, float q, float fs);
/* low, band (k*bp: unity gain at the centre), high */
void mp_svf_tick(mp_svf *f, float x, float *lp, float *bp, float *hp);

/* ---- biquad (RBJ cookbook, transposed direct form II) ---- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} mp_biquad;
void mp_biquad_lowshelf(mp_biquad *b, float hz, float db, float fs);
void mp_biquad_peak(mp_biquad *b, float hz, float q, float db, float fs);
void mp_biquad_highshelf(mp_biquad *b, float hz, float db, float fs);
float mp_biquad_tick(mp_biquad *b, float x);

/* ---- SHORT DELAY ---- */
#define MP_DLY_SIZE 4096        /* > 80 ms at 48 kHz + interpolation room */
#define MP_DLY_MIN 3.0f         /* samples: the 4-point read needs 3 behind */
typedef struct {
    float buf[MP_DLY_SIZE];
    int w;
    float d, d_target;          /* delay in samples, glides to the target */
    float glide;                /* per-sample glide coefficient */
    float fb, wet;
} mp_delay;
void mp_delay_init(mp_delay *d, float fs);
void mp_delay_set(mp_delay *d, float time_ms, float fb, float wet, float fs);
float mp_delay_tick(mp_delay *d, float x);

/* ---- FORMANT FILTER: two vowel formant banks and a balance ---- */
typedef struct {
    mp_svf band[2][3];
    float amp[2][3];
    float balance;
} mp_formant;
void mp_formant_set(mp_formant *f, float vowel1, float vowel2, float balance, float fs);
float mp_formant_tick(mp_formant *f, float x);
/* the three formant frequencies of a (fractional) vowel index, for tests */
void mp_formant_freqs(float vowel, float hz[3]);

/* ---- TUBE CLIP: asymmetric soft clip at 4x oversampling, two channels ---- */
#define MP_OS 4
#define MP_OS_TAPS 64           /* FIR length at the oversampled rate */
typedef struct {
    float up_hist[2][2 * (MP_OS_TAPS / MP_OS)];
    int up_pos;
    float dn_hist[2][2 * MP_OS_TAPS];
    int dn_pos;
    float dc_x[2], dc_y[2], dc_r;
    float drive, out_gain;
    int os;                     /* MP_OS, or 1 (tests: no oversampling) */
} mp_tube;
void mp_tube_init(mp_tube *t, float fs);
void mp_tube_set(mp_tube *t, float gain, float out_level);
void mp_tube_tick(mp_tube *t, const float in[2], float out[2]);

/* ---- 3 BAND EQ (mono) ---- */
typedef struct {
    mp_biquad lo, mid, hi;
    float g_lo, g_mid, g_hi;    /* last applied, to skip unchanged recomputes */
} mp_eq;
void mp_eq_init(mp_eq *e);
void mp_eq_set(mp_eq *e, float hi, float mid, float lo, float fs);
float mp_eq_tick(mp_eq *e, float x);

/* ---- COMPRESSOR: feed-forward, stereo-linked peak detector ---- */
typedef struct {
    float thr_db, ratio, att, rel;
    float env_db;               /* gain reduction in dB, smoothed */
} mp_comp;
void mp_comp_set(mp_comp *c, float threshold, float ratio, float attack, float release, float fs);
void mp_comp_tick(mp_comp *c, const float in[2], float out[2]);

/* ---- BITRAZER main module ---- */
typedef struct {
    float phase, inc;           /* sample-and-hold clock */
    float held[2];
    float levels;               /* quantiser steps per unit */
    mp_svf filt[2];
    float type, type_target;    /* 0 LPF, 1 HPF, cross-faded */
    float bypass, bypass_target;
    float xfade;                /* per-sample step of the two cross-fades */
} mp_bitrazer;
void mp_bitrazer_init(mp_bitrazer *b, float fs);
void mp_bitrazer_set(mp_bitrazer *b, float rate, float bits, float cutoff, float reso,
                     int hpf, int bypass, float fs);
void mp_bitrazer_tick(mp_bitrazer *b, const float in[2], float out[2]);

#endif
