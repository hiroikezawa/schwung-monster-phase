/*
 * MONSTER PHASE - the patch as a data-driven graph.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The graph is built from data, never from a hard-coded chain: the module
 * types and raw parameters come from the patch file and the cables from a
 * routing table (both compiled in by tools/aira_patch.py, mp_patch_data.h).
 * Ports use the public SysEx patching ids: 22 sources (INPUT1-2, GRF1-6,
 * MAIN OUT1-2, SUB1-6 OUT1-2) and 34 destinations (OUTPUT1-2, MAIN IN1-8,
 * SUB1-6 IN1-4). Several cables into one input are summed; one output may
 * feed many inputs; a cycle is legal and costs one sample of delay (every
 * node reads the other nodes' outputs as they stand, so a node later in the
 * order reads this sample's value and one earlier reads the last sample's).
 *
 * Audio inputs are summed every sample. Control inputs (CV and gates) are
 * read every MP_CR samples, when coefficients are recomputed; the GRF
 * sources only change at that rate anyway.
 */
#ifndef MP_ENGINE_H
#define MP_ENGINE_H

#include <stdint.h>

#include "mp_dsp.h"
#include "mp_params.h"

#define MP_CR 16              /* control-rate period in samples */
#define MP_NSRC 22
#define MP_NDST 34
#define MP_NODES 7            /* 0 = MAIN (BITRAZER), 1..6 = SUB1..6 */
#define MP_PORTS 8            /* MAIN has 8 inputs, a sub-module 4 */
#define MP_FANIN 8
#define MP_MAX_EDGES 64

enum { MP_T_EMPTY = 0, MP_T_SHORT_DELAY = 16, MP_T_TUBE_CLIP = 17, MP_T_COMPRESSOR = 18,
       MP_T_3BAND_EQ = 20, MP_T_FORMANT = 28, MP_T_MAIN = 255 };

typedef struct { uint8_t src, dst; } mp_edge;

typedef struct {
    int type;                          /* sub-module type id, MP_T_MAIN for node 0 */
    float raw01[10];                   /* stored parameters 0..1 (main: P1..P10) */
    int nin[MP_PORTS];                 /* cables into each input */
    uint8_t in_src[MP_PORTS][MP_FANIN];
    uint8_t audio_port[MP_PORTS];      /* 1 = audio (summed per sample) */
    float cv[MP_PORTS];                /* control inputs, read at control rate */
    int out_src[2];                    /* source ids of OUT1/OUT2 */
    union {
        mp_delay delay;
        mp_formant formant;
        mp_tube tube;
        mp_eq eq;
        mp_comp comp;
        mp_bitrazer bit;
    } u;
} mp_node;

typedef struct {
    float fs;
    float p[MP_P_COUNT];               /* knob targets */
    float s[MP_P_COUNT];               /* smoothed (control rate) */
    float mix;                         /* per-sample smoothed MIX */
    float k_smooth, k_mix;

    mp_node node[MP_NODES];
    int order[MP_NODES];               /* evaluation order (topological where possible) */
    int cyclic;                        /* 1 if the routing has a cycle */
    int nout[2];
    uint8_t out_src[2][MP_FANIN];      /* cables into OUTPUT1/2 */
    int edges_used, edges_rejected;

    float time_offset[MP_NODES];       /* per SHORT DELAY: stored position below the max */
    float src[MP_NSRC];                /* every source's current value */
    uint32_t sample;                   /* drives the control clock (block-size independent) */
    int nan_resets;
} mp_engine;

/* Build from the compiled-in patch and routing. Allocation-free. */
void mp_engine_init(mp_engine *e, float fs);
/* Build from explicit data (the tests use other routings). */
void mp_engine_init_with(mp_engine *e, float fs, const uint8_t main_raw[16],
                         const uint8_t sub_raw[6][5], const mp_edge *edges, int nedges);
void mp_engine_set(mp_engine *e, int idx, float v);
void mp_engine_snap(mp_engine *e);    /* jump smoothing to the targets */
void mp_engine_process(mp_engine *e, int16_t *audio, int frames);
/* float in/out, for the tests: interleaved stereo */
void mp_engine_process_f(mp_engine *e, const float *in, float *out, int frames);
void mp_engine_reset(mp_engine *e);   /* clear all audio state, keep settings */

#endif
