/*
 * MONSTER PHASE - the processing blocks (see mp_dsp.h).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Written from the mathematics: TPT state-variable filters, RBJ biquads, a
 * 4-point Hermite fractional delay read, a windowed-sinc polyphase 4x
 * resampler, a rational tanh. No code copied from anywhere.
 */
#include "mp_dsp.h"

#include <math.h>
#include <string.h>

#include "mp_convert.h"

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

static void svf_flush(mp_svf *f) {
    f->ic1 = flush(f->ic1);
    f->ic2 = flush(f->ic2);
}

/* ---------------- biquad ---------------- */

static void biquad_norm(mp_biquad *b, float b0, float b1, float b2, float a0, float a1, float a2) {
    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0;
    b->a1 = a1 / a0; b->a2 = a2 / a0;
}

void mp_biquad_lowshelf(mp_biquad *b, float hz, float db, float fs) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * MP_PI * hz / fs;
    float c = cosf(w), s = sinf(w), al = s / 2.0f * sqrtf(2.0f), sa = 2.0f * sqrtf(A) * al;
    biquad_norm(b, A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c),
                A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa,
                -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}

void mp_biquad_highshelf(mp_biquad *b, float hz, float db, float fs) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * MP_PI * hz / fs;
    float c = cosf(w), s = sinf(w), al = s / 2.0f * sqrtf(2.0f), sa = 2.0f * sqrtf(A) * al;
    biquad_norm(b, A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
                A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
                2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
}

void mp_biquad_peak(mp_biquad *b, float hz, float q, float db, float fs) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * MP_PI * hz / fs;
    float c = cosf(w), al = sinf(w) / (2.0f * q);
    biquad_norm(b, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

float mp_biquad_tick(mp_biquad *b, float x) {
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

/* ---------------- SHORT DELAY ---------------- */

void mp_delay_init(mp_delay *d, float fs) {
    memset(d->buf, 0, sizeof(d->buf));
    d->w = 0;
    d->d = d->d_target = MP_DLY_MIN;
    d->glide = 1.0f - expf(-1.0f / (0.02f * fs));   /* 20 ms time glide */
    d->fb = d->wet = 0.0f;
}

void mp_delay_set(mp_delay *d, float time_ms, float fb, float wet, float fs) {
    float s = time_ms * 0.001f * fs;
    if (s < MP_DLY_MIN) s = MP_DLY_MIN;
    if (s > MP_DLY_SIZE - 4) s = MP_DLY_SIZE - 4;
    d->d_target = s;
    d->fb = fb;
    d->wet = wet;
}

float mp_delay_tick(mp_delay *d, float x) {
    d->d += d->glide * (d->d_target - d->d);
    float r = (float)d->w - d->d;
    float fl = floorf(r);
    float f = r - fl;
    int i = (int)fl;
    const int m = MP_DLY_SIZE - 1;
    float xm1 = d->buf[(i - 1) & m], x0 = d->buf[i & m], x1 = d->buf[(i + 1) & m], x2 = d->buf[(i + 2) & m];
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    float y = ((c3 * f + c2) * f + c1) * f + x0;
    /* the loop saturates softly above ~0.5 so FEEDBACK 100 % cannot run away */
    d->buf[d->w] = flush(x + d->fb * 2.0f * mp_soft(0.5f * y));
    d->w = (d->w + 1) & m;
    return (1.0f - d->wet) * x + d->wet * y;
}

/* ---------------- FORMANT FILTER ---------------- */

/* Vowels in the panel's order a, i, u, e, o: centre Hz, level dB, bandwidth Hz
 * of the first three formants (standard tenor formant tables). Roland's own
 * table is not published. */
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

void mp_formant_set(mp_formant *f, float vowel1, float vowel2, float balance, float fs) {
    const float v[2] = {vowel1, vowel2};
    for (int j = 0; j < 2; ++j) {
        float hz[3], bw[3];
        vowel_lookup(v[j], hz, f->amp[j], bw);
        for (int k = 0; k < 3; ++k) {
            mp_svf_set(&f->band[j][k], hz[k], hz[k] / bw[k], fs);
            svf_flush(&f->band[j][k]);
        }
    }
    f->balance = balance < 0 ? 0 : balance > 1 ? 1 : balance;
}

float mp_formant_tick(mp_formant *f, float x) {
    float out[2];
    for (int j = 0; j < 2; ++j) {
        float s = 0.0f;
        for (int k = 0; k < 3; ++k) {
            float bp;
            mp_svf_tick(&f->band[j][k], x, NULL, &bp, NULL);
            s += f->amp[j][k] * bp;
        }
        out[j] = s;
    }
    return (1.0f - f->balance) * out[0] + f->balance * out[1];
}

/* ---------------- TUBE CLIP ---------------- */

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

void mp_tube_set(mp_tube *t, float gain, float out_level) {
    t->drive = powf(10.0f, mp_tube_drive_db(gain) / 20.0f);
    t->out_gain = mp_tube_out_gain(out_level);
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

/* ---------------- 3 BAND EQ ---------------- */

void mp_eq_init(mp_eq *e) {
    memset(e, 0, sizeof(*e));
    e->g_lo = e->g_mid = e->g_hi = 1e9f;   /* force the first set */
}

void mp_eq_set(mp_eq *e, float hi, float mid, float lo, float fs) {
    float dl = mp_eq_gain_db(lo), dm = mp_eq_gain_db(mid), dh = mp_eq_gain_db(hi);
    if (fabsf(dl - e->g_lo) > 1e-4f) { mp_biquad_lowshelf(&e->lo, MP_EQ_LOW_HZ, dl, fs); e->g_lo = dl; }
    if (fabsf(dm - e->g_mid) > 1e-4f) { mp_biquad_peak(&e->mid, MP_EQ_MID_HZ, MP_EQ_MID_Q, dm, fs); e->g_mid = dm; }
    if (fabsf(dh - e->g_hi) > 1e-4f) { mp_biquad_highshelf(&e->hi, MP_EQ_HI_HZ, dh, fs); e->g_hi = dh; }
    mp_biquad *b[3] = {&e->lo, &e->mid, &e->hi};
    for (int i = 0; i < 3; ++i) { b[i]->z1 = flush(b[i]->z1); b[i]->z2 = flush(b[i]->z2); }
}

float mp_eq_tick(mp_eq *e, float x) {
    return mp_biquad_tick(&e->hi, mp_biquad_tick(&e->mid, mp_biquad_tick(&e->lo, x)));
}

/* ---------------- COMPRESSOR ---------------- */

void mp_comp_set(mp_comp *c, float threshold, float ratio, float attack, float release, float fs) {
    c->thr_db = mp_comp_threshold_db(threshold);
    c->ratio = mp_comp_ratio(ratio);
    c->att = expf(-1.0f / (mp_comp_attack_ms(attack) * 0.001f * fs));
    c->rel = expf(-1.0f / (mp_comp_release_ms(release) * 0.001f * fs));
    c->env_db = flush(c->env_db);
}

void mp_comp_tick(mp_comp *c, const float in[2], float out[2]) {
    float target = 0.0f;
    if (c->ratio > 1.0001f) {
        float peak = fmaxf(fabsf(in[0]), fabsf(in[1]));
        /* 20 log10 via logf: log10f carries a GLIBC_2.43 version the Move lacks */
        float over = 8.68588964f * logf(peak + 1e-12f) - c->thr_db;
        if (over > 0.0f) target = over * (1.0f - 1.0f / c->ratio);
    }
    float a = target > c->env_db ? c->att : c->rel;
    c->env_db = a * c->env_db + (1.0f - a) * target;
    float g = expf(-c->env_db * 0.115129255f);   /* 10^(-dB/20) */
    out[0] = in[0] * g;
    out[1] = in[1] * g;
}

/* ---------------- BITRAZER ---------------- */

void mp_bitrazer_init(mp_bitrazer *b, float fs) {
    memset(b, 0, sizeof(*b));
    b->phase = 1.0f;
    b->inc = 1.0f;
    b->levels = 32768.0f;
    b->xfade = 1.0f / (0.005f * fs);   /* 5 ms: the LPF/HPF and BYPASS switches */
    for (int c = 0; c < 2; ++c) mp_svf_set(&b->filt[c], 0.45f * fs, 0.5f, fs);
}

void mp_bitrazer_set(mp_bitrazer *b, float rate, float bits, float cutoff, float reso,
                     int hpf, int bypass, float fs) {
    b->inc = mp_bit_rate_hz(rate, fs) / fs;
    if (b->inc > 1.0f) b->inc = 1.0f;
    b->levels = exp2f(mp_bit_bits(bits) - 1.0f);
    for (int c = 0; c < 2; ++c) {
        mp_svf_set(&b->filt[c], mp_bit_cutoff_hz(cutoff), mp_bit_q(reso), fs);
        svf_flush(&b->filt[c]);
    }
    b->type_target = hpf ? 1.0f : 0.0f;
    b->bypass_target = bypass ? 1.0f : 0.0f;
}

static float approach(float v, float target, float step) {
    if (v < target) return v + step > target ? target : v + step;
    if (v > target) return v - step < target ? target : v - step;
    return v;
}

void mp_bitrazer_tick(mp_bitrazer *b, const float in[2], float out[2]) {
    /* Sample rate reduction: a free-running sample-and-hold, no anti-alias
     * filter, so the fold-over IS the sound. The held value is quantised at
     * the instant it is taken (mid-tread). */
    b->phase += b->inc;
    if (b->phase >= 1.0f) {
        b->phase -= 1.0f;
        for (int c = 0; c < 2; ++c) b->held[c] = floorf(in[c] * b->levels + 0.5f) / b->levels;
    }
    b->type = approach(b->type, b->type_target, b->xfade);
    b->bypass = approach(b->bypass, b->bypass_target, b->xfade);
    for (int c = 0; c < 2; ++c) {
        float lp, hp;
        mp_svf_tick(&b->filt[c], b->held[c], &lp, NULL, &hp);
        float wet = (1.0f - b->type) * lp + b->type * hp;
        out[c] = b->bypass * in[c] + (1.0f - b->bypass) * wet;
    }
}
