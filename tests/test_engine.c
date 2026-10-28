/*
 * MONSTER PHASE - engine, block and plugin tests (desktop).
 *
 *   test-engine [--quick]
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "mp_convert.h"
#include "mp_engine.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

#define FS 44100.0f
#define PI 3.14159265358979323846

static int g_fail, g_quick;

static void check(int cond, const char *what, double got) {
    printf("%s  %-62s %g\n", cond ? "ok  " : "FAIL", what, got);
    if (!cond) g_fail++;
}

static uint32_t rng = 1;
static float noise(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* amplitude of the component at hz in x[0..n) (n should hold whole cycles) */
static double tone_amp(const float *x, int n, int stride, double hz, double fs) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) {
        double ph = 2 * PI * hz * i / fs;
        re += x[i * stride] * cos(ph);
        im += x[i * stride] * sin(ph);
    }
    return 2.0 * sqrt(re * re + im * im) / n;
}

static double db(double v) { return 20.0 * log10(v + 1e-30); }

static mp_engine E, E2;   /* large: keep off the stack */

/* Music-like material: saw bass, a chord, noise hits. */
static float music(int i) {
    double t = i / 44100.0, s = 0.25 * (2 * fmod(55 * t, 1) - 1);
    s += 0.08 * (sin(2 * PI * 220 * t) + sin(2 * PI * 277 * t) + sin(2 * PI * 330 * t));
    if (fmod(t, 0.25) < 0.02) s += 0.2 * noise();
    return (float)s;
}

/* Output/input power ratio (dB) and side/mid ratio (dB) over 2 s of music
 * (after 1 s), with one knob set. */
static double loudness(int idx, float v, double *side) {
    mp_engine_init(&E, FS);
    if (idx >= 0) mp_engine_set(&E, idx, v);
    mp_engine_snap(&E);
    static float in[256], out[256];
    double ei = 0, eo = 0, es = 0, em = 0;
    for (int b = 0; b < 3 * 344; ++b) {
        for (int k = 0; k < 128; ++k) in[2 * k] = in[2 * k + 1] = music(b * 128 + k);
        mp_engine_process_f(&E, in, out, 128);
        if (b < 344) continue;
        for (int k = 0; k < 128; ++k) {
            double l = out[2 * k], r = out[2 * k + 1];
            ei += in[2 * k] * in[2 * k];
            eo += 0.5 * (l * l + r * r);
            es += 0.25 * (l - r) * (l - r);
            em += 0.25 * (l + r) * (l + r);
        }
    }
    if (side) *side = 10 * log10(es / em + 1e-30);
    return 10 * log10(eo / ei);
}

/* ------------------------------------------------------------------ */

static void test_conversions(void) {
    puts("== knob curves (MP_CONVERT_VERSION 2)");
    check(MP_CONVERT_VERSION == 2, "curve table version", MP_CONVERT_VERSION);
    check(fabsf(mp_crush_hz(1.0f, FS) - FS) < 0.01f && fabsf(mp_crush_hz(0.0f, FS) - FS / 256) < 0.01f,
          "CRUSH 0..1 -> fs/256 .. fs (as 0.1.0's knob 2)", mp_crush_hz(0.7f, FS));
    check(fabsf(mp_cutoff_hz(0.0f) - 120.0f) < 0.01f && fabsf(mp_cutoff_hz(1.0f) - 18000.0f) < 1.0f,
          "CUTOFF 120 Hz .. 18 kHz (never below the bass)", mp_cutoff_hz(0.8f));
    check(fabsf(mp_reso_q(0.0f) - 0.6f) < 1e-6f && fabsf(mp_reso_q(1.0f) - 14.0f) < 1e-5f, "RESO Q 0.6 .. 14", mp_reso_q(0.4f));
    check(mp_vowel(0.25f) == 1.0f && mp_vowel(1.0f) == 4.0f, "FORMANT 0..1 -> vowel a i u e o", mp_vowel(0.5f));
    check(mp_bass_db(1.0f) == 12.0f && mp_drive_db(1.0f) == 30.0f, "BASS +12 dB, DRIVE +30 dB at the top", 0);
    float hz[3];
    mp_formant_freqs(0.0f, hz);
    check(fabsf(hz[0] - 650.0f) < 0.01f && fabsf(hz[1] - 1080.0f) < 0.01f, "vowel a: F1 650, F2 1080 Hz", hz[0]);
}

static void test_delays(void) {
    puts("== delays");
    static mp_delay d;
    static float y[44100];
    mp_delay_init(&d, FS);
    mp_delay_set(&d, 10.0f, 0.0f, 1.0f, FS);
    d.d = d.d_target;
    for (int i = 0; i < 2000; ++i) y[i] = mp_delay_tick(&d, i == 0 ? 1.0f : 0.0f);
    double rest = 0;
    for (int i = 0; i < 2000; ++i) if (i != 441) rest += fabs(y[i]);
    check(fabsf(y[441] - 1.0f) < 1e-3f && rest < 1e-3, "pre delay: 10 ms = 441 samples, clean impulse", rest);

    mp_delay_init(&d, FS);
    mp_delay_set(&d, 10.0f, 0.5f, 1.0f, FS);
    d.d = d.d_target;
    for (int i = 0; i < 44100; ++i) y[i] = mp_delay_tick(&d, i == 0 ? 0.01f : 0.0f);
    check(fabs(y[882] / y[441] - 0.5) < 1e-3, "pre delay FEEDBACK 0.5: each echo half the last", y[882] / y[441]);

    /* 50 % dry/wet of a 44-sample delay is a comb, notch at fs/88 */
    double amp[2];
    const double f[2] = {FS / 88.0, FS / 44.0};
    for (int k = 0; k < 2; ++k) {
        mp_delay_init(&d, FS);
        mp_delay_set(&d, 44.0f / 44.1f, 0.0f, 0.5f, FS);
        d.d = d.d_target;
        for (int i = 0; i < 8820; ++i) y[i] = mp_delay_tick(&d, (float)sin(2 * PI * f[k] * i / FS));
        amp[k] = tone_amp(y + 4410, 4400, 1, f[k], FS);
    }
    check(db(amp[0]) < -40.0 && fabs(db(amp[1])) < 0.1, "pre delay MIX 50 %: comb notch / peak (dB)", db(amp[0]));

    mp_delay_init(&d, FS);
    mp_delay_set(&d, 5.0f, 1.0f, 1.0f, FS);
    d.d = d.d_target;
    float mx = 0;
    int finite = 1;
    for (int i = 0; i < 44100 * 4; ++i) {
        float v = mp_delay_tick(&d, i < 4410 ? noise() : 0.0f);
        finite &= isfinite(v);
        mx = fmaxf(mx, fabsf(v));
    }
    check(finite && mx <= 3.0f, "FEEDBACK 100 %: bounded (|in| 1 + loop saturation 2)", mx);

    /* post delay: L and R times, cross feedback */
    static mp_pingpong p;
    static float wl[44100], wr[44100];
    mp_pingpong_init(&p, FS);
    mp_pingpong_set(&p, 100.0f, 120.0f, 0.5f, 0.0f, FS);
    p.d[0] = p.d_target[0];
    p.d[1] = p.d_target[1];
    for (int i = 0; i < 44100; ++i) {
        float in[2] = {i == 0 ? 0.01f : 0.0f, i == 0 ? 0.01f : 0.0f}, w[2];
        mp_pingpong_tick(&p, in, w);
        wl[i] = w[0];
        wr[i] = w[1];
    }
    check(fabsf(wl[4410] - 0.01f) < 1e-5f && fabsf(wr[5292] - 0.01f) < 1e-4f,
          "post delay: left 100 ms, right 120 ms", wr[5292]);
    check(fabsf(wl[8820] / wl[4410] - 0.5f) < 1e-3f, "post delay FEEDBACK 0.5 on its own side", wl[8820] / wl[4410]);

    mp_pingpong_init(&p, FS);
    mp_pingpong_set(&p, 100.0f, 100.0f, 0.5f, 1.0f, FS);
    p.d[0] = p.d[1] = p.d_target[0];
    for (int i = 0; i < 44100; ++i) {
        float in[2] = {i == 0 ? 0.01f : 0.0f, 0.0f}, w[2];
        mp_pingpong_tick(&p, in, w);
        wl[i] = w[0];
        wr[i] = w[1];
    }
    check(fabsf(wl[4410] - 0.01f) < 1e-5f && fabsf(wr[8820] - 0.005f) < 1e-5f && fabsf(wl[8820]) < 1e-7f,
          "post delay full cross: left echo bounces to the right", wr[8820]);
}

static void test_filters(void) {
    puts("== filters");
    static mp_svf f;
    static float y[8820];
    double peak[2];
    for (int k = 0; k < 2; ++k) {
        memset(&f, 0, sizeof(f));
        mp_svf_set(&f, 1000.0f, mp_reso_q(k ? 1.0f : 0.0f), FS);
        for (int i = 0; i < 8820; ++i) mp_svf_tick(&f, (float)sin(2 * PI * 1000.0 * i / FS), &y[i], NULL, NULL);
        peak[k] = db(tone_amp(y + 4410, 4410, 1, 1000.0, FS));
    }
    check(peak[1] - peak[0] > 25.0, "RESO 100 % lifts the cutoff by > 25 dB over RESO 0", peak[1] - peak[0]);

    memset(&f, 0, sizeof(f));
    mp_svf_set(&f, mp_cutoff_hz(0.0f), mp_reso_q(0.0f), FS);
    for (int i = 0; i < 8820; ++i) mp_svf_tick(&f, (float)sin(2 * PI * 55.0 * i / FS), &y[i], NULL, NULL);
    double bass = db(tone_amp(y + 4410, 4410, 1, 55.0, FS));
    check(bass > -3.0, "CUTOFF 0: 55 Hz still passes (dB)", bass);

    memset(&f, 0, sizeof(f));
    float mx = 0;
    int finite = 1;
    for (int i = 0; i < 44100 * (g_quick ? 2 : 10); ++i) {
        if (i % 16 == 0) mp_svf_set(&f, mp_cutoff_hz(0.5f + 0.5f * noise()), mp_reso_q(1.0f), FS);
        float lp;
        mp_svf_tick(&f, 0.5f * noise(), &lp, NULL, NULL);
        finite &= isfinite(lp);
        mx = fmaxf(mx, fabsf(lp));
    }
    check(finite && mx < 60.0f, "LPF: random cutoff every 16 samples at RESO 100 %", mx);

    static mp_vowel_bank v;
    memset(&v, 0, sizeof(v));
    mp_vowel_set(&v, 0.0f, FS);
    const double fq[3] = {650.0, 1080.0, 4000.0};
    double a[3];
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 8820; ++i) y[i] = mp_vowel_tick(&v, (float)sin(2 * PI * fq[k] * i / FS));
        a[k] = tone_amp(y + 4410, 4410, 1, fq[k], FS);
    }
    check(a[0] > 0.8 && a[1] > 0.3 && a[2] < 0.1, "vowel a: resonant at 650 / 1080 Hz, low at 4 kHz", a[0]);
    memset(&v, 0, sizeof(v));
    mx = 0;
    for (int i = 0; i < 44100 * 2; ++i) {
        if (i % 16 == 0) mp_vowel_set(&v, 2.0f + 2.0f * noise(), FS);
        float o = mp_vowel_tick(&v, 0.5f * noise());
        finite &= isfinite(o);
        mx = fmaxf(mx, fabsf(o));
    }
    check(finite && mx < 10.0f, "vowel bank: vowel jumping every 16 samples", mx);

    static mp_biquad b;
    memset(&b, 0, sizeof(b));
    mp_biquad_lowshelf(&b, MP_BASS_HZ, mp_bass_db(1.0f), FS);
    for (int i = 0; i < 8820; ++i) y[i] = mp_biquad_tick(&b, (float)sin(2 * PI * 30.0 * i / FS));
    double lo = db(tone_amp(y + 4410, 4410, 1, 30.0, FS));
    for (int i = 0; i < 8820; ++i) y[i] = mp_biquad_tick(&b, (float)sin(2 * PI * 3000.0 * i / FS));
    double hi = db(tone_amp(y + 4410, 4410, 1, 3000.0, FS));
    check(fabs(lo - 12.0) < 1.0 && fabs(hi) < 0.3, "BASS 100 %: +12 dB at 30 Hz, flat at 3 kHz", lo);
}

static void test_tube(void) {
    puts("== tube");
    static mp_tube t;
    static float y[2 * 44100];
    mp_tube_init(&t, FS);
    mp_tube_set(&t, mp_drive_db(0.35f));
    for (int i = 0; i < 44100; ++i) {
        float s = 0.5f * (float)sin(2 * PI * i / 40.0), in[2] = {s, s};
        mp_tube_tick(&t, in, y + 2 * i);
    }
    double h1 = tone_amp(y + 2 * 40000, 4000, 2, FS / 40, FS);
    double h2 = tone_amp(y + 2 * 40000, 4000, 2, 2 * FS / 40, FS);
    double h3 = tone_amp(y + 2 * 40000, 4000, 2, 3 * FS / 40, FS);
    printf("     DRIVE 35 %%: H2 %.1f dBc  H3 %.1f dBc\n", db(h2 / h1), db(h3 / h1));
    check(db(h2 / h1) > -40.0 && db(h3 / h1) > -50.0, "asymmetric: even and odd harmonics", db(h2 / h1));
    double dc = 0;
    for (int i = 40000; i < 44000; ++i) dc += y[2 * i];
    check(fabs(dc / 4000) < 1e-4, "DC from the bias removed", dc / 4000);

    const double alias[4] = {16100, 9100, 2100, 4900};
    double al[2];
    for (int k = 0; k < 2; ++k) {
        mp_tube_init(&t, FS);
        t.os = k ? MP_OS : 1;
        mp_tube_set(&t, 18.0f);
        for (int i = 0; i < 8000; ++i) {
            float s = 0.5f * (float)sin(2 * PI * 7000.0 * i / FS), in[2] = {s, s};
            mp_tube_tick(&t, in, y + 2 * i);
        }
        double fund = tone_amp(y + 2 * 3590, 4410, 2, 7000, FS), e = 0;
        for (int j = 0; j < 4; ++j) { double a = tone_amp(y + 2 * 3590, 4410, 2, alias[j], FS); e += a * a; }
        al[k] = db(sqrt(e) / fund);
    }
    printf("     aliases of 7 kHz (dBc): no oversampling %.1f, 4x %.1f\n", al[0], al[1]);
    check(al[1] < al[0] - 15.0, "4x oversampling lowers the fold-over by > 15 dB", al[0] - al[1]);
}

static void test_crush(void) {
    puts("== crusher");
    static mp_crush c;
    mp_crush_init(&c);
    mp_crush_set(&c, mp_crush_hz(5.0f / 8.0f, FS), 0.0f, FS);   /* fs / 8 */
    int changes = 0;
    float last = -9;
    for (int i = 0; i < 8000; ++i) {
        float in[2] = {i / 8000.0f, 0}, out[2];
        mp_crush_tick(&c, in, out);
        if (out[0] != last) { changes++; last = out[0]; }
    }
    check(abs(changes - 1000) <= 1, "CRUSH 5/8 -> held every 8 samples", changes);

    mp_crush_init(&c);
    mp_crush_set(&c, mp_crush_hz(1.0f, FS), 0.5f, FS);
    int same = 1;
    for (int i = 0; i < 4410; ++i) {
        float in[2] = {noise(), noise()}, out[2];
        mp_crush_tick(&c, in, out);
        same &= out[0] == in[0] && out[1] == in[1];
    }
    check(same, "CRUSH 100 %: transparent, no bit reduction (exact)", same);

    mp_crush_init(&c);
    mp_crush_set(&c, FS / 8, 0.5f, FS);
    int apart = 0;
    float pl = 0, pr = 0;
    for (int i = 0; i < 800; ++i) {
        float in[2] = {(float)i, (float)i}, out[2];
        mp_crush_tick(&c, in, out);
        if ((out[0] != pl) != (out[1] != pr)) apart++;
        pl = out[0];
        pr = out[1];
    }
    check(apart > 150, "crusher clock offset: left and right hold at different samples", apart);
}

static void test_engine(void) {
    puts("== whole effect");
    double side;
    double base = loudness(-1, 0, &side);
    printf("     defaults: %.1f dB, side/mid %.1f dB\n", base, side);
    check(fabs(base) < 6.0, "defaults within 6 dB of the input", base);
    check(side > -12.0, "defaults are clearly stereo (side/mid, dB)", side);

    loudness(MP_P_WIDTH, 0.0f, &side);
    check(side < -100.0, "WIDTH 0: a mono input stays exactly mono", side);

    const int knobs[] = {MP_P_CRUSH, MP_P_CUTOFF, MP_P_RESO, MP_P_FORMANT, MP_P_DRIVE, MP_P_BASS, MP_P_DELAY,
                         MP_P_PRE_TIME, MP_P_PRE_FDBK, MP_P_PRE_MIX, MP_P_POST_TIME, MP_P_POST_FDBK,
                         MP_P_WIDTH, MP_P_FMT_AMT};
    double lo_all = 99, hi_all = -99;
    for (unsigned k = 0; k < sizeof(knobs) / sizeof(knobs[0]); ++k)
        for (int end = 0; end < 2; ++end) {
            const mp_param_info *pi = &MP_PARAMS[knobs[k]];
            double g = loudness(knobs[k], end ? pi->max : pi->min, NULL);
            if (g < lo_all) lo_all = g;
            if (g > hi_all) hi_all = g;
            printf("     %-10s %s %6.1f dB\n", pi->key, end ? "max" : "min", g);
        }
    check(lo_all > -12.0 && hi_all < 9.0, "every knob at either end: -12 .. +9 dB of the input", lo_all);

    /* FORMANT changes the tone: 1.8 kHz through each vowel */
    double fa[5];
    for (int v = 0; v < 5; ++v) {
        mp_engine_init(&E, FS);
        mp_engine_set(&E, MP_P_FORMANT, v / 4.0f);
        mp_engine_set(&E, MP_P_FMT_AMT, 1.0f);
        mp_engine_set(&E, MP_P_CRUSH, 1.0f);
        mp_engine_set(&E, MP_P_CUTOFF, 1.0f);
        mp_engine_set(&E, MP_P_DELAY, 0.0f);
        mp_engine_set(&E, MP_P_PRE_MIX, 0.0f);
        mp_engine_set(&E, MP_P_WIDTH, 0.0f);
        mp_engine_snap(&E);
        static float in[2 * 8820], out[2 * 8820];
        for (int i = 0; i < 8820; ++i) in[2 * i] = in[2 * i + 1] = 0.05f * (float)sin(2 * PI * 1800.0 * i / FS);
        mp_engine_process_f(&E, in, out, 8820);
        fa[v] = db(tone_amp(out + 2 * 4410, 4410, 2, 1800.0, FS));
    }
    printf("     1.8 kHz through a, i, u, e, o: %.1f %.1f %.1f %.1f %.1f dB\n", fa[0], fa[1], fa[2], fa[3], fa[4]);
    check(fa[1] - fa[2] > 10.0, "FORMANT: i (F2 1870 Hz) passes 1.8 kHz > 10 dB more than u", fa[1] - fa[2]);

    mp_engine_init(&E, FS);
    static float in[256], out[256];
    int finite = 1, blocks = (int)(FS / 128) * (g_quick ? 5 : 60);
    float mx = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int p = 0; p < MP_P_COUNT; ++p) {
            const mp_param_info *pi = &MP_PARAMS[p];
            mp_engine_set(&E, p, pi->min + (pi->max - pi->min) * (0.5f + 0.5f * noise()));
        }
        for (int i = 0; i < 256; ++i) in[i] = 0.9f * noise();
        mp_engine_process_f(&E, in, out, 128);
        for (int i = 0; i < 256; ++i) { finite &= isfinite(out[i]); mx = fmaxf(mx, fabsf(out[i])); }
    }
    check(finite && E.nan_resets == 0 && mx <= 4.0f, "every knob random every block, full-scale noise", mx);

    mp_engine_init(&E, FS);
    for (int b = 0; b < 344; ++b) {
        for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
        mp_engine_process_f(&E, in, out, 128);
    }
    double tail = 0;
    for (int b = 0; b < 344 * 8; ++b) {
        memset(in, 0, sizeof(in));
        mp_engine_process_f(&E, in, out, 128);
        if (b > 344 * 7) for (int i = 0; i < 256; ++i) tail = fmax(tail, fabs(out[i]));
    }
    check(db(tail) < -60.0, "defaults: below -60 dB 7 s after the input stops", db(tail));

    mp_engine_init(&E, FS);
    int silent = 1;
    for (int b = 0; b < 100; ++b) {
        memset(in, 0, sizeof(in));
        mp_engine_process_f(&E, in, out, 128);
        for (int i = 0; i < 256; ++i) silent &= out[i] == 0.0f;
    }
    check(silent, "silence in -> silence out (no noise floor, no DC)", silent);

    mp_engine_init(&E, FS);
    mp_engine_set(&E, MP_P_MIX, 0.0f);
    mp_engine_snap(&E);
    for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
    mp_engine_process_f(&E, in, out, 128);
    check(!memcmp(in, out, sizeof(in)), "MIX 0: output = input", 0);

    /* MIX 0 sleeps: after the fade nothing runs, the input passes exactly */
    mp_engine_init(&E, FS);
    for (int b = 0; b < 344; ++b) {   /* fill the delays */
        for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
        mp_engine_process_f(&E, in, out, 128);
    }
    mp_engine_set(&E, MP_P_MIX, 0.0f);
    int blocks_to_sleep = 0;
    while (!E.asleep && blocks_to_sleep < 1000) {
        for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
        mp_engine_process_f(&E, in, out, 128);
        blocks_to_sleep++;
    }
    check(E.asleep && blocks_to_sleep < 100, "MIX 0: asleep once the fade ends (blocks)", blocks_to_sleep);
    for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
    mp_engine_process_f(&E, in, out, 128);
    check(!memcmp(in, out, sizeof(in)), "asleep: output = input exactly", 0);
    {
        static int16_t pcm[256];
        struct timespec t0, t1;
        double awake = 0, slept = 0;
        for (int pass = 0; pass < 2; ++pass) {
            mp_engine_init(&E2, FS);
            if (pass) { mp_engine_set(&E2, MP_P_MIX, 0.0f); mp_engine_snap(&E2); }
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int b = 0; b < 2000; ++b) {
                for (int i = 0; i < 256; ++i) pcm[i] = (int16_t)(noise() * 16000);
                mp_engine_process(&E2, pcm, 128);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 2000;
            if (pass) slept = us; else awake = us;
        }
        printf("     per block: awake %.1f us, MIX 0 %.2f us\n", awake, slept);
        check(slept < awake * 0.05, "MIX 0 costs < 5 % of running", slept / awake);
    }
    /* waking starts empty: silence in -> silence out, no echo from before */
    mp_engine_set(&E, MP_P_MIX, 1.0f);
    double echo = 0;
    for (int b = 0; b < 344 * 2; ++b) {
        memset(in, 0, sizeof(in));
        mp_engine_process_f(&E, in, out, 128);
        for (int i = 0; i < 256; ++i) echo = fmax(echo, fabs(out[i]));
    }
    check(!E.asleep && E.wakes == 1 && echo == 0.0, "MIX up again: wakes with empty delays (no old echo)", echo);

    const int N = 44100;
    static float x[2 * 44100], y1[2 * 44100], y2[2 * 44100];
    for (int i = 0; i < 2 * N; ++i) x[i] = 0.5f * noise();
    mp_engine_init(&E, FS);
    mp_engine_init(&E2, FS);
    for (int i = 0; i < N; i += 128) mp_engine_process_f(&E, x + 2 * i, y1 + 2 * i, N - i < 128 ? N - i : 128);
    for (int i = 0; i < N; i += 37) mp_engine_process_f(&E2, x + 2 * i, y2 + 2 * i, N - i < 37 ? N - i : 37);
    check(!memcmp(y1, y2, sizeof(y1)), "128- and 37-frame blocks give identical output", 0);

    mp_engine_init(&E, 48000.0f);
    check(fabsf(E.pre[0].d_target - 3840.0f) < 0.01f && fabsf(E.post.d_target[0] - 14400.0f) < 0.01f,
          "48 kHz: delays keep their milliseconds", E.pre[0].d_target);

    mp_engine_init(&E, 48000.0f);
    mp_engine_set(&E, MP_P_POST_TIME, 1000.0f);
    mp_engine_set(&E, MP_P_WIDTH, 1.0f);
    mp_engine_snap(&E);
    check(E.post.d_target[1] < MP_PP_SIZE - 4 && fabsf(E.post.d_target[1] - 60000.0f) < 1.0f,
          "longest post delay (1.25 s right, 48 kHz) fits the buffer", E.post.d_target[1]);
}

static void test_cpu(void) {
    puts("== CPU (this machine)");
    static int16_t buf[256];
    mp_engine_init(&E, FS);
    double worst = 0, total = 0;
    int n = g_quick ? 1000 : 6000;
    for (int b = 0; b < n; ++b) {
        for (int i = 0; i < 256; ++i) buf[i] = (int16_t)(noise() * 16000);
        for (int p = 1; p < MP_P_COUNT; ++p) {
            const mp_param_info *pi = &MP_PARAMS[p];
            mp_engine_set(&E, p, pi->min + (pi->max - pi->min) * (0.5f + 0.5f * noise()));
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        mp_engine_process(&E, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
        total += us;
        if (b > 100 && us > worst) worst = us;
    }
    printf("     knobs moving: mean %6.1f us/block  worst %6.1f us  (block = 2902 us)\n", total / n, worst);
    check(total / n < 500.0, "mean block time well inside the 2902 us budget", total / n);
    printf("     instance %.1f KB\n", sizeof(mp_engine) / 1024.0);
}

static void test_plugin(void) {
    puts("== plugin API");
    host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&host);
    check(api && api->api_version == AUDIO_FX_API_VERSION_2, "move_audio_fx_init_v2", 0);
    void *inst[9];
    for (int i = 0; i < 9; ++i) inst[i] = api->create_instance("", NULL);
    check(inst[7] && !inst[8], "pool of 8, the 9th refused (no allocation)", 8);
    for (int i = 1; i < 8; ++i) api->destroy_instance(inst[i]);
    char buf[2048], saved[2048];
    api->set_param(inst[0], "cutoff", "0.25");
    api->set_param(inst[0], "post_time", "450");
    api->get_param(inst[0], "post_time", buf, sizeof(buf));
    check(!strcmp(buf, "450.0000"), "a knob set and read in its own units", 0);
    api->get_param(inst[0], "state", saved, sizeof(saved));
    void *other = api->create_instance("", NULL);
    api->set_param(other, "state", saved);
    api->get_param(other, "state", buf, sizeof(buf));
    check(!strcmp(buf, saved), "state round trip (all 16 knobs)", (double)strlen(saved));
    api->set_param(other, "state", "{\"mix\":0.5}");
    api->get_param(other, "state", buf, sizeof(buf));
    check(!strcmp(buf, saved), "an incomplete state is refused whole", 0);
    api->get_param(other, "ui_hierarchy", buf, sizeof(buf));
    check(strstr(buf, "\"level\"") != NULL, "ui_hierarchy lists all 16 knobs", 0);
    int16_t audio[256] = {0};
    api->process_block(other, audio, 128);
    api->destroy_instance(other);
    api->destroy_instance(inst[0]);
    check(move_audio_fx_init_v2(&(host_api_v1_t){.api_version = 1, .sample_rate = 22050}) == NULL,
          "an unsupported host rate is refused", 0);
}

int main(int argc, char **argv) {
    g_quick = argc > 1 && !strcmp(argv[1], "--quick");
    test_conversions();
    test_delays();
    test_filters();
    test_tube();
    test_crush();
    test_engine();
    test_cpu();
    test_plugin();
    if (g_fail) { printf("%d FAILURE(S)\n", g_fail); return 1; }
    puts("ALL ENGINE TESTS PASSED");
    return 0;
}
