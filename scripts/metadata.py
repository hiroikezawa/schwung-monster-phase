#!/usr/bin/env python3
"""Embed module.json's contract and the AIRA patch in the DSP.

The runtime callbacks never read files, so chain_params, the version, the
patch's raw records and the routing table are compiled in. The build fails if
module.json and dsp/mp_params.h disagree on any key, order, range or default,
if the patch file is not the expected one, or if the routing names a port the
patch's modules do not have.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import hashlib
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
meta = json.loads((root / "module.json").read_text())
cp = meta["capabilities"]["chain_params"]
params_h = (root / "dsp" / "mp_params.h").read_text()
rows = re.findall(r'\{"(\w+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*([01])\}', params_h)
assert rows, "could not parse MP_PARAMS in mp_params.h"
assert [p["key"] for p in cp] == [r[0] for r in rows], "chain_params order must match mp_params.h"
for p, (key, lo, hi, de, integer) in zip(cp, rows):
    lo, hi, de = float(lo), float(hi), float(de)
    if p["type"] == "enum":
        assert len(p["options"]) == hi - lo + 1 and lo == 0, key
        assert p["options"].index(p["default"]) == de, key
        assert integer == "1", key
    else:
        assert (p["min"], p["max"], p["default"]) == (lo, hi, de), key
        assert integer == "0", key
assert meta["dsp"] == meta["id"] + ".so", "the chain host loads audio_fx/<id>/<id>.so"
assert cp[0]["key"] == "mix", "knob 1 is MIX"
version = meta["version"]


def literal(value):
    return json.dumps(json.dumps(value, separators=(",", ":")), ensure_ascii=True)


out = pathlib.Path(sys.argv[1]) / "mp_metadata.h"
out.write_text("\n".join([
    "/* Generated at build time from module.json; edit that instead. */",
    "static const char kChainParams[] = " + literal(cp) + ";",
    f'static const char kVersion[] = "{version}";',
]) + "\n")

# The patch and its routing -> mp_patch_data.h (tools/aira_patch.py).
sys.path.insert(0, str(root / "tools"))
import aira_patch  # noqa: E402

PATCH_SHA256 = "f27c95ff684e9e155f7c71ea75f53fe5d62701623b0ee663f847b30584d23843"
data = (root / "patch" / "BITRAZER_MONSTER_PHASE.bin").read_bytes()
assert hashlib.sha256(data).hexdigest() == PATCH_SHA256, "patch/BITRAZER_MONSTER_PHASE.bin is not the original"
routing = json.loads((root / "patch" / "routing_screenshot.json").read_text())
(pathlib.Path(sys.argv[1]) / "mp_patch_data.h").write_text(aira_patch.header(data, routing))
