/*
 * MONSTER PHASE - CPU benchmark meant to run ON the Move (static aarch64
 * binary).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Runs the engine as the chain host does - 128-frame int16 blocks at
 * 44.1 kHz - and reports mean and worst block time. Normal priority: it
 * measures the code, it does not compete with Move's realtime threads (pin
 * it with taskset 0x7).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mp_engine.h"

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static mp_engine e;

int main(void) {
    const char *names[3] = {"defaults", "all knobs moving", "crushed + max reso"};
    int16_t buf[256];
    uint32_t rng = 1;
    for (int c = 0; c < 3; ++c) {
        mp_engine_init(&e, 44100.0f);
        if (c == 2) {
            mp_engine_set(&e, MP_P_CRUSH, 0.1f);
            mp_engine_set(&e, MP_P_RESO, 1.0f);
            mp_engine_snap(&e);
        }
        double worst = 0, total = 0;
        const int warm = 400, n = 3445 * 2;   /* ~20 s measured */
        for (int i = 0; i < warm + n; ++i) {
            for (int k = 0; k < 256; ++k) {
                rng = rng * 1664525u + 1013904223u;
                buf[k] = (int16_t)((int32_t)rng >> 18);
            }
            if (c == 1)
                for (int p = 1; p < MP_P_COUNT; ++p) {
                    rng = rng * 1664525u + 1013904223u;
                    mp_engine_set(&e, p, MP_PARAMS[p].min + (MP_PARAMS[p].max - MP_PARAMS[p].min) * ((rng >> 8) * (1.0f / 16777216.0f)));
                }
            double t0 = now_us();
            mp_engine_process(&e, buf, 128);
            double dt = now_us() - t0;
            if (i < warm) continue;
            total += dt;
            if (dt > worst) worst = dt;
        }
        printf("%-18s mean %6.1f us (%4.1f %% of 2902 us)  worst %6.1f us\n", names[c], total / n,
               100.0 * total / n / 2902.0, worst);
    }
    printf("instance size %.1f KB\n", sizeof(mp_engine) / 1024.0);
    return 0;
}
