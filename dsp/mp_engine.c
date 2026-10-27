/*
 * MONSTER PHASE - the effect chain (see mp_engine.h).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "mp_engine.h"

#include <math.h>
#include <string.h>

#include "mp_convert.h"

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void control_tick(mp_engine *e) {
    for (int i = 0; i < MP_P_COUNT; ++i) e->s[i] += e->k_smooth * (e->p[i] - e->s[i]);
    const float *s = e->s, fs = e->fs, w = clampf(s[MP_P_WIDTH], 0.0f, 1.0f);

    float pre = s[MP_P_PRE_TIME];
    mp_delay_set(&e->pre[0], pre, s[MP_P_PRE_FDBK], s[MP_P_PRE_MIX], fs);
    mp_delay_set(&e->pre[1], pre * (1.0f - MP_PRE_SPREAD * w), s[MP_P_PRE_FDBK], s[MP_P_PRE_MIX], fs);

    float v = mp_vowel(s[MP_P_FORMANT]);
    mp_vowel_set(&e->vowel[0], clampf(v - MP_VOWEL_SPREAD * w, 0.0f, 4.0f), fs);
    mp_vowel_set(&e->vowel[1], clampf(v + MP_VOWEL_SPREAD * w, 0.0f, 4.0f), fs);
    e->fmt_amt = s[MP_P_FMT_AMT];

    mp_crush_set(&e->crush, mp_crush_hz(s[MP_P_CRUSH], fs), MP_CRUSH_SPREAD * w, fs);

    float hz = mp_cutoff_hz(s[MP_P_CUTOFF]), q = mp_reso_q(s[MP_P_RESO]);
    float bass = mp_bass_db(s[MP_P_BASS]);
    for (int c = 0; c < 2; ++c) {
        mp_svf_set(&e->lpf[c], hz, q, fs);
        mp_svf_flush(&e->lpf[c]);
        mp_biquad_lowshelf(&e->bass[c], MP_BASS_HZ, bass, fs);
    }
    mp_tube_set(&e->tube, mp_drive_db(s[MP_P_DRIVE]));

    float t = s[MP_P_POST_TIME];
    mp_pingpong_set(&e->post, t, t * (1.0f + MP_POST_SPREAD * w), s[MP_P_POST_FDBK], MP_POST_CROSS * w, fs);
    e->level_target = mp_db_to_gain(s[MP_P_LEVEL]);
}

static void settle(mp_engine *e) {
    for (int c = 0; c < 2; ++c) e->pre[c].d = e->pre[c].d_target;
    for (int c = 0; c < 2; ++c) e->post.d[c] = e->post.d_target[c];
    e->level = e->level_target;
}

static void init_audio(mp_engine *e) {
    for (int c = 0; c < 2; ++c) {
        mp_delay_init(&e->pre[c], e->fs);
        memset(&e->vowel[c], 0, sizeof(e->vowel[c]));
        memset(&e->lpf[c], 0, sizeof(e->lpf[c]));
        memset(&e->bass[c], 0, sizeof(e->bass[c]));
    }
    mp_crush_init(&e->crush);
    mp_tube_init(&e->tube, e->fs);
    mp_pingpong_init(&e->post, e->fs);
}

void mp_engine_init(mp_engine *e, float fs) {
    memset(e, 0, sizeof(*e));
    e->fs = fs;
    for (int i = 0; i < MP_P_COUNT; ++i) e->p[i] = e->s[i] = MP_PARAMS[i].def;
    e->mix = e->p[MP_P_MIX];
    e->k_smooth = 1.0f - expf(-(float)MP_CR / (0.015f * fs));   /* 15 ms */
    e->k_mix = 1.0f - expf(-1.0f / (0.015f * fs));
    init_audio(e);
    control_tick(e);
    settle(e);
}

void mp_engine_set(mp_engine *e, int idx, float v) {
    if (idx < 0 || idx >= MP_P_COUNT || !isfinite(v)) return;
    const mp_param_info *info = &MP_PARAMS[idx];
    if (info->integer) v = floorf(v + 0.5f);
    e->p[idx] = clampf(v, info->min, info->max);
}

void mp_engine_snap(mp_engine *e) {
    for (int i = 0; i < MP_P_COUNT; ++i) e->s[i] = e->p[i];
    e->mix = e->p[MP_P_MIX];
    control_tick(e);
    settle(e);
}

void mp_engine_reset(mp_engine *e) {
    init_audio(e);
    control_tick(e);
    settle(e);
}

void mp_engine_process_f(mp_engine *e, const float *in, float *out, int frames) {
    for (int i = 0; i < frames; ++i) {
        if (e->sample++ % MP_CR == 0) control_tick(e);
        const float dry[2] = {in[2 * i], in[2 * i + 1]};
        float a[2], b[2];
        for (int c = 0; c < 2; ++c) {
            float x = mp_delay_tick(&e->pre[c], dry[c]);
            a[c] = x + e->fmt_amt * (MP_FORMANT_GAIN * mp_vowel_tick(&e->vowel[c], x) - x);
        }
        mp_crush_tick(&e->crush, a, b);
        for (int c = 0; c < 2; ++c) {
            float lp;
            mp_svf_tick(&e->lpf[c], b[c], &lp, NULL, NULL);
            a[c] = mp_biquad_tick(&e->bass[c], lp);
        }
        mp_tube_tick(&e->tube, a, b);
        float wet[2];
        mp_pingpong_tick(&e->post, b, wet);
        e->level += e->k_mix * (e->level_target - e->level);
        float y[2];
        for (int c = 0; c < 2; ++c) y[c] = e->level * (b[c] + e->s[MP_P_DELAY] * wet[c]);
        if (!isfinite(y[0]) || !isfinite(y[1])) {
            e->nan_resets++;
            mp_engine_reset(e);
            y[0] = y[1] = 0.0f;
        }
        e->mix += e->k_mix * (e->p[MP_P_MIX] - e->mix);
        for (int c = 0; c < 2; ++c) {
            /* a resonance peak or a runaway loop must not reach the speakers */
            float v = clampf(y[c], -4.0f, 4.0f);
            out[2 * i + c] = dry[c] + e->mix * (v - dry[c]);
        }
    }
}

void mp_engine_process(mp_engine *e, int16_t *audio, int frames) {
    float in[256], out[256];
    while (frames > 0) {
        int n = frames > 128 ? 128 : frames;
        for (int i = 0; i < 2 * n; ++i) in[i] = audio[i] * (1.0f / 32768.0f);
        mp_engine_process_f(e, in, out, n);
        for (int i = 0; i < 2 * n; ++i) {
            float v = out[i] * 32768.0f;
            audio[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : lrintf(v));
        }
        audio += 2 * n;
        frames -= n;
    }
}
