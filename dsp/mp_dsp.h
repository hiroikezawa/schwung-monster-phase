/*
 * MONSTER PHASE - the processing blocks: pre delay, formant filter, crusher,
 * resonant low-pass, bass shelf, tube saturation, post delay.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every block has a *_set() that recomputes coefficients (called at control
 * rate, every MP_CR samples) and a per-sample tick. No block allocates; all
 * state is inline.
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
void mp_svf_flush(mp_svf *f);

/* ---- biquad (RBJ cookbook, transposed direct form II) ---- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} mp_biquad;
void mp_biquad_lowshelf(mp_biquad *b, float hz, float db, float fs);
float mp_biquad_tick(mp_biquad *b, float x);

/* ---- mono delay line with feedback and dry/wet (the pre delay) ---- */
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

/* ---- stereo delay with cross feedback (the post delay); returns wet only ---- */
#define MP_PP_SIZE 65536        /* > 1.25 s at 48 kHz */
typedef struct {
    float buf[2][MP_PP_SIZE];
    int w;
    float d[2], d_target[2], glide;
    float fb, cross;
} mp_pingpong;
void mp_pingpong_init(mp_pingpong *p, float fs);
void mp_pingpong_set(mp_pingpong *p, float ms_l, float ms_r, float fb, float cross, float fs);
void mp_pingpong_tick(mp_pingpong *p, const float in[2], float wet[2]);

/* ---- vowel formant bank: three resonances of a (fractional) vowel ---- */
typedef struct {
    mp_svf band[3];
    float amp[3];
} mp_vowel_bank;
void mp_vowel_set(mp_vowel_bank *v, float vowel, float fs);
float mp_vowel_tick(mp_vowel_bank *v, float x);
void mp_formant_freqs(float vowel, float hz[3]);

/* ---- crusher: sample-and-hold with a right-side clock offset ---- */
typedef struct {
    float acc[2], inc, offset;
    float held[2];
} mp_crush;
void mp_crush_init(mp_crush *c);
void mp_crush_set(mp_crush *c, float hz, float offset, float fs);
void mp_crush_tick(mp_crush *c, const float in[2], float out[2]);

/* ---- tube: asymmetric soft clip at 4x oversampling, two channels ---- */
#define MP_OS 4
#define MP_OS_TAPS 64           /* FIR length at the oversampled rate */
#define MP_TUBE_BIAS 0.25f      /* asymmetry -> even harmonics */
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
void mp_tube_set(mp_tube *t, float drive_db);
void mp_tube_tick(mp_tube *t, const float in[2], float out[2]);

#endif
