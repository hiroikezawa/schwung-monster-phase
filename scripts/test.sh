#!/usr/bin/env bash
# Every test: the patch analyser, native engine tests (plain and ASan/UBSan),
# the knob grid against Schwung's real page engine, the Move build with its
# ABI check, the device benchmark binary, and the licence scan.
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
API="$MODULE/third_party/schwung"
SRC=("$MODULE"/dsp/*.c)
INC=(-I"$MODULE/dsp" -I"$MODULE/tests" -I"$MODULE/build/generated" -I"$API")

echo "== patch analyser (tools/aira_patch.py)"
python3 "$MODULE/tests/test_patch.py"

echo "== native engine tests"
bash "$MODULE/scripts/build.sh" native
"${CC:-gcc}" -std=gnu11 -O2 -g -ffp-contract=off -Wall -Wextra -Wno-unused-function "${INC[@]}" \
  "$MODULE/tests/test_engine.c" "${SRC[@]}" -lm -o "$MODULE/build/native/test-engine"
"$MODULE/build/native/test-engine"

echo "== the same under AddressSanitizer + UBSan"
"${CC:-gcc}" -std=gnu11 -O1 -g -ffp-contract=off -Wall -Wextra -Wno-unused-function "${INC[@]}" \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  "$MODULE/tests/test_engine.c" "${SRC[@]}" -lm -o "$MODULE/build/native/test-engine-asan"
# --quick shortens the long stability runs (they ran in full above, unsanitised)
ASAN_OPTIONS=detect_leaks=1 "$MODULE/build/native/test-engine-asan" --quick | grep -E "FAIL|PASSED|FAILURE"

echo "== knob grid test (Schwung's real page engine)"
# The page engine: a mirror of the device's shared/ in build/device-shared if
# there is one (scp -r ableton@move.local:/data/UserData/schwung/shared
# build/device-shared/), else the ../schwung checkout. The framebuffer harness
# always comes from ../schwung (or SCHWUNG_ROOT).
python3 "$MODULE/tests/dump_contract.py" "$MODULE/build/native/monster-phase.so" "$MODULE/build/native/contract.json"
ROOT="${SCHWUNG_ROOT:-$MODULE/../schwung}"
SHARED="${MP_SHARED:-}"
[[ -z "$SHARED" && -d "$MODULE/build/device-shared/shared" ]] && SHARED="$MODULE/build/device-shared/shared"
if [[ ! -f "$ROOT/tools/param-pages/harness.mjs" ]]; then
  echo "SKIP: no Schwung checkout at $ROOT (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  MP_SHARED="$SHARED" SCHWUNG_ROOT="$ROOT" node "$MODULE/tests/test_grid.mjs"
else
  NODE=node.exe; command -v node.exe >/dev/null 2>&1 || NODE="/mnt/c/Program Files/nodejs/node.exe"
  MP_SHARED="${SHARED:+$(wslpath -w "$SHARED")}" SCHWUNG_ROOT="$(wslpath -w "$ROOT")" \
    WSLENV="MP_SHARED:SCHWUNG_ROOT" "$NODE" "$(wslpath -w "$MODULE/tests/test_grid.mjs")"
fi

echo "== Move build (arm64) + ABI"
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
  "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" -std=gnu11 -O3 -static -ffp-contract=off \
    "${INC[@]}" "$MODULE/tests/bench_device.c" \
    "$MODULE/dsp/mp_engine.c" "$MODULE/dsp/mp_dsp.c" "$MODULE/dsp/mp_convert.c" -lm -o "$MODULE/build/bench-device-arm64"
  echo "built build/bench-device-arm64 (run it on the Move, see README)"
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi

echo "== licences"
python3 "$MODULE/scripts/check_licenses.py"
echo "ALL TESTS PASSED"
