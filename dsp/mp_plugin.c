/*
 * MONSTER PHASE - Schwung audio FX v2 adapter.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every entry point here runs on Move's SPI callback (or, for a bus insert,
 * the bus worker): no allocation, no file I/O, no locks. Instances come from
 * a static pool claimed with an atomic compare-and-swap, so even
 * create_instance allocates nothing.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "host/audio_fx_api_v2.h"
#include "mp_engine.h"
#include "mp_metadata.h"
#include "mp_patch_data.h"

#define MP_POOL 8

typedef struct {
    int used;
    mp_engine engine;
} mp_slot;

static mp_slot g_pool[MP_POOL];
static int g_host_rate = 44100;

/* Schwung's standard knob grid, one page of eight. Served as ui_hierarchy,
 * so the shadow UI opens it in a track slot AND in Master FX. */
#define MP_KEYS "\"mix\",\"g1_srate\",\"g2_cutoff\",\"g3_formant\",\"g4_low\",\"g5_type\",\"g6_bypass\",\"time\""
static const char k_hierarchy[] =
    "{\"modes\":null,\"levels\":{"
      "\"root\":{\"label\":\"MONSTER PHASE\","
        "\"knobs\":[" MP_KEYS "],"
        "\"params\":[" MP_KEYS "]}"
    "}}";

static const char *const *enum_names(int idx) {
    if (idx == MP_P_GRF5) return MP_TYPE_NAMES;
    if (idx == MP_P_GRF6) return MP_ONOFF_NAMES;
    return NULL;
}

static int param_index(const char *key) {
    for (int i = 0; i < MP_P_COUNT; ++i)
        if (!strcmp(key, MP_PARAMS[i].key)) return i;
    return -1;
}

static const char *skip_space(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

/* A number, or (enums) an option name in any case. */
static int parse_value(int idx, const char *text, float *out) {
    const char *const *names = enum_names(idx);
    const char *p = skip_space(text);
    for (int i = 0; names && i < 2; ++i) {
        size_t n = strlen(names[i]);
        if (!strncasecmp(p, names[i], n) && !*skip_space(p + n)) {
            *out = (float)i;
            return 1;
        }
    }
    char *end;
    float v = strtof(p, &end);
    if (end == p || *skip_space(end) || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

/* Flat JSON object of numbers. Every knob must be present; unknown keys
 * (e.g. "version") are skipped. Nothing is applied unless all is valid. */
static int restore_state(mp_engine *e, const char *text) {
    if (strlen(text) > 2048) return 0;
    float next[MP_P_COUNT];
    unsigned seen = 0;
    const unsigned need = (1u << MP_P_COUNT) - 1;
    const char *p = skip_space(text);
    if (*p++ != '{') return 0;
    for (int field = 0; field < 64; ++field) {
        p = skip_space(p);
        if (*p++ != '"') return 0;
        char key[32];
        int n = 0;
        while (*p && *p != '"' && n < (int)sizeof(key) - 1) key[n++] = *p++;
        key[n] = 0;
        if (*p++ != '"') return 0;
        p = skip_space(p);
        if (*p++ != ':') return 0;
        p = skip_space(p);
        char *end;
        float v = strtof(p, &end);
        if (end == p || !isfinite(v)) return 0;
        p = end;
        int idx = param_index(key);
        if (idx >= 0) {
            if (seen & (1u << idx)) return 0;
            seen |= 1u << idx;
            next[idx] = v;
        }
        p = skip_space(p);
        if (*p == '}') {
            if (*skip_space(p + 1)) return 0;
            if ((seen & need) != need) return 0;
            for (int i = 0; i < MP_P_COUNT; ++i) mp_engine_set(e, i, next[i]);
            mp_engine_snap(e);
            return 1;
        }
        if (*p++ != ',') return 0;
    }
    return 0;
}

static int copy_text(char *buf, int size, const char *text) {
    size_t n = strlen(text);
    if (n >= (size_t)size) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, text, n + 1);
    return (int)n;
}

static void *create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;
    for (int i = 0; i < MP_POOL; ++i) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_pool[i].used, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            mp_engine_init(&g_pool[i].engine, (float)g_host_rate);
            return &g_pool[i].engine;
        }
    }
    return NULL;
}

static void destroy_instance(void *instance) {
    for (int i = 0; i < MP_POOL; ++i) {
        if (instance == &g_pool[i].engine) {
            __atomic_store_n(&g_pool[i].used, 0, __ATOMIC_RELEASE);
            return;
        }
    }
}

static void process_block(void *instance, int16_t *audio, int frames) {
    if (!instance || !audio || frames <= 0) return;
    mp_engine_process((mp_engine *)instance, audio, frames);
}

static void set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key || !val) return;
    mp_engine *e = (mp_engine *)instance;
    if (!strcmp(key, "state")) {
        (void)restore_state(e, val);
        return;
    }
    int idx = param_index(key);
    float v;
    if (idx < 0 || !parse_value(idx, val, &v)) return;
    mp_engine_set(e, idx, v);
}

static int get_param(void *instance, const char *key, char *buf, int size) {
    if (!instance || !key || !buf || size <= 0) return -1;
    buf[0] = 0;
    const mp_engine *e = (const mp_engine *)instance;
    int n = -1;
    int idx = param_index(key);
    if (idx >= 0) {
        const char *const *names = enum_names(idx);
        n = names ? snprintf(buf, size, "%s", names[(int)e->p[idx]]) : snprintf(buf, size, "%.4f", e->p[idx]);
    } else if (!strcmp(key, "name")) {
        return copy_text(buf, size, "MONSTER PHASE");
    } else if (!strcmp(key, "chain_params")) {
        return copy_text(buf, size, kChainParams);
    } else if (!strcmp(key, "ui_hierarchy")) {
        return copy_text(buf, size, k_hierarchy);
    } else if (!strcmp(key, "state")) {
        n = snprintf(buf, size,
            "{\"version\":1,\"mix\":%.6g,\"g1_srate\":%.6g,\"g2_cutoff\":%.6g,\"g3_formant\":%.6g,"
            "\"g4_low\":%.6g,\"g5_type\":%d,\"g6_bypass\":%d,\"time\":%.6g}",
            e->p[MP_P_MIX], e->p[MP_P_GRF1], e->p[MP_P_GRF2], e->p[MP_P_GRF3], e->p[MP_P_GRF4],
            (int)e->p[MP_P_GRF5], (int)e->p[MP_P_GRF6], e->p[MP_P_TIME]);
    } else if (!strcmp(key, "version")) {
        return copy_text(buf, size, kVersion);
    } else if (!strcmp(key, "patch_sha256")) {
        return copy_text(buf, size, MP_PATCH_SHA256);
    } else if (!strcmp(key, "routing")) {
        return copy_text(buf, size, MP_ROUTING_ID);
    } else if (!strcmp(key, "nan_resets")) {
        n = snprintf(buf, size, "%d", e->nan_resets);
    }
    if (n < 0 || n >= size) {
        buf[0] = 0;
        return -1;
    }
    return n;
}

static audio_fx_api_v2_t g_api = {
    AUDIO_FX_API_VERSION_2,
    create_instance,
    destroy_instance,
    process_block,
    set_param,
    get_param,
    NULL,   /* on_midi: not used */
};

__attribute__((visibility("default")))
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    if (host && host->sample_rate > 0) {
        if (host->sample_rate != 44100 && host->sample_rate != 48000) return NULL;
        g_host_rate = host->sample_rate;
    }
    return &g_api;
}
