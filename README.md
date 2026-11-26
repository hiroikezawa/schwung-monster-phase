# MONSTER PHASE

**English** | [日本語](README.ja.md)

A stereo audio effect for Schwung on Ableton Move. It is based on the patch
`BITRAZER_MONSTER_PHASE`, made with AIRA Modular Customizer for the Roland AIRA
Modular **BITRAZER** (a bitcrusher pedal), and rebuilt for Move.

```
input → pre-delay → formant → crusher → resonant LPF → bass boost → tube
      → + post-delay → level → MIX (blend with the dry signal) → output
```

- **Crusher**: a sample-and-hold that lowers the sample rate. No anti-aliasing;
  the aliasing is kept as part of the sound. Bit depth is not reduced.
- **Resonant LPF**: a state-variable filter. The cutoff stops at 120 Hz, so the
  low end stays even when it is closed all the way.
- **Formant**: three formants for the vowels a → i → u → e → o (the standard
  tenor table). The knob morphs the vowel continuously; the amount (VOW AMT)
  sets the balance with the unprocessed signal.
- **Tube**: an asymmetric soft clip (even harmonics), 4× oversampled. The output
  is lowered automatically so the level changes little as DRIVE goes up.
- **Bass boost**: a 110 Hz low shelf, 0 to +12 dB. It sits before the tube, so
  the more you add, the fatter and more distorted the low end gets.
- **Two delays**: the first is a delay up to 80 ms like the original patch's
  SHORT DELAY (with feedback and dry/wet). The second is a 20–1000 ms stereo
  delay added on top of the signal.

## Stereo

Every stage runs separately for left and right, and WIDTH sets how much the two
sides differ. At WIDTH 100 %:

| Stage | Left/right difference |
|---|---|
| Pre-delay | Right is 3 % shorter (the same difference as the original patch's two SHORT DELAYs) |
| Formant | Left's vowel sits earlier, right's later (±0.35 vowel) |
| Crusher | Right samples half a period later |
| Post-delay | Right is 25 % longer, and half of the feedback crosses to the other side |

At WIDTH 0 % a mono input comes out mono.

## Knobs

Page 1 (sound):

| Knob | Display | What it does | Default |
|---|---|---|---|
| 1 | MIX | Balance of dry and effect. At 0 % processing stops and almost no CPU is used | 100 % |
| 2 | CRUSH | Sample rate. 100 % is unprocessed; lower is rougher (same feel as knob 2 in 0.1.0) | 70 % |
| 3 | CUTOFF | Low-pass cutoff 120 Hz – 18 kHz | 80 % |
| 4 | RESO | Resonance (Q 0.6–14) | 40 % |
| 5 | VOWEL | Formant vowel a, i, u, e, o | 0 % (a) |
| 6 | DRIVE | Tube distortion (0 to +30 dB) | 35 % |
| 7 | BASS | Bass boost (0 to +12 dB) | 40 % |
| 8 | DELAY | Post-delay level | 30 % |

Page 2 (delays and stereo), switched with the jog wheel:

| Knob | Display | What it does | Default |
|---|---|---|---|
| 1 | PRE T | Pre-delay time 0.5–80 ms | 80 ms |
| 2 | PRE FB | Pre-delay feedback | 61 % |
| 3 | PRE MX | Pre-delay dry/wet | 48 % |
| 4 | POST T | Post-delay time 20–1000 ms | 300 ms |
| 5 | POST FB | Post-delay feedback | 35 % |
| 6 | WIDTH | Stereo width | 60 % |
| 7 | VOW AMT | Formant amount | 60 % |
| 8 | LEVEL | Output level −12 to +12 dB | 0 dB |

The pre-delay defaults (80 ms, 61 %, 48 %) come from the values stored in the
original patch's SHORT DELAY. Bring PRE T down to a few ms for flanger- or
comb-filter-like sounds.

## Requirements

- An Ableton Move with Schwung installed
- Works in a track slot and in Master FX (standard knob grid)

## Installing

```bash
python3 scripts/install.py http://<Move's IP>:7700
```

Restart Move after installing.

## Building and testing

```bash
bash scripts/build.sh arm64     # dist/monster-phase-module.tar.gz
bash scripts/test.sh            # all tests
```

`tests/test_engine.c` checks:

- Each stage: delay time and feedback, the resonance peak, formant frequencies
  per vowel, the amount of bass boost, the tube's harmonics and the effect of
  oversampling, the crusher's interval and left/right offset
- Overall: with any knob turned to its ends the level stays within −12 to +9 dB
  of the input, the default output is stereo, WIDTH 0 stays mono
- Stability: moving every knob wildly every block does not blow up, and the
  output dies away when the input stops
- Other: the same output regardless of block length, 48 kHz operation, CPU load,
  the plugin API

It also runs under AddressSanitizer + UBSan, and `tests/test_grid.mjs` uses
Schwung's page engine to check the display and writes of both pages.

## About the original patch

0.1.0 tried to reproduce the original patch faithfully from its file. That
analysis (the file structure, the inferred wiring, the patch analysis tool
`tools/aira_patch.py`) is kept in [docs/ANALYSIS.md](docs/ANALYSIS.md), `patch/`
and `reference/`. From 0.2.0 reproduction was abandoned, and the design is an
original one built from the original patch's elements (crusher, filter,
formant, tube, bass, two delays).

## Licence

MIT ([LICENSE](LICENSE)). Copyright (c) 2026 Hiro Ikezawa. For third-party code
see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). Roland, AIRA and
BITRAZER are trademarks of Roland; this module is not affiliated with Roland.

## Changes

- 0.2.1 — At MIX 0 processing stops and almost no CPU is used (the sound is the
  input itself). When MIX goes up, the delays are cleared before it starts again
  (echoes from before the stop do not come back)
- 0.2.0 — Rebuilt. Changed to a stereo serial chain (pre-delay, formant,
  crusher, resonant LPF, bass, tube, post-delay). 2 pages, 16 knobs. Bit-depth
  reduction removed, the cutoff's lower limit raised to 120 Hz
- 0.1.0 — First version, aiming to reproduce the original patch
