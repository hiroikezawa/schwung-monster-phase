/*
 * MONSTER PHASE - engine, block and plugin tests (desktop).
 *
 *   test-engine [--quick]
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * What is tested is the IMPLEMENTATION against its own documented models
 * (docs/ANALYSIS.md): routing, conversions, delay phase and feedback, filter
 * stability, the clipper's harmonics and aliasing, compressor timing, knob
 * abuse, block-size independence, CPU. None of it is a comparison with the
 * Roland hardware, which was not measured.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "mp_convert.h"
#include "mp_engine.h"
#include "mp_patch_data.h"

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

/* ------------------------------------------------------------------ */

static void test_conversions(void) {
    puts("== conversions (MP_CONVERT_VERSION 1)");
    check(MP_CONVERT_VERSION == 1, "conversion table version", MP_CONVERT_VERSION);
    check(fabsf(mp_raw01(61) - 0.61f) < 1e-6f && mp_raw01(200) == 1.0f, "raw 0..100 -> 0..1, clamped", mp_raw01(61));
    check(fabsf(mp_sd_time_pos(3) - 0.97f) < 1e-6f && mp_sd_time_pos(0) == 1.0f,
          "TIME raw 3 / 0 -> panel 0.97 / 1.00 (reversed reading)", mp_sd_time_pos(3));
    check(fabsf(mp_sd_time_ms(mp_sd_time_pos(3)) - 77.6f) < 1e-3f && mp_sd_time_ms(1.0f) == 80.0f,
          "SHORT DELAY 1 / 2 -> 77.6 / 80 ms (linear, provisional)", mp_sd_time_ms(0.97f));
    check(mp_formant_vowel(0) == 0.0f && mp_formant_vowel(1) == 1.0f && mp_formant_vowel(9) == 4.0f,
          "FORMANT raw -> vowel index a=0 i=1 .. o=4", mp_formant_vowel(1));
    float hz[3];
    mp_formant_freqs(0.0f, hz);
    check(fabsf(hz[0] - 650.0f) < 0.01f && fabsf(hz[1] - 1080.0f) < 0.01f, "vowel a: F1 650 F2 1080 Hz", hz[0]);
    mp_formant_freqs(0.5f, hz);
    check(fabsf(hz[0] - sqrtf(650.0f * 290.0f)) < 0.5f, "between a and i: F1 geometric mean", hz[0]);
    check(fabsf(mp_tube_out_gain(0.75f) - 1.0f) < 1e-6f, "TUBE OUT LEVEL 75 = unity (assumed)", mp_tube_out_gain(0.75f));
    check(fabsf(mp_tube_drive_db(0.27f) - 9.72f) < 1e-3f, "TUBE GAIN 27 -> +9.72 dB drive", mp_tube_drive_db(0.27f));
    check(mp_eq_gain_db(0.5f) == 0.0f && mp_eq_gain_db(1.0f) == 15.0f, "EQ 50 = flat, 100 = +15 dB", mp_eq_gain_db(1.0f));
    check(mp_comp_ratio(0.0f) == 1.0f && mp_comp_threshold_db(0.0f) == 0.0f,
          "COMPRESSOR raw 0/0 -> 0 dBFS, 1:1 (transparent)", mp_comp_ratio(0));
    check(fabsf(mp_bit_rate_hz(1.0f, FS) - FS) < 1e-2f && fabsf(mp_bit_rate_hz(0.0f, FS) - FS / 256) < 1e-2f,
          "SAMPLE RATE 0..1 -> fs/256 .. fs", mp_bit_rate_hz(0, FS));
    check(mp_bit_bits(0.5f) == 9.0f, "BIT DOWN 50 -> 9 bits", mp_bit_bits(0.5f));
}

static void test_patch_data(void) {
    puts("== patch data compiled in");
    static const uint8_t want[6][5] = {{16, 3, 61, 48, 0}, {16, 0, 61, 48, 0}, {28, 0, 1, 0, 0},
                                       {17, 27, 75, 0, 0}, {20, 50, 50, 50, 0}, {18, 0, 0, 10, 10}};
    check(!memcmp(want, MP_SUB_RAW, sizeof(want)), "six sub-module records: ids and raw values", 6);
    check(!strcmp(MP_PATCH_SHA256, "f27c95ff684e9e155f7c71ea75f53fe5d62701623b0ee663f847b30584d23843"),
          "patch SHA-256", 0);
    static const uint8_t main_raw[11] = {1, 0, 0, 0, 0, 50, 0, 100, 100, 50, 100};
    check(!memcmp(main_raw, MP_MAIN_RAW, 11), "main record bytes", 0);
    check(MP_EDGE_COUNT == 17, "routing: 17 traced cables", MP_EDGE_COUNT);
}

static int order_pos(const mp_engine *e, int node) {
    for (int i = 0; i < MP_NODES; ++i) if (e->order[i] == node) return i;
    return -1;
}

static void test_routing(void) {
    puts("== routing graph");
    mp_engine_init(&E, FS);
    check(E.edges_used == 17 && E.edges_rejected == 0, "all 17 cables accepted", E.edges_used);
    check(!E.cyclic, "no cycle in the traced patch", E.cyclic);
    int types[7] = {MP_T_MAIN, 16, 16, 28, 17, 20, 18}, ok = 1;
    for (int k = 0; k < 7; ++k) ok &= E.node[k].type == types[k];
    check(ok, "node types from the file: MAIN SD SD FORMANT TUBE EQ COMP", 7);
    int p[7];
    for (int k = 0; k < 7; ++k) p[k] = order_pos(&E, k);
    check(p[1] < p[4] && p[2] < p[3] && p[3] < p[4] && p[4] < p[0] && p[0] < p[6] && p[6] < p[5],
          "order: SD1,SD2 < FORMANT < TUBE < BITRAZER < COMP < EQ", 0);
    check(fabsf(E.time_offset[1] - 0.03f) < 1e-6f && E.time_offset[2] == 0.0f,
          "TIME knob keeps SD1 3 % below SD2", E.time_offset[1]);
    check(fabsf(E.node[1].u.delay.d_target - 0.0776f * FS) < 0.01f && fabsf(E.node[2].u.delay.d_target - 0.08f * FS) < 0.01f,
          "SD1 / SD2 delay 77.6 / 80 ms in samples", E.node[1].u.delay.d_target);
    check(E.node[0].nin[4] == 1 && E.node[0].in_src[4][0] == 2, "GRF1 -> BITRAZER MOD3 (SAMPLE RATE)", 0);
    check(E.node[3].nin[1] == 1 && E.node[3].in_src[1][0] == 4, "GRF3 -> FORMANT 1 IN", 0);
    check(E.node[5].nin[3] == 1 && E.node[5].in_src[3][0] == 5, "GRF4 -> EQ LOW IN", 0);

    /* the traced patch keeps the channels apart: L in never reaches R out */
    static float in[2 * 44100], out[2 * 44100];
    for (int side = 0; side < 2; ++side) {
        mp_engine_init(&E, FS);
        memset(in, 0, sizeof(in));
        for (int i = 0; i < 4410; ++i) in[2 * i + side] = 0.5f * noise();
        mp_engine_process_f(&E, in, out, 44100);
        double same = 0, other = 0;
        for (int i = 0; i < 44100; ++i) { same += fabs(out[2 * i + side]); other += fabs(out[2 * i + 1 - side]); }
        check(same > 10 && other == 0.0, side ? "R in -> R out only (exact zero on L)" : "L in -> L out only (exact zero on R)", same);
    }

    /* rejected cables and summing, on routings of our own */
    mp_edge bad[3] = {{13, 0}, {0, 33}, {0, 0}};   /* SD2 OUT2 (no such), COMP IN4 (no such), ok */
    mp_engine_init_with(&E, FS, MP_MAIN_RAW, MP_SUB_RAW, bad, 3);
    check(E.edges_rejected == 2 && E.edges_used == 1, "cables to/from ports a module lacks are rejected", E.edges_rejected);
    mp_edge sum[2] = {{0, 0}, {1, 0}};             /* INPUT1 + INPUT2 -> OUTPUT1 */
    mp_engine_init_with(&E, FS, MP_MAIN_RAW, MP_SUB_RAW, sum, 2);
    float i2[2] = {0.25f, 0.5f}, o2[2];
    mp_engine_process_f(&E, i2, o2, 1);
    check(fabsf(o2[0] - 0.75f) < 1e-6f && o2[1] == 0.0f, "two cables into one input are summed", o2[0]);
    /* fan-out: INPUT1 -> OUTPUT1 and OUTPUT2 */
    mp_edge fan[2] = {{0, 0}, {0, 1}};
    mp_engine_init_with(&E, FS, MP_MAIN_RAW, MP_SUB_RAW, fan, 2);
    mp_engine_process_f(&E, i2, o2, 1);
    check(o2[0] == 0.25f && o2[1] == 0.25f, "one output feeds two inputs", o2[1]);

    /* a cycle: INPUT1 -> SD1 -> EQ -> SD1 (feedback), SD1 -> OUTPUT1 */
    mp_edge cyc[4] = {{0, 10}, {10, 26}, {18, 10}, {10, 0}};
    mp_engine_init_with(&E, FS, MP_MAIN_RAW, MP_SUB_RAW, cyc, 4);
    check(E.cyclic == 1 && E.edges_used == 4, "a feedback cable is accepted, flagged cyclic", E.cyclic);
    memset(in, 0, sizeof(in));
    for (int i = 0; i < 441; ++i) in[2 * i] = 0.1f * noise();
    mp_engine_process_f(&E, in, out, 44100);
    int finite = 1;
    for (int i = 0; i < 2 * 44100; ++i) finite &= isfinite(out[i]) && fabsf(out[i]) <= 4.0f;
    check(finite, "the cyclic graph runs, output finite and bounded", E.nan_resets);
}

static void test_delay(void) {
    puts("== SHORT DELAY");
    static mp_delay d;
    static float y[44100];
    mp_delay_init(&d, FS);
    mp_delay_set(&d, 10.0f, 0.0f, 1.0f, FS);
    d.d = d.d_target;
    for (int i = 0; i < 2000; ++i) y[i] = mp_delay_tick(&d, i == 0 ? 1.0f : 0.0f);
    double rest = 0;
    for (int i = 0; i < 2000; ++i) if (i != 441) rest += fabs(y[i]);
    check(fabsf(y[441] - 1.0f) < 1e-3f && rest < 1e-3, "10 ms = 441 samples, clean impulse", rest);

    mp_delay_init(&d, FS);
    mp_delay_set(&d, 10.0f, 0.5f, 1.0f, FS);
    d.d = d.d_target;
    for (int i = 0; i < 44100; ++i) y[i] = mp_delay_tick(&d, i == 0 ? 0.01f : 0.0f);
    double r1 = y[882] / y[441], r2 = y[1323] / y[882];
    check(fabs(r1 - 0.5) < 1e-3 && fabs(r2 - 0.5) < 1e-3, "FEEDBACK 0.5: each echo half the last", r1);

    mp_delay_init(&d, FS);
    mp_delay_set(&d, 10.5f / 44.1f, 0.0f, 1.0f, FS);
    d.d = d.d_target;
    double s = 0, sn = 0;
    for (int i = 0; i < 64; ++i) { y[i] = mp_delay_tick(&d, i == 0 ? 1.0f : 0.0f); s += y[i]; sn += i * y[i]; }
    check(fabs(sn / s - 10.5) < 0.05 && fabs(s - 1.0) < 1e-3, "fractional 10.5 samples: group delay, unity DC", sn / s);

    /* phase: 50 % dry/wet of a 44-sample delay is a comb, notch at fs/88 */
    double notch = FS / 88.0, peak = FS / 44.0, amp[2];
    const double f[2] = {notch, peak};
    for (int k = 0; k < 2; ++k) {
        mp_delay_init(&d, FS);
        mp_delay_set(&d, 44.0f / 44.1f, 0.0f, 0.5f, FS);
        d.d = d.d_target;
        for (int i = 0; i < 8820; ++i) y[i] = mp_delay_tick(&d, (float)sin(2 * PI * f[k] * i / FS));
        amp[k] = tone_amp(y + 4410, 4400, 1, f[k], FS);
    }
    check(db(amp[0]) < -40.0, "DRY/WET 50 %: comb notch at fs/(2d) (dB)", db(amp[0]));
    check(fabs(db(amp[1])) < 0.1, "... and full level at fs/d (dB)", db(amp[1]));

    /* FEEDBACK 100 % with a full-scale burst: saturates, never runs away */
    mp_delay_init(&d, FS);
    mp_delay_set(&d, 5.0f, 1.0f, 1.0f, FS);
    d.d = d.d_target;
    float mx = 0;
    int finite = 1;
    for (int i = 0; i < 44100 * 4; ++i) {
        float v = mp_delay_tick(&d, i < 4410 ? noise() : 0.0f);
        finite &= isfinite(v);
        if (fabsf(v) > mx) mx = fabsf(v);
    }
    check(finite && mx <= 3.0f, "FEEDBACK 100 %: bounded (|in| 1 + loop saturation 2)", mx);

    /* the patch's own setting decays: 61 % feedback, 80 ms */
    mp_delay_init(&d, FS);
    mp_delay_set(&d, 80.0f, 0.61f, 0.48f, FS);
    d.d = d.d_target;
    double tail = 0;
    for (int i = 0; i < 44100 * 3; ++i) {
        float v = mp_delay_tick(&d, i < 441 ? noise() : 0.0f);
        if (i > 44100 * 2) tail = fmax(tail, fabs(v));
    }
    check(db(tail) < -60.0, "patch feedback 61 %: tail below -60 dB after 2 s", db(tail));
}

static void test_filters(void) {
    puts("== filter stability");
    static mp_bitrazer b;
    mp_bitrazer_init(&b, FS);
    float mx = 0;
    int finite = 1;
    for (int i = 0; i < 44100 * (g_quick ? 2 : 10); ++i) {
        if (i % MP_CR == 0)
            mp_bitrazer_set(&b, 1.0f, 0.0f, 0.5f + 0.5f * noise(), 1.0f, (i / 4410) & 1, 0, FS);
        float in[2] = {0.5f * noise(), 0.5f * noise()}, out[2];
        mp_bitrazer_tick(&b, in, out);
        finite &= isfinite(out[0]) && isfinite(out[1]);
        mx = fmaxf(mx, fmaxf(fabsf(out[0]), fabsf(out[1])));
    }
    check(finite && mx < 60.0f, "BITRAZER filter: random cutoff, max resonance, LPF/HPF flips", mx);

    static mp_formant fo;
    memset(&fo, 0, sizeof(fo));
    mx = 0;
    finite = 1;
    for (int i = 0; i < 44100 * (g_quick ? 2 : 10); ++i) {
        if (i % MP_CR == 0) mp_formant_set(&fo, 2.0f + 2.0f * noise(), 2.0f + 2.0f * noise(), 0.5f + 0.5f * noise(), FS);
        float v = mp_formant_tick(&fo, 0.5f * noise());
        finite &= isfinite(v);
        mx = fmaxf(mx, fabsf(v));
    }
    check(finite && mx < 10.0f, "FORMANT: vowels jumping every 16 samples", mx);

    /* formant peaks where the table says: vowel a, F1 = 650 Hz */
    memset(&fo, 0, sizeof(fo));
    mp_formant_set(&fo, 0.0f, 0.0f, 0.0f, FS);
    double a[3];
    const double fq[3] = {650.0, 1080.0, 4000.0};
    static float y[8820];
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 8820; ++i) y[i] = mp_formant_tick(&fo, (float)sin(2 * PI * fq[k] * i / FS));
        a[k] = tone_amp(y + 4410, 4410, 1, fq[k], FS);
    }
    check(a[0] > 0.8 && a[1] > 0.3 && a[2] < 0.1, "vowel a: resonant at 650 / 1080 Hz, low at 4 kHz", a[0]);

    static mp_eq eq;
    mp_eq_init(&eq);
    mp_eq_set(&eq, 0.0f, 1.0f, 1.0f, FS);   /* HI -15, MID +15, LOW +15 */
    mx = 0;
    finite = 1;
    for (int i = 0; i < 44100; ++i) {
        float v = mp_eq_tick(&eq, 0.5f * noise());
        finite &= isfinite(v);
    }
    mp_eq_init(&eq);
    mp_eq_set(&eq, 0.5f, 0.5f, 1.0f, FS);
    for (int i = 0; i < 8820; ++i) y[i] = mp_eq_tick(&eq, (float)sin(2 * PI * 30.0 * i / FS));
    double lo = db(tone_amp(y + 4410, 4410, 1, 30.0, FS));
    mp_eq_init(&eq);
    mp_eq_set(&eq, 0.5f, 0.5f, 0.5f, FS);
    for (int i = 0; i < 8820; ++i) y[i] = mp_eq_tick(&eq, (float)sin(2 * PI * 1000.0 * i / FS));
    double flat = db(tone_amp(y + 4410, 4410, 1, 1000.0, FS));
    check(finite && fabs(lo - 15.0) < 1.0 && fabs(flat) < 0.01, "EQ: LOW 100 = +15 dB at 30 Hz, 50/50/50 flat", lo);
}

static void test_tube(void) {
    puts("== TUBE CLIP");
    static mp_tube t;
    static float y[2 * 44100];
    /* the patch's GAIN 27 / OUT 75 on a 1102.5 Hz sine (40-sample period) */
    mp_tube_init(&t, FS);
    mp_tube_set(&t, 0.27f, 0.75f);
    for (int i = 0; i < 44100; ++i) {
        float s = 0.5f * (float)sin(2 * PI * i / 40.0), in[2] = {s, s};
        mp_tube_tick(&t, in, y + 2 * i);
    }
    double h1 = tone_amp(y + 2 * 40000, 4000, 2, FS / 40, FS);
    double h2 = tone_amp(y + 2 * 40000, 4000, 2, 2 * FS / 40, FS);
    double h3 = tone_amp(y + 2 * 40000, 4000, 2, 3 * FS / 40, FS);
    printf("     H2 %.1f dBc  H3 %.1f dBc\n", db(h2 / h1), db(h3 / h1));
    check(db(h2 / h1) > -40.0, "asymmetric: even (2nd) harmonic present (dBc)", db(h2 / h1));
    check(db(h3 / h1) > -50.0, "odd (3rd) harmonic present (dBc)", db(h3 / h1));
    double dc = 0;
    for (int i = 40000; i < 44000; ++i) dc += y[2 * i];
    check(fabs(dc / 4000) < 1e-4, "DC from the bias removed (5 Hz blocker, 1 s in)", dc / 4000);

    /* aliasing: 7 kHz at high drive. Harmonics 4..7 fold to 16.1, 9.1, 2.1,
     * 4.9 kHz; 4x oversampling must push those down. */
    const double alias[4] = {16100, 9100, 2100, 4900};
    double al[2];
    for (int k = 0; k < 2; ++k) {
        mp_tube_init(&t, FS);
        t.os = k ? MP_OS : 1;
        mp_tube_set(&t, 0.6f, 0.75f);
        for (int i = 0; i < 8000; ++i) {
            float s = 0.5f * (float)sin(2 * PI * 7000.0 * i / FS), in[2] = {s, s};
            mp_tube_tick(&t, in, y + 2 * i);
        }
        double fund = tone_amp(y + 2 * 3590, 4410, 2, 7000, FS), e = 0;
        for (int j = 0; j < 4; ++j) { double a = tone_amp(y + 2 * 3590, 4410, 2, alias[j], FS); e += a * a; }
        al[k] = db(sqrt(e) / fund);
    }
    printf("     aliases (dBc): no oversampling %.1f, 4x %.1f\n", al[0], al[1]);
    check(al[1] < al[0] - 15.0, "4x oversampling lowers the fold-over by > 15 dB", al[0] - al[1]);
}

static void test_comp(void) {
    puts("== COMPRESSOR");
    static mp_comp c;
    const float n_ratio = 3.0f / 19.0f, n_att = 2.0f / 3.0f, n_rel = (float)(log(10.0) / log(200.0));
    memset(&c, 0, sizeof(c));
    mp_comp_set(&c, 0.5f, n_ratio, n_att, n_rel, FS);   /* -20 dB, 4:1, 10 ms, 100 ms */
    float in[2], out[2];
    for (int i = 0; i < 4410; ++i) { in[0] = in[1] = 0.01f; mp_comp_tick(&c, in, out); }
    /* step to a constant-magnitude -6.02 dB square */
    double gr10 = 0, grss = 0;
    for (int i = 0; i < 44100; ++i) {
        in[0] = in[1] = (i & 1) ? 0.5f : -0.5f;
        mp_comp_tick(&c, in, out);
        if (i == 441) gr10 = -db(fabs(out[0]) / 0.5);
        grss = -db(fabs(out[0]) / 0.5);
    }
    double want = (20.0 + db(0.5)) * 0.75;
    check(fabs(grss - want) < 0.05, "steady state: 4:1 over -20 dB -> 10.49 dB reduction", grss);
    check(fabs(gr10 - want * (1 - exp(-1.0))) < 0.3, "attack 10 ms: 63 % of it after 10 ms", gr10);
    double gr100 = 0;
    for (int i = 0; i < 4411; ++i) {
        in[0] = in[1] = 0.01f;
        mp_comp_tick(&c, in, out);
        if (i == 4410) gr100 = -db(fabs(out[0]) / 0.01);
    }
    check(fabs(gr100 - want * exp(-1.0)) < 0.3, "release 100 ms: 37 % left after 100 ms", gr100);

    memset(&c, 0, sizeof(c));
    mp_comp_set(&c, 0.0f, 0.0f, 0.1f, 0.1f, FS);   /* the patch: raw 0, 0, 10, 10 */
    int same = 1;
    for (int i = 0; i < 4410; ++i) {
        in[0] = noise(); in[1] = noise();
        mp_comp_tick(&c, in, out);
        same &= out[0] == in[0] && out[1] == in[1];
    }
    check(same, "the patch's settings (1:1 under this map): bit-transparent", same);
}

static void test_bitrazer(void) {
    puts("== BITRAZER");
    static mp_bitrazer b;
    mp_bitrazer_init(&b, FS);
    mp_bitrazer_set(&b, 5.0f / 8.0f, 0.0f, 1.0f, 0.0f, 0, 0, FS);   /* fs / 8 */
    int changes = 0;
    float last = -9;
    for (int i = 0; i < 8000; ++i) {
        float in[2] = {i / 8000.0f, 0}, out[2];
        mp_bitrazer_tick(&b, in, out);
        if (b.held[0] != last) { changes++; last = b.held[0]; }
    }
    check(abs(changes - 1000) <= 1, "SAMPLE RATE 5/8 -> held every 8 samples", changes);

    mp_bitrazer_init(&b, FS);
    mp_bitrazer_set(&b, 1.0f, 1.0f, 1.0f, 0.0f, 0, 0, FS);          /* 2 bits */
    float seen[64];
    int nseen = 0;
    for (int i = 0; i < 8000; ++i) {
        float in[2] = {-1.0f + 2.0f * i / 8000.0f, 0}, out[2];
        mp_bitrazer_tick(&b, in, out);
        int found = 0;
        for (int j = 0; j < nseen; ++j) found |= seen[j] == b.held[0];
        if (!found && nseen < 64) seen[nseen++] = b.held[0];
    }
    check(nseen <= 5, "BIT DOWN 100 -> 2 bits: at most 5 levels over -1..1", nseen);

    mp_bitrazer_init(&b, FS);
    mp_bitrazer_set(&b, 0.2f, 1.0f, 0.3f, 0.5f, 0, 1, FS);          /* crushed, but BYPASS */
    float out[2], in[2];
    for (int i = 0; i < 441; ++i) { in[0] = in[1] = noise(); mp_bitrazer_tick(&b, in, out); }
    check(out[0] == in[0] && out[1] == in[1], "BYPASS: input through, after the 5 ms fade", out[0]);

    static float y[8820];
    mp_bitrazer_init(&b, FS);
    mp_bitrazer_set(&b, 1.0f, 0.0f, (float)(log(1000.0 / 20.0) / log(1000.0)), 0.0f, 1, 0, FS);   /* HPF 1 kHz */
    for (int i = 0; i < 8820; ++i) {
        float s = (float)sin(2 * PI * 50.0 * i / FS);
        in[0] = in[1] = s;
        mp_bitrazer_tick(&b, in, out);
        y[i] = out[0];
    }
    check(db(tone_amp(y + 4410, 4410, 1, 50.0, FS)) < -40.0, "LPF/HPF switch: HPF 1 kHz removes 50 Hz (dB)",
          db(tone_amp(y + 4410, 4410, 1, 50.0, FS)));
}

static void test_engine_abuse(void) {
    puts("== knobs, stability, determinism");
    mp_engine_init(&E, FS);
    static float in[256], out[256];
    int finite = 1, blocks = (int)(FS / 128) * (g_quick ? 5 : 60);
    float mx = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int p = 0; p < MP_P_COUNT; ++p) mp_engine_set(&E, p, 0.5f + 0.5f * noise());
        for (int i = 0; i < 256; ++i) in[i] = 0.9f * noise();
        mp_engine_process_f(&E, in, out, 128);
        for (int i = 0; i < 256; ++i) { finite &= isfinite(out[i]); mx = fmaxf(mx, fabsf(out[i])); }
    }
    check(finite && E.nan_resets == 0 && mx <= 4.0f, "every knob random every block, full-scale noise", mx);

    /* stop the input: the patch dies away (no runaway) */
    double tail = 0;
    for (int b = 0; b < (int)(FS / 128) * 6; ++b) {
        memset(in, 0, sizeof(in));
        mp_engine_process_f(&E, in, out, 128);
        if (b > (int)(FS / 128) * 5) for (int i = 0; i < 256; ++i) tail = fmax(tail, fabs(out[i]));
    }
    check(db(tail) < -60.0, "after the input stops: below -60 dB within 5 s", db(tail));

    /* knob smoothing: 15 ms to 63 % */
    mp_engine_init(&E, FS);
    mp_engine_set(&E, MP_P_GRF1, 0.0f);
    mp_engine_snap(&E);
    mp_engine_set(&E, MP_P_GRF1, 1.0f);
    for (int b = 0; b < 5; ++b) { memset(in, 0, sizeof(in)); mp_engine_process_f(&E, in, out, 128); }
    check(fabs(E.s[MP_P_GRF1] - (1 - exp(-640.0 / (0.015 * FS)))) < 0.03, "a knob glides: 15 ms time constant", E.s[MP_P_GRF1]);

    /* silence in -> exact silence out */
    mp_engine_init(&E, FS);
    int silent = 1;
    for (int b = 0; b < 100; ++b) {
        memset(in, 0, sizeof(in));
        mp_engine_process_f(&E, in, out, 128);
        for (int i = 0; i < 256; ++i) silent &= out[i] == 0.0f;
    }
    check(silent, "silence in -> silence out (no noise floor, no DC)", silent);

    /* block size does not change the output */
    const int N = 44100;
    static float x[2 * 44100], y1[2 * 44100], y2[2 * 44100];
    for (int i = 0; i < 2 * N; ++i) x[i] = 0.5f * noise();
    mp_engine_init(&E, FS);
    mp_engine_init(&E2, FS);
    for (int i = 0; i < N; i += 128) {
        if (i == 128 * 100) mp_engine_set(&E, MP_P_GRF2, 0.2f);
        mp_engine_process_f(&E, x + 2 * i, y1 + 2 * i, N - i < 128 ? N - i : 128);
    }
    for (int i = 0; i < N; i += 37) {
        if (i <= 128 * 100 && i + 37 > 128 * 100) {   /* same sample position as above */
            int cut = 128 * 100 - i;
            mp_engine_process_f(&E2, x + 2 * i, y2 + 2 * i, cut);
            mp_engine_set(&E2, MP_P_GRF2, 0.2f);
            mp_engine_process_f(&E2, x + 2 * (i + cut), y2 + 2 * (i + cut), 37 - cut);
            continue;
        }
        mp_engine_process_f(&E2, x + 2 * i, y2 + 2 * i, N - i < 37 ? N - i : 37);
    }
    check(!memcmp(y1, y2, sizeof(y1)), "128- and 37-frame blocks give identical output", 0);

    /* 48 kHz: the delays keep their milliseconds */
    mp_engine_init(&E, 48000.0f);
    check(fabsf(E.node[2].u.delay.d_target - 3840.0f) < 0.01f, "48 kHz: 80 ms = 3840 samples", E.node[2].u.delay.d_target);

    /* MIX 0 = dry exactly */
    mp_engine_init(&E, FS);
    mp_engine_set(&E, MP_P_MIX, 0.0f);
    mp_engine_snap(&E);
    for (int i = 0; i < 256; ++i) in[i] = 0.5f * noise();
    mp_engine_process_f(&E, in, out, 128);
    check(!memcmp(in, out, sizeof(in)), "MIX 0: output = input", 0);
}

static void test_cpu(void) {
    puts("== CPU (this machine; tests/bench_device.c measures the Move)");
    static int16_t buf[256];
    const char *names[2] = {"defaults", "all knobs moving"};
    for (int c = 0; c < 2; ++c) {
        mp_engine_init(&E, FS);
        double worst = 0, total = 0;
        int n = g_quick ? 1000 : 6000;
        for (int b = 0; b < n; ++b) {
            for (int i = 0; i < 256; ++i) buf[i] = (int16_t)(noise() * 16000);
            if (c) for (int p = 1; p < MP_P_COUNT; ++p) mp_engine_set(&E, p, 0.5f + 0.5f * noise());
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            mp_engine_process(&E, buf, 128);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
            total += us;
            if (b > 100 && us > worst) worst = us;
        }
        printf("     %-18s mean %6.1f us/block  worst %6.1f us  (block = 2902 us)\n", names[c], total / n, worst);
        check(total / n < 500.0, "mean block time well inside the 2902 us budget", total / n);
    }
    printf("     instance %.1f KB\n", sizeof(mp_engine) / 1024.0);
}

/* ---- the plugin entry points, as the chain host calls them ---- */
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
    char buf[2048];
    api->set_param(inst[0], "g5_type", "hpf");
    api->set_param(inst[0], "g1_srate", "0.25");
    api->get_param(inst[0], "g5_type", buf, sizeof(buf));
    check(!strcmp(buf, "HPF"), "enum by name, served as the name", 0);
    api->get_param(inst[0], "state", buf, sizeof(buf));
    char saved[2048];
    strcpy(saved, buf);
    void *other = api->create_instance("", NULL);
    api->set_param(other, "state", saved);
    api->get_param(other, "state", buf, sizeof(buf));
    check(!strcmp(buf, saved), "state round trip", 0);
    api->set_param(other, "state", "{\"mix\":0.5}");
    api->get_param(other, "state", buf, sizeof(buf));
    check(!strcmp(buf, saved), "an incomplete state is refused whole", 0);
    api->get_param(other, "patch_sha256", buf, sizeof(buf));
    check(!strcmp(buf, MP_PATCH_SHA256), "patch_sha256 served", 0);
    api->get_param(other, "routing", buf, sizeof(buf));
    check(strstr(buf, "approximate") != NULL, "routing id says approximate", 0);
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
    test_patch_data();
    test_routing();
    test_delay();
    test_filters();
    test_tube();
    test_comp();
    test_bitrazer();
    test_engine_abuse();
    test_cpu();
    test_plugin();
    if (g_fail) { printf("%d FAILURE(S)\n", g_fail); return 1; }
    puts("ALL ENGINE TESTS PASSED");
    return 0;
}
