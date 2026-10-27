/*
 * MONSTER PHASE - knob values -> DSP units.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every curve lives here so the tests can pin it. Since 0.2.0 this is a
 * free design in the spirit of the BITRAZER_MONSTER_PHASE patch, not a
 * re-creation of Roland's curves.
 */
#ifndef MP_CONVERT_H
#define MP_CONVERT_H

#define MP_CONVERT_VERSION 2

float mp_crush_hz(float n, float fs);     /* CRUSH: fs/256 at 0 .. fs at 1 (as 0.1.0's knob 2) */
float mp_cutoff_hz(float n);              /* CUTOFF: 120 Hz .. 18 kHz, exponential */
float mp_reso_q(float n);                 /* RESO: Q 0.6 .. 14 */
float mp_vowel(float n);                  /* FORMANT: 0..1 -> vowel index 0 (a) .. 4 (o) */
float mp_drive_db(float n);               /* DRIVE: 0 .. +30 dB into the tube */
float mp_bass_db(float n);                /* BASS: 0 .. +12 dB low shelf */
#define MP_BASS_HZ 110.0f
float mp_db_to_gain(float db);

/* stereo offsets at WIDTH 1 */
#define MP_PRE_SPREAD 0.03f               /* right pre delay 3 % shorter (the patch's two delays) */
#define MP_POST_SPREAD 0.25f              /* right post delay 25 % longer */
#define MP_POST_CROSS 0.5f                /* share of post feedback that crosses sides */
#define MP_VOWEL_SPREAD 0.35f             /* vowel index offset, left down / right up */
#define MP_CRUSH_SPREAD 0.5f              /* hold clock phase offset of the right side */

#endif
