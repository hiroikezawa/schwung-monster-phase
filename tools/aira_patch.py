#!/usr/bin/env python3
"""AIRA Modular Customizer patch file (.bin, "HabPatchFile0001") analyser.

    aira_patch.py dump   PATCH.bin              everything that is known, as JSON
    aira_patch.py diff   A.bin B.bin            byte/field differences of two patches
    aira_patch.py routing ROUTING.json [PATCH]  validate a routing file against a patch
    aira_patch.py header PATCH.bin ROUTING.json OUT.h   C header for the DSP

What is CONFIRMED and what is not (see docs/ANALYSIS.md):

  0x00..0x0F  "HabPatchFile0001"                         confirmed (bytes)
  0x10..0x27  24-byte ASCII name, space padded           confirmed (bytes)
  0x28..0x37  main module record, 16 bytes               field meaning INFERRED
  0x38..0x67  six 8-byte sub-module records:
              [type, p1, p2, p3, p4, 0, 0, 0]            confirmed: the type bytes are
              the public SysEx sub-module type ids and the six types match the
              screenshot in order
  0x68..0x7F  zero in this file                          unknown
  0x80..0x47F 1024 bytes, 18 non-zero                    NOT DECODED (cables? geometry?)
  0x480..0x4BB zero in this file                         unknown

The cable area is reported raw plus a few index decompositions; nothing here
claims a decoding. `diff` exists so that a second patch with ONE cable changed
in the Customizer pins the format down: the bytes that move are the cable.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import hashlib
import json
import pathlib
import sys

MAGIC = b"HabPatchFile0001"
SIZE = 1212
NAME = slice(0x10, 0x28)
MAIN = slice(0x28, 0x38)
SUB0, SUB_LEN, SUBS = 0x38, 8, 6
CABLE = slice(0x80, 0x480)
TAIL = slice(0x480, SIZE)

# Public (unofficial) SysEx sub-module types, mugenkidou/AIRA_Modular_Effects v1.2:
# parameter names, input names, output names.
SUB_TYPES = {
    0: ("EMPTY", [], [], []),
    16: ("SHORT DELAY", ["TIME", "FEEDBACK", "DRY/WET"],
         ["AUDIO IN", "DELAY TIME IN", "FEEDBACK IN", "DRY/WET IN"], ["DELAY OUT"]),
    17: ("TUBE CLIP", ["GAIN", "OUTPUT LEVEL"],
         ["AUDIO IN1", "AUDIO IN2", "GAIN IN", "OUTPUT LEVEL IN"], ["CLIP OUT1", "CLIP OUT2"]),
    18: ("COMPRESSOR", ["THRESHOLD", "RATIO", "ATTACK", "RELEASE"],
         ["AUDIO IN1", "AUDIO IN2", "THRESHOLD IN"], ["COMPRESSION OUT1", "COMPRESSION OUT2"]),
    20: ("3 BAND EQ", ["HI", "MID", "LOW"],
         ["AUDIO IN", "HI IN", "MID IN", "LOW IN"], ["EQ OUT"]),
    28: ("FORMANT FILTER", ["FORMANT 1", "FORMANT 2", "BALANCE"],
         ["AUDIO IN", "FORMANT 1 IN", "FORMANT 2 IN", "BALANCE IN"], ["FILTER OUT"]),
}
# Which inputs carry audio; everything else is a control (CV) input.
AUDIO_INPUTS = {"AUDIO IN", "AUDIO IN1", "AUDIO IN2"}

MAIN_PARAMS = ["LPF/HPF", "BYPASS", "SAMPLE RATE", "FILTER CUTOFF", "BIT DOWN", "RESONANCE",
               "SAMPLE RATE LEVEL", "FILTER CUTOFF LEVEL", "BIT DOWN LEVEL", "RESONANCE LEVEL"]
# Main module destinations 2..9 are IN1, IN2, MOD IN 1..6 (public SysEx). Which
# parameter each MOD IN drives is NOT published; MOD k = parameter k is a GUESS.
MAIN_INPUTS = ["IN1", "IN2", "MOD1", "MOD2", "MOD3", "MOD4", "MOD5", "MOD6"]
MAIN_MOD_GUESS = {"MOD1": "LPF/HPF", "MOD2": "BYPASS", "MOD3": "SAMPLE RATE",
                  "MOD4": "FILTER CUTOFF", "MOD5": "BIT DOWN", "MOD6": "RESONANCE"}


def source_ids():
    """Patching source names -> SysEx ids (ss of 10 20 ss dd)."""
    ids = {"INPUT1": 0, "INPUT2": 1}
    ids.update({f"GRF{i}": 1 + i for i in range(1, 7)})
    ids.update({"MAIN.OUT1": 8, "MAIN.OUT2": 9})
    for s in range(1, 7):
        for o in (1, 2):
            ids[f"SUB{s}.OUT{o}"] = 10 + (s - 1) * 2 + (o - 1)
    return ids


def dest_ids():
    """Patching destination names -> SysEx ids (dd of 10 20 ss dd)."""
    ids = {"OUTPUT1": 0, "OUTPUT2": 1}
    ids.update({f"MAIN.{n}": 2 + i for i, n in enumerate(MAIN_INPUTS)})
    for s in range(1, 7):
        for i in range(1, 5):
            ids[f"SUB{s}.IN{i}"] = 10 + (s - 1) * 4 + (i - 1)
    return ids


SRC = source_ids()
DST = dest_ids()


def parse(data):
    if len(data) != SIZE:
        raise ValueError(f"expected {SIZE} bytes, got {len(data)}")
    if data[:16] != MAGIC:
        raise ValueError("not a HabPatchFile0001")
    main = list(data[MAIN])
    subs = []
    for s in range(SUBS):
        rec = data[SUB0 + s * SUB_LEN: SUB0 + (s + 1) * SUB_LEN]
        tid = rec[0]
        name, pnames, ins, outs = SUB_TYPES.get(tid, (f"TYPE {tid} (not modelled)", [], [], []))
        subs.append({
            "slot": s + 1, "offset": f"0x{SUB0 + s * SUB_LEN:04X}", "type_id": tid, "type": name,
            "raw": list(rec[1:5]), "reserved": list(rec[5:8]),
            "params": {pnames[i]: rec[1 + i] for i in range(len(pnames))},
            "inputs": ins, "outputs": outs,
        })
    cable = []
    for i, v in enumerate(data[CABLE]):
        if v:
            cable.append({"offset": f"0x{0x80 + i:04X}", "index": i, "value": v,
                          "row32": i // 32, "col32": i % 32, "row34": i // 34, "col34": i % 34})
    return {
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "magic": data[:16].decode("ascii"),
        "name": data[NAME].decode("ascii", "replace").rstrip(),
        "main_raw": main,
        # Two readings of the 16-byte main record. H1 (byte 0 = a header/type
        # byte, bytes 1..10 = PARAMETER 1..10) is the one used by the DSP:
        # under H2 the SAMPLE RATE LEVEL would be 0 and GRF1 -> SAMPLE RATE
        # (a cable in the screenshot) would do nothing, and the screenshot's
        # LEVEL knobs read near maximum. Neither reading is proven.
        "main_h1": {"byte0": main[0], **{MAIN_PARAMS[i]: main[1 + i] for i in range(10)}},
        "main_h2": {MAIN_PARAMS[i]: main[i] for i in range(10)},
        "main_trailing": main[11:],
        "subs": subs,
        "gap_0x68_0x7F_nonzero": sum(1 for b in data[0x68:0x80] if b),
        "cable_area_nonzero": cable,
        "tail_nonzero": sum(1 for b in data[TAIL] if b),
    }


def field_of(offset):
    if offset < 0x10:
        return "magic"
    if offset < 0x28:
        return f"name[{offset - 0x10}]"
    if offset < 0x38:
        i = offset - 0x28
        return f"main[{i}] (H1: {MAIN_PARAMS[i - 1] if 1 <= i <= 10 else 'byte0/trailing'})"
    if offset < 0x68:
        s, i = divmod(offset - SUB0, SUB_LEN)
        return f"sub{s + 1}[{i}] ({'type' if i == 0 else f'param{i}' if i <= 4 else 'reserved'})"
    if offset < 0x80:
        return "gap 0x68..0x7F"
    if offset < 0x480:
        i = offset - 0x80
        return f"cable area index {i} (row32 {i // 32}, col32 {i % 32}; row34 {i // 34}, col34 {i % 34})"
    return "tail 0x480.."


def diff(a, b):
    if len(a) != len(b):
        return [{"error": f"sizes differ: {len(a)} vs {len(b)}"}]
    return [{"offset": f"0x{i:04X}", "field": field_of(i), "a": x, "b": y}
            for i, (x, y) in enumerate(zip(a, b)) if x != y]


def port_kind(name):
    """'audio' / 'cv' / 'gate' for a source or destination name."""
    if name in ("INPUT1", "INPUT2", "OUTPUT1", "OUTPUT2") or name.startswith(("MAIN.OUT", "MAIN.IN")):
        return "audio"
    if name in ("GRF5", "GRF6") or name in ("MAIN.MOD1", "MAIN.MOD2"):
        return "gate"
    if name.startswith(("GRF", "MAIN.MOD")):
        return "cv"
    return None   # sub-module port: decided by the module type


def validate_routing(routing, patch=None):
    """Every edge names real ports of the loaded module types. Returns a list of
    problems (empty = valid)."""
    problems = []
    seen = set()
    for n, e in enumerate(routing["edges"]):
        src, dst = e["src"], e["dst"]
        if src not in SRC:
            problems.append(f"edge {n}: unknown source {src}")
            continue
        if dst not in DST:
            problems.append(f"edge {n}: unknown destination {dst}")
            continue
        if (src, dst) in seen:
            problems.append(f"edge {n}: duplicate {src} -> {dst}")
        seen.add((src, dst))
        if patch is None:
            continue
        for end, kind in ((src, "OUT"), (dst, "IN")):
            if not end.startswith("SUB"):
                continue
            slot, port = end[3], int(end.split(kind)[-1])
            sub = patch["subs"][int(slot) - 1]
            ports = sub["outputs"] if kind == "OUT" else sub["inputs"]
            if port > len(ports):
                problems.append(f"edge {n}: {end} does not exist on {sub['type']} (slot {slot})")
    return problems


def edge_kind(name, patch):
    k = port_kind(name)
    if k:
        return k
    slot, rest = int(name[3]), name.split(".")[1]
    sub = patch["subs"][slot - 1]
    if rest.startswith("OUT"):
        return "audio"
    port = sub["inputs"][int(rest[2:]) - 1]
    return "audio" if port in AUDIO_INPUTS else "cv"


def header(data, routing):
    p = parse(data)
    problems = validate_routing(routing, p)
    if problems:
        raise ValueError("; ".join(problems))
    lines = [
        "/* Generated by tools/aira_patch.py from patch/" + "BITRAZER_MONSTER_PHASE.bin and",
        " * patch/routing_screenshot.json. Do not edit. */",
        "#ifndef MP_PATCH_DATA_H",
        "#define MP_PATCH_DATA_H",
        "#include <stdint.h>",
        f'static const char MP_PATCH_SHA256[] = "{p["sha256"]}";',
        f'static const char MP_PATCH_NAME[] = "{p["name"]}";',
        f'static const char MP_ROUTING_ID[] = "{routing["id"]}";',
        "static const uint8_t MP_MAIN_RAW[16] = {" + ", ".join(map(str, p["main_raw"])) + "};",
        "static const uint8_t MP_SUB_RAW[6][5] = {",
    ]
    for s in p["subs"]:
        lines.append("    {" + ", ".join(map(str, [s["type_id"]] + s["raw"])) + "},")
    lines.append("};")
    lines.append("/* src/dst are the public SysEx patching ids (10 20 ss dd). */")
    lines.append("typedef struct { uint8_t src, dst; } mp_edge_def;")
    lines.append(f"#define MP_EDGE_COUNT {len(routing['edges'])}")
    lines.append("static const mp_edge_def MP_EDGES[MP_EDGE_COUNT] = {")
    for e in routing["edges"]:
        lines.append(f"    {{{SRC[e['src']]:2d}, {DST[e['dst']]:2d}}},   /* {e['src']} -> {e['dst']} */")
    lines += ["};", "#endif", ""]
    return "\n".join(lines)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "dump":
        print(json.dumps(parse(pathlib.Path(argv[2]).read_bytes()), indent=1))
    elif cmd == "diff":
        d = diff(pathlib.Path(argv[2]).read_bytes(), pathlib.Path(argv[3]).read_bytes())
        print(json.dumps(d, indent=1))
        print(f"{len(d)} byte(s) differ", file=sys.stderr)
    elif cmd == "routing":
        routing = json.loads(pathlib.Path(argv[2]).read_text())
        patch = parse(pathlib.Path(argv[3]).read_bytes()) if len(argv) > 3 else None
        problems = validate_routing(routing, patch)
        for e in routing["edges"]:
            kind = (edge_kind(e["src"], patch), edge_kind(e["dst"], patch)) if patch else ("?", "?")
            print(f"{e['src']:>10} -> {e['dst']:<10} {kind[0]:>5} -> {kind[1]:<5} {e.get('confidence', '')}")
        print("\n".join(problems) or f"OK: {len(routing['edges'])} edges valid")
        return 1 if problems else 0
    elif cmd == "header":
        out = header(pathlib.Path(argv[2]).read_bytes(), json.loads(pathlib.Path(argv[3]).read_text()))
        pathlib.Path(argv[4]).write_text(out)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
