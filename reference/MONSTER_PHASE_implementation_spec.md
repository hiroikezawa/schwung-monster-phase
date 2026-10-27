# MONSTER PHASE 移植仕様 v1.0（静的解析で確認できた範囲）

## 目的・制約
Roland AIRA Modular Customizer の `BITRAZER_MONSTER_PHASE.bin` をソースとし、Schwangへ再現実装する。外部音響測定なしの段階で、**トポロジーや音響的完全一致は確定していない**。パラメータが既知でもRoland非公開DSPの近似実装は別問題である。

## 入力ファイル
- ファイル名: `BITRAZER_MONSTER_PHASE.bin`
- サイズ: 1212 bytes (`0x4BC`)
- SHA256: `f27c95ff684e9e155f7c71ea75f53fe5d62701623b0ee663f847b30584d23843`
- `0x00..0x0F`: ASCII `HabPatchFile0001`
- `0x10..0x27`: 24-byte ASCII name field: `AiraEfx Patch` + space padding (in this file).
- `0x28..0x37`: main module record; hex `01 00 00 00 00 32 00 64 64 32 64 00 00 00 00 00`; field mapping unverified.

## Confirmed module record layout (by cross-checking IDs)
`0x38 + 8*(slot-1)` through `0x67`: each 8-byte record: `[type_id, parameter_1, parameter_2, parameter_3, parameter_4, reserved0, reserved1, reserved2]`.

| Slot | Module | ID | Param raw | Meaning (public SysEx mapping) |
|---|---|---:|---|---|
| 1 | SHORT DELAY | 16 | 3,61,48,0 | Time, Feedback, Dry/Wet, unused (order should also be checked in Customizer UI) |
| 2 | SHORT DELAY | 16 | 0,61,48,0 | Time, Feedback, Dry/Wet, unused |
| 3 | FORMANT FILTER | 28 | 0,1,0,0 | Formant 1, Formant 2, Balance, unused |
| 4 | TUBE CLIP | 17 | 27,75,0,0 | Gain, Output Level, unused, unused |
| 5 | 3 BAND EQ | 20 | 50,50,50,0 | High, Mid, Low, unused |
| 6 | COMPRESSOR | 18 | 0,0,10,10 | Threshold, Ratio, Attack, Release |

Raw numeric values are not necessarily milliseconds, Hz, dB, or ratio. No units or curves are yet proven.

### Important screenshot cross-check: SHORT DELAY TIME
In the supplied original screenshot, **both SHORT DELAY TIME pointers are positioned near the clockwise maximum end**. This conflicts with naïvely interpreting raw TIME = 3 and 0 as very short delay times. Do not convert raw TIME with an unverified ascending map or initialize the DSP to near-zero milliseconds. A reversed mapping (smaller raw value = longer delay) is one possible explanation; another is an incorrect parameter-index mapping. Neither is proven. Verify against Customizer UI values or controlled preset-file changes before specifying milliseconds. Both TIME controls should be represented as near maximum *visual setting* until the conversion is validated.

## Wiring / GRF assignments: NOT confirmed
- `0x80..0x47F` is exactly 1024 bytes and contains 18 nonzero bytes. It **may** encode cable routing or UI wiring geometry; position/value semantics have not been established. Do NOT interpret its 32×32 byte representation as a proven source/destination matrix.
- `0x480..0x4BB` is 60 zero bytes in this file.
- The published unofficial SysEx document defines endpoints and cable connection requests (`10 20 ss dd`), but **does not document this local `.bin` serialization**. Do not confuse their layouts.
- To determine the mapping, use safe offline differential analysis: save a second Customizer patch with exactly one wire removed/added; compare binary byte offsets against the untouched backup. Repeat for one GRF patch if necessary. Do not overwrite the original or require any writes to hardware.
- Until then, wiring, channel routing, GRF assignments, and feedback cycle presence are **unverified**.

## Source and destination endpoint IDs from the unofficial SysEx reference (not `.bin` indices)
- Sources: `0 INPUT1, 1 INPUT2, 2..7 GRF1..6, 8..9 MAIN OUT1..2, 10..21 SUB1..6 OUT1..2`.
- Destinations: `0 OUTPUT1, 1 OUTPUT2, 2..9 MAIN IN1..8, 10..33 SUB1..6 IN1..4`.
- Valid input/output pins depend on module. See https://github.com/mugenkidou/AIRA_Modular_Effects.

## Roland DSP model status
- SHORT DELAY: Roland describes up to 80ms, but exact time mapping/interpolation/feedback law unknown.
- TUBE CLIP: valve-like soft-clipping with even-order harmonic characteristics is described; transfer function unknown.
- FORMANT FILTER, EQ, COMPRESSOR, main BITRAZER: exact internal filters, parameter curves, nonlinearities, oversampling, level calibration unknown.
- Avoid claims of bit-identical or perceptually identical sound without references.

## Schwang requirements: implementation architecture
1. Add `MonsterPhase` as a dedicated effect with raw patch source metadata and a separately versioned DSP approximation.
2. Implement six independent processors plus main BITRAZER block and a directed signal/control graph. Preserve two channels and summing rules once connections are decoded.
3. The routing graph must be data-driven; no hard-coded guessed signal order is allowed as 'original'. Provide explicit provisional fallback only behind a named `approximateRouting` mode.
4. Knob 1 = Schwang MIX (product UI decision, not original). For knobs 2..8, defer assignment until GRF wiring decoded; do not label guessed assignments as original.
5. Keep the original raw parameter values in patch metadata, then map to DSP units in a testable versioned conversion layer.
6. Handle feedback with deterministic block/sample update order, clamp numerical instability and denormals, and ensure no allocations or blocking calls in real-time audio callback.
7. Add tests: binary parse roundtrip, slot IDs and raw values, routing consistency once decoded, unit tests for each block, CPU/NaN/stereo regressions.
8. Provide a documented list of differences from Roland including measured vs inferred findings.

## Completion criteria and status
- **Static patch parameters**: verified against actual bytes and module IDs.
- **Routing / GRF**: blocked pending offline differential patch comparison or validated file format description.
- **DSP numerical equivalence**: not verified; limited audio A/B recommended only after first complete implementation.
- Therefore this is a **frozen implementation baseline**, **not a fully determined exact-reproduction specification**.

## References
- Roland AIRA Modular Customizer: https://www.roland.com/global/products/aira_modular_customizer/specifications/
- Unofficial MIDI implementation: https://github.com/mugenkidou/AIRA_Modular_Effects
- Roland engineering descriptions: https://rolandcorp.com.au/blog/inside-the-aira-modular-customizer
