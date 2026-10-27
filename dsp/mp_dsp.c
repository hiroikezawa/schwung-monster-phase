/*
 * MONSTER PHASE - the processing blocks (see mp_dsp.h).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Written from the mathematics: TPT state-variable filters, an RBJ shelf, a
 * 4-point Hermite fractional delay read, a windowed-sinc polyphase 4x
 * resampler, a rational tanh. No code copied from anywhere.
 */
#include "mp_dsp.h"

#include <math.h>
#include <string.h>

#define MP_PI 3.14159265358979323846f

static float flush(float v) { return fabsf(v) < 1e-20f ? 0.0f : v; }

/* x(27 + x^2) / (27 + 9x^2): reaches exactly +-1 with zero slope at |x| = 3. */
float mp_soft(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---------------- SVF ---------------- */

void mp_svf_set(mp_svf *f, float hz, float q, float fs) {
    if (hz > 0.45f * fs) hz = 0.45f * fs;
    if (hz < 5.0f) hz = 5.0f;
    if (q < 0.1f) q = 0.1f;
    float g = tanf(MP_PI * hz / fs);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

void mp_svf_tick(mp_svf *f, float x, float *lp, float *bp, float *hp) {
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    if (lp) *lp = v2;
    if (bp) *bp = f->k * v1;
    if (hp) *hp = x - f->k * v1 - v2;
}

void mp_svf_flush(mp_svf *f) {
    f->ic1 = flush(f->ic1);
    f->ic2 = flush(f->ic2);
}

/* ---------------- biquad ---------------- */

void mp_biquad_lowshelf(mp_biquad *b, float hz, float db, float fs) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * MP_PI * hz / fs;
    float c = cosf(w), s = sinf(w), al = s / 2.0f * sqrtf(2.0f), sa = 2.0f * sqrtf(A) * al;
    float a0 = (A + 1) + (A - 1) * c + sa;
    b->b0 = A * ((A + 1) - (A - 1) * c + sa) / a0;
    b->b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
    b->b2 = A * ((A + 1) - (A - 1) * c - sa) / a0;
    b->a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
    b->a2 = ((A + 1) + (A - 1) * c - sa) / a0;
    b->z1 = flush(b->z1);
    b->z2 = flush(b->z2);
}

float mp_biquad_tick(mp_biquad *b, float x) {
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

/* ---------------- delays ---------------- */

static float hermite(const float *buf, int mask, float r) {
    float fl = floorf(r);
    float f = r - fl;
    int i = (int)fl;
    float xm1 = buf[(i - 1) & mask], x0 = buf[i & mask], x1 = buf[(i + 1) & mask], x2 = buf[(i + 2) & mask];
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

static float ms_to_samples(float ms, float fs, float max) {
    float s = ms * 0.001f * fs;
    return s < MP_DLY_MIN ? MP_DLY_MIN : s > max ? max : s;
}

void mp_delay_init(mp_delay *d, float fs) {
    memset(d, 0, sizeof(*d));
    d->d = d->d_target = MP_DLY_MIN;
    d->glide = 1.0f - expf(-1.0f / (0.02f * fs));   /* 20 ms time glide */
}

void mp_delay_set(mp_delay *d, float time_ms, float fb, float wet, float fs) {
    d->d_target = ms_to_samples(time_ms, fs, MP_DLY_SIZE - 4);
    d->fb = fb;
    d->wet = wet;
}

float mp_delay_tick(mp_delay *d, float x) {
    d->d += d->glide * (d->d_target - d->d);
    float y = hermite(d->buf, MP_DLY_SIZE - 1, (float)d->w - d->d);
    /* the loop saturates softly above ~0.5 so FEEDBACK 100 % cannot run away */
    d->buf[d->w] = flush(x + d->fb * 2.0f * mp_soft(0.5f * y));
    d->w = (d->w + 1) & (MP_DLY_SIZE - 1);
    return (1.0f - d->wet) * x + d->wet * y;
}

void mp_pingpong_init(mp_pingpong *p, float fs) {
    memset(p, 0, sizeof(*p));
    p->d[0] = p->d[1] = p->d_target[0] = p->d_target[1] = MP_DLY_MIN;
    p->glide = 1.0f - expf(-1.0f / (0.05f * fs));   /* 50 ms time glide */
}

void mp_pingpong_set(mp_pingpong *p, float ms_l, float ms_r, float fb, float cross, float fs) {
    p->d_target[0] = ms_to_samples(ms_l, fs, MP_PP_SIZE - 4);
    p->d_target[1] = ms_to_samples(ms_r, fs, MP_PP_SIZE - 4);
    p->fb = fb;
    p->cross = cross;
}

void mp_pingpong_tick(mp_pingpong *p, const float in[2], float wet[2]) {
    for (int c = 0; c < 2; ++c) {
        p->d[c] += p->glide * (p->d_target[c] - p->d[c]);
        wet[c] = hermite(p->buf[c], MP_PP_SIZE - 1, (float)p->w - p->d[c]);
    }
    for (int c = 0; c < 2; ++c) {
        float back = (1.0f - p->cross) * wet[c] + p->cross * wet[1 - c];
        p->buf[c][p->w] = flush(in[c] + p->fb * 2.0f * mp_soft(0.5f * back));
    }
    p->w = (p->w + 1) & (MP_PP_SIZE - 1);
}

/* ---------------- formant ---------------- */

/* Vowels in the order a, i, u, e, o: centre Hz, level dB, bandwidth Hz of the
 * first three formants (standard tenor formant tables). */
static const float VOWEL_HZ[5][3] = {
    {650, 1080, 2650}, {290, 1870, 2800}, {350, 600, 2700}, {400, 1700, 2600}, {400, 800, 2600}};
static const float VOWEL_DB[5][3] = {
    {0, -6, -7}, {0, -15, -18}, {0, -20, -17}, {0, -14, -12}, {0, -10, -12}};
static const float VOWEL_BW[5][3] = {
    {80, 90, 120}, {40, 90, 100}, {40, 60, 100}, {70, 80, 100}, {40, 80, 100}};

static void vowel_lookup(float vowel, float hz[3], float amp[3], float bw[3]) {
    if (!(vowel > 0.0f)) vowel = 0.0f;
    if (vowel > 4.0f) vowel = 4.0f;
    int i0 = (int)vowel;
    if (i0 > 3) i0 = 3;
    float t = vowel - (float)i0;
    for (int k = 0; k < 3; ++k) {
        hz[k] = expf((1 - t) * logf(VOWEL_HZ[i0][k]) + t * logf(VOWEL_HZ[i0 + 1][k]));
        amp[k] = (1 - t) * powf(10.0f, VOWEL_DB[i0][k] / 20.0f) + t * powf(10.0f, VOWEL_DB[i0 + 1][k] / 20.0f);
        bw[k] = (1 - t) * VOWEL_BW[i0][k] + t * VOWEL_BW[i0 + 1][k];
    }
}

void mp_formant_freqs(float vowel, float hz[3]) {
    float amp[3], bw[3];
    vowel_lookup(vowel, hz, amp, bw);
}

void mp_vowel_set(mp_vowel_bank *v, float vowel, float fs) {
    float hz[3], bw[3];
    vowel_lookup(vowel, hz, v->amp, bw);
    for (int k = 0; k < 3; ++k) {
        mp_svf_set(&v->band[k], hz[k], hz[k] / bw[k], fs);
        mp_svf_flush(&v->band[k]);
    }
}

float mp_vowel_tick(mp_vowel_bank *v, float x) {
    float s = 0.0f;
    for (int k = 0; k < 3; ++k) {
        float bp;
        mp_svf_tick(&v->band[k], x, NULL, &bp, NULL);
        s += v->amp[k] * bp;
    }
    return s;
}

/* ---------------- crusher ---------------- */

void mp_crush_init(mp_crush *c) {
    memset(c, 0, sizeof(*c));
    c->acc[0] = c->acc[1] = 1.0f;   /* take the first sample at once */
    c->inc = 1.0f;
}

void mp_crush_set(mp_crush *c, float hz, float offset, float fs) {
    c->inc = hz / fs;
    if (c->inc > 1.0f) c->inc = 1.0f;
    /* moving the right clock against the left keeps both running */
    float a = c->acc[1] + (offset - c->offset);
    c->acc[1] = a - floorf(a);
    c->offset = offset;
}

void mp_crush_tick(mp_crush *c, const float in[2], float out[2]) {
    /* No anti-alias filter and no bit reduction: the fold-over is the sound. */
    for (int k = 0; k < 2; ++k) {
        c->acc[k] += c->inc;
        if (c->acc[k] >= 1.0f) {
            c->acc[k] -= floorf(c->acc[k]);
            c->held[k] = in[k];
        }
        out[k] = c->held[k];
    }
}

/* ---------------- tube ---------------- */

/* One windowed-sinc low-pass serves both directions: cutoff at 0.9 of the
 * base-rate Nyquist, Blackman window, unity DC gain. */
static float g_fir[MP_OS_TAPS];
static int g_fir_ready;

static void fir_design(void) {
    float tmp[MP_OS_TAPS], sum = 0.0f;
    const float fc = 0.9f / (2.0f * MP_OS);
    for (int n = 0; n < MP_OS_TAPS; ++n) {
        float x = (float)n - (MP_OS_TAPS - 1) * 0.5f;
        float sinc = fabsf(x) < 1e-6f ? 2.0f * fc : sinf(2.0f * MP_PI * fc * x) / (MP_PI * x);
        float w = 0.42f - 0.5f * cosf(2.0f * MP_PI * n / (MP_OS_TAPS - 1)) +
                  0.08f * cosf(4.0f * MP_PI * n / (MP_OS_TAPS - 1));
        tmp[n] = sinc * w;
        sum += tmp[n];
    }
    for (int n = 0; n < MP_OS_TAPS; ++n) g_fir[n] = tmp[n] / sum;
}

void mp_tube_init(mp_tube *t, float fs) {
    /* The design is deterministic, so two instances racing here write the
     * same values; the flag only saves the work. */
    if (!__atomic_load_n(&g_fir_ready, __ATOMIC_ACQUIRE)) {
        fir_design();
        __atomic_store_n(&g_fir_ready, 1, __ATOMIC_RELEASE);
    }
    memset(t, 0, sizeof(*t));
    t->dc_r = 1.0f - 2.0f * MP_PI * 5.0f / fs;   /* 5 Hz DC blocker: the bias makes DC */
    t->drive = 1.0f;
    t->out_gain = 1.0f;
    t->os = MP_OS;
}

/* The output is scaled down as the drive goes up (drive^-0.6), so DRIVE
 * changes the saturation far more than the loudness. */
void mp_tube_set(mp_tube *t, float drive_db) {
    t->drive = powf(10.0f, drive_db / 20.0f);
    t->out_gain = powf(t->drive, -0.6f) / (1.0f - MP_TUBE_BIAS * MP_TUBE_BIAS);
}

static float tube_curve(float u) { return mp_soft(u + MP_TUBE_BIAS) - mp_soft(MP_TUBE_BIAS); }

void mp_tube_tick(mp_tube *t, const float in[2], float out[2]) {
    const int L = MP_OS_TAPS / MP_OS;
    float y[2];
    if (t->os == 1) {
        for (int c = 0; c < 2; ++c) y[c] = tube_curve(t->drive * in[c]);
    } else {
        t->up_pos = (t->up_pos + L - 1) % L;
        for (int c = 0; c < 2; ++c) {
            float *uh = t->up_hist[c];
            uh[t->up_pos] = uh[t->up_pos + L] = t->drive * in[c];
            const float *x = uh + t->up_pos;          /* x[j] = input n-j */
            float *dh = t->dn_hist[c];
            for (int p = 0; p < MP_OS; ++p) {
                float u = 0.0f;
                for (int j = 0; j < L; ++j) u += g_fir[p + MP_OS * j] * x[j];
                int pos = (t->dn_pos + MP_OS_TAPS - 1 - p) % MP_OS_TAPS;
                dh[pos] = dh[pos + MP_OS_TAPS] = tube_curve(MP_OS * u);
            }
            /* newest oversampled value at dn_pos - MP_OS, older behind it */
            const float *h = dh + (t->dn_pos + MP_OS_TAPS - MP_OS) % MP_OS_TAPS;
            float s = 0.0f;
            for (int k = 0; k < MP_OS_TAPS; ++k) s += g_fir[k] * h[k];
            y[c] = s;
        }
        t->dn_pos = (t->dn_pos + MP_OS_TAPS - MP_OS) % MP_OS_TAPS;
    }
    for (int c = 0; c < 2; ++c) {
        float v = t->out_gain * y[c];
        float d = v - t->dc_x[c] + t->dc_r * t->dc_y[c];
        t->dc_x[c] = v;
        t->dc_y[c] = flush(d);
        out[c] = d;
    }
}
