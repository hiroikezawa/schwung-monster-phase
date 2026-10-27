# Third-party code in MONSTER PHASE

MONSTER PHASE's DSP is entirely original (Copyright (c) 2026 Hiro Ikezawa,
MIT, see `LICENSE`). It is an independent design after a patch made in
Roland's AIRA Modular Customizer for the Roland BITRAZER. The patch analyser
in `tools/` used the unofficial MIDI implementation by Mugenkidou
(https://github.com/mugenkidou/AIRA_Modular_Effects) only as a reference for
module type ids, parameter order and patch-point ids; no code. Roland's DSP
is not published and no Roland code or data is included.
The primitives - TPT state-variable filters, an RBJ shelf, a Hermite
fractional delay read, a windowed-sinc 4x resampler, a rational tanh, a
sample-and-hold crusher - were written here from the mathematics. The vowel formant table is the standard published tenor
formant data (frequencies, levels, bandwidths), not anyone's code.

"Roland", "AIRA" and "BITRAZER" are trademarks of Roland Corporation; this
module is an independent re-creation, not affiliated with or endorsed by
Roland.

`patch/BITRAZER_MONSTER_PHASE.bin` is the user's own patch file; the files
in `reference/` (the user's screenshot and notes) stay in the repository for
the analysis and are not shipped in the package.

Nothing GPL, LGPL or AGPL is compiled in, linked or copied:
`monster-phase.so` needs only `libc` and `libm` (`scripts/check_abi.py` fails
the build otherwise), and `scripts/check_licenses.py` fails if copyleft text
appears in the source.

The only third-party files are Schwung's plugin API headers, used unmodified
to compile against the host ABI.

## Schwung plugin API headers — MIT

| | |
|---|---|
| Files | `third_party/schwung/host/audio_fx_api_v2.h`, `third_party/schwung/host/plugin_api_v1.h` |
| Upstream | https://github.com/charlesvestal/schwung (`src/host/`) |
| Copyright | Copyright (c) 2025-2026 Charles Vestal |
| Licence | MIT, reproduced in `third_party/schwung/LICENSE` |

They are compiled into `monster-phase.so` only as declarations (struct
layouts, constants); no Schwung code is linked.
