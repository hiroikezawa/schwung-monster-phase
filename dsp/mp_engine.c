/*
 * MONSTER PHASE - the patch graph (see mp_engine.h).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include "mp_engine.h"

#include <math.h>
#include <string.h>

#include "mp_convert.h"
#include "mp_patch_data.h"

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* Inputs per sub-module type (public SysEx "Sub Module Ins"), and which are audio. */
static int type_inputs(int type, uint8_t audio[MP_PORTS]) {
    memset(audio, 0, MP_PORTS);
    switch (type) {
    case MP_T_MAIN: audio[0] = audio[1] = 1; return 8;
    case MP_T_SHORT_DELAY: audio[0] = 1; return 4;
    case MP_T_TUBE_CLIP: audio[0] = audio[1] = 1; return 4;
    case MP_T_COMPRESSOR: audio[0] = audio[1] = 1; return 3;
    case MP_T_3BAND_EQ: audio[0] = 1; return 4;
    case MP_T_FORMANT: audio[0] = 1; return 4;
    default: return 0;   /* EMPTY or a type not modelled: no inputs, silent outputs */
    }
}

static int type_outputs(int type) {
    switch (type) {
    case MP_T_MAIN: case MP_T_TUBE_CLIP: case MP_T_COMPRESSOR: return 2;
    case MP_T_SHORT_DELAY: case MP_T_3BAND_EQ: case MP_T_FORMANT: return 1;
    default: return 0;
    }
}

/* source id -> (node, output) ; -1 for INPUT / GRF */
static int src_node(int src, int *out) {
    if (src == 8 || src == 9) { *out = src - 8; return 0; }
    if (src >= 10 && src < MP_NSRC) { *out = (src - 10) % 2; return 1 + (src - 10) / 2; }
    return -1;
}

static void node_dsp_init(mp_node *n, float fs) {
    memset(&n->u, 0, sizeof(n->u));
    switch (n->type) {
    case MP_T_MAIN: mp_bitrazer_init(&n->u.bit, fs); break;
    case MP_T_SHORT_DELAY: mp_delay_init(&n->u.delay, fs); break;
    case MP_T_TUBE_CLIP: mp_tube_init(&n->u.tube, fs); break;
    case MP_T_3BAND_EQ: mp_eq_init(&n->u.eq); break;
    default: break;   /* formant, compressor: zeroed state is their reset */
    }
}

static void control_tick(mp_engine *e) {
    for (int i = 0; i < MP_P_COUNT; ++i) {
        if (MP_PARAMS[i].integer) e->s[i] = e->p[i];
        else e->s[i] += e->k_smooth * (e->p[i] - e->s[i]);
    }
    /* GRF1-4 are knobs (0..1), GRF5-6 buttons (0/1). */
    e->src[2] = e->s[MP_P_GRF1];
    e->src[3] = e->s[MP_P_GRF2];
    e->src[4] = e->s[MP_P_GRF3];
    e->src[5] = e->s[MP_P_GRF4];
    e->src[6] = e->s[MP_P_GRF5];
    e->src[7] = e->s[MP_P_GRF6];

    const float fs = e->fs;
    for (int k = 0; k < MP_NODES; ++k) {
        mp_node *n = &e->node[k];
        for (int p = 0; p < MP_PORTS; ++p) {
            if (n->audio_port[p]) continue;
            float v = 0.0f;
            for (int j = 0; j < n->nin[p]; ++j) v += e->src[n->in_src[p][j]];
            n->cv[p] = v;
        }
        const float *r = n->raw01, *cv = n->cv;
        switch (n->type) {
        case MP_T_MAIN: {
            /* MOD IN 1..6 = ports 2..7, read as PARAMETER 1..6 (a guess, see
             * docs/ANALYSIS.md). A gate inverts the stored switch; a CV adds
             * LEVEL x CV to the stored knob. */
            int hpf = (r[0] > 0.0f) ^ (n->nin[2] && cv[2] > 0.5f);
            int byp = (r[1] > 0.0f) ^ (n->nin[3] && cv[3] > 0.5f);
            mp_bitrazer_set(&n->u.bit, clamp01(r[2] + r[6] * cv[4]), clamp01(r[4] + r[8] * cv[6]),
                            clamp01(r[3] + r[7] * cv[5]), clamp01(r[5] + r[9] * cv[7]), hpf, byp, fs);
            break;
        }
        case MP_T_SHORT_DELAY: {
            float pos = clamp01(clamp01(e->s[MP_P_TIME] - e->time_offset[k]) + cv[1]);
            mp_delay_set(&n->u.delay, mp_sd_time_ms(pos), mp_sd_feedback(r[1] + cv[2]),
                         mp_sd_wet(r[2] + cv[3]), fs);
            break;
        }
        case MP_T_FORMANT:
            mp_formant_set(&n->u.formant, n->raw01[8] + MP_FORMANT_CV_SPAN * cv[1],
                           n->raw01[9] + MP_FORMANT_CV_SPAN * cv[2], clamp01(r[2] + cv[3]), fs);
            break;
        case MP_T_TUBE_CLIP:
            mp_tube_set(&n->u.tube, clamp01(r[0] + cv[2]), clamp01(r[1] + cv[3]));
            break;
        case MP_T_3BAND_EQ:
            mp_eq_set(&n->u.eq, clamp01(r[0] + cv[1]), clamp01(r[1] + cv[2]), clamp01(r[2] + cv[3]), fs);
            break;
        case MP_T_COMPRESSOR:
            mp_comp_set(&n->u.comp, clamp01(r[0] + cv[2]), r[1], r[2], r[3], fs);
            break;
        default: break;
        }
    }
}

void mp_engine_init_with(mp_engine *e, float fs, const uint8_t main_raw[16],
                         const uint8_t sub_raw[6][5], const mp_edge *edges, int nedges) {
    memset(e, 0, sizeof(*e));
    e->fs = fs;
    for (int i = 0; i < MP_P_COUNT; ++i) e->p[i] = e->s[i] = MP_PARAMS[i].def;
    e->mix = e->p[MP_P_MIX];
    e->k_smooth = 1.0f - expf(-(float)MP_CR / (0.015f * fs));   /* 15 ms */
    e->k_mix = 1.0f - expf(-1.0f / (0.015f * fs));

    /* Node 0: the BITRAZER. Main record read as H1 (byte 0 a header byte,
     * bytes 1..10 = PARAMETER 1..10; docs/ANALYSIS.md). */
    mp_node *m = &e->node[0];
    m->type = MP_T_MAIN;
    for (int i = 0; i < 10; ++i) m->raw01[i] = mp_raw01(main_raw[1 + i]);
    m->raw01[0] = main_raw[1] ? 1.0f : 0.0f;   /* LPF/HPF and BYPASS are switches */
    m->raw01[1] = main_raw[2] ? 1.0f : 0.0f;
    m->out_src[0] = 8;
    m->out_src[1] = 9;
    for (int s = 0; s < 6; ++s) {
        mp_node *n = &e->node[1 + s];
        n->type = sub_raw[s][0];
        for (int i = 0; i < 4; ++i) n->raw01[i] = mp_raw01(sub_raw[s][1 + i]);
        if (n->type == MP_T_FORMANT) {   /* FORMANT 1/2 are vowel indices, not 0..100 */
            n->raw01[8] = mp_formant_vowel(sub_raw[s][1]);
            n->raw01[9] = mp_formant_vowel(sub_raw[s][2]);
        }
        n->out_src[0] = 10 + 2 * s;
        n->out_src[1] = 11 + 2 * s;
    }
    int ninputs[MP_NODES];
    for (int k = 0; k < MP_NODES; ++k) ninputs[k] = type_inputs(e->node[k].type, e->node[k].audio_port);

    /* Cables. A cable from or to a port the loaded module does not have is
     * rejected and counted (it would carry nothing). */
    if (nedges > MP_MAX_EDGES) nedges = MP_MAX_EDGES;
    for (int i = 0; i < nedges; ++i) {
        int s = edges[i].src, d = edges[i].dst, o;
        int sn = src_node(s, &o);
        if (s >= MP_NSRC || d >= MP_NDST || (sn >= 0 && o >= type_outputs(e->node[sn].type))) {
            e->edges_rejected++;
            continue;
        }
        if (d < 2) {
            if (e->nout[d] < MP_FANIN) { e->out_src[d][e->nout[d]++] = (uint8_t)s; e->edges_used++; }
            else e->edges_rejected++;
            continue;
        }
        int k = d < 10 ? 0 : 1 + (d - 10) / 4;
        int p = d < 10 ? d - 2 : (d - 10) % 4;
        mp_node *n = &e->node[k];
        if (p >= ninputs[k] || n->nin[p] >= MP_FANIN) {
            e->edges_rejected++;
            continue;
        }
        n->in_src[p][n->nin[p]++] = (uint8_t)s;
        e->edges_used++;
    }

    /* Evaluation order: Kahn's algorithm on node -> node cables; whatever is
     * left in a cycle runs in slot order and reads last-sample values. */
    int dep[MP_NODES][MP_NODES] = {{0}}, indeg[MP_NODES] = {0}, done[MP_NODES] = {0};
    for (int k = 0; k < MP_NODES; ++k)
        for (int p = 0; p < MP_PORTS; ++p)
            for (int j = 0; j < e->node[k].nin[p]; ++j) {
                int o, a = src_node(e->node[k].in_src[p][j], &o);
                if (a >= 0 && a != k && !dep[a][k]) { dep[a][k] = 1; indeg[k]++; }
            }
    int count = 0;
    while (count < MP_NODES) {
        int pick = -1;
        for (int k = 0; k < MP_NODES && pick < 0; ++k)
            if (!done[k] && indeg[k] == 0) pick = k;
        if (pick < 0) {
            e->cyclic = 1;
            for (int k = 0; k < MP_NODES && pick < 0; ++k)
                if (!done[k]) pick = k;
        }
        done[pick] = 1;
        e->order[count++] = pick;
        for (int k = 0; k < MP_NODES; ++k)
            if (dep[pick][k] && !done[k]) indeg[k]--;
    }

    /* TIME (knob 8) is the panel position of the longest SHORT DELAY; each
     * delay keeps its stored distance below it. */
    float pmax = 0.0f;
    for (int s = 0; s < 6; ++s)
        if (sub_raw[s][0] == MP_T_SHORT_DELAY && mp_sd_time_pos(sub_raw[s][1]) > pmax)
            pmax = mp_sd_time_pos(sub_raw[s][1]);
    for (int s = 0; s < 6; ++s)
        if (sub_raw[s][0] == MP_T_SHORT_DELAY) e->time_offset[1 + s] = pmax - mp_sd_time_pos(sub_raw[s][1]);

    for (int k = 0; k < MP_NODES; ++k) node_dsp_init(&e->node[k], fs);
    control_tick(e);
    /* the delays start AT their time, not gliding to it */
    for (int k = 0; k < MP_NODES; ++k)
        if (e->node[k].type == MP_T_SHORT_DELAY) e->node[k].u.delay.d = e->node[k].u.delay.d_target;
}

void mp_engine_init(mp_engine *e, float fs) {
    mp_edge edges[MP_EDGE_COUNT];
    for (int i = 0; i < MP_EDGE_COUNT; ++i) {
        edges[i].src = MP_EDGES[i].src;
        edges[i].dst = MP_EDGES[i].dst;
    }
    mp_engine_init_with(e, fs, MP_MAIN_RAW, MP_SUB_RAW, edges, MP_EDGE_COUNT);
}

void mp_engine_set(mp_engine *e, int idx, float v) {
    if (idx < 0 || idx >= MP_P_COUNT || !isfinite(v)) return;
    const mp_param_info *info = &MP_PARAMS[idx];
    if (info->integer) v = floorf(v + 0.5f);
    if (v < info->min) v = info->min;
    if (v > info->max) v = info->max;
    e->p[idx] = v;
}

void mp_engine_snap(mp_engine *e) {
    for (int i = 0; i < MP_P_COUNT; ++i) e->s[i] = e->p[i];
    e->mix = e->p[MP_P_MIX];
    control_tick(e);
    for (int k = 0; k < MP_NODES; ++k)
        if (e->node[k].type == MP_T_SHORT_DELAY) e->node[k].u.delay.d = e->node[k].u.delay.d_target;
}

void mp_engine_reset(mp_engine *e) {
    for (int k = 0; k < MP_NODES; ++k) node_dsp_init(&e->node[k], e->fs);
    memset(e->src, 0, sizeof(e->src));
    control_tick(e);
    for (int k = 0; k < MP_NODES; ++k)
        if (e->node[k].type == MP_T_SHORT_DELAY) e->node[k].u.delay.d = e->node[k].u.delay.d_target;
}

static void run_node(mp_engine *e, mp_node *n) {
    float a[2] = {0.0f, 0.0f}, out[2] = {0.0f, 0.0f};
    for (int p = 0; p < 2; ++p)
        if (n->audio_port[p])
            for (int j = 0; j < n->nin[p]; ++j) a[p] += e->src[n->in_src[p][j]];
    switch (n->type) {
    case MP_T_MAIN: mp_bitrazer_tick(&n->u.bit, a, out); break;
    case MP_T_SHORT_DELAY: out[0] = mp_delay_tick(&n->u.delay, a[0]); break;
    case MP_T_FORMANT: out[0] = mp_formant_tick(&n->u.formant, a[0]); break;
    case MP_T_TUBE_CLIP: mp_tube_tick(&n->u.tube, a, out); break;
    case MP_T_3BAND_EQ: out[0] = mp_eq_tick(&n->u.eq, a[0]); break;
    case MP_T_COMPRESSOR: mp_comp_tick(&n->u.comp, a, out); break;
    default: break;
    }
    e->src[n->out_src[0]] = out[0];
    e->src[n->out_src[1]] = out[1];
}

void mp_engine_process_f(mp_engine *e, const float *in, float *out, int frames) {
    for (int i = 0; i < frames; ++i) {
        if (e->sample++ % MP_CR == 0) control_tick(e);
        float dl = in[2 * i], dr = in[2 * i + 1];
        e->src[0] = dl;
        e->src[1] = dr;
        for (int k = 0; k < MP_NODES; ++k) run_node(e, &e->node[e->order[k]]);
        float w[2];
        for (int c = 0; c < 2; ++c) {
            w[c] = 0.0f;
            for (int j = 0; j < e->nout[c]; ++j) w[c] += e->src[e->out_src[c][j]];
        }
        if (!isfinite(w[0]) || !isfinite(w[1])) {
            e->nan_resets++;
            mp_engine_reset(e);
            w[0] = w[1] = 0.0f;
        }
        /* a resonance peak or a runaway cable must not reach the speakers */
        for (int c = 0; c < 2; ++c) w[c] = w[c] > 4.0f ? 4.0f : w[c] < -4.0f ? -4.0f : w[c];
        e->mix += e->k_mix * (e->p[MP_P_MIX] - e->mix);
        out[2 * i] = dl + e->mix * (w[0] - dl);
        out[2 * i + 1] = dr + e->mix * (w[1] - dr);
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
