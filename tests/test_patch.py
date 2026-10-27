#!/usr/bin/env python3
"""Tests for tools/aira_patch.py: the patch file is parsed as documented, the
diff finds exactly what changed, and the routing table names real ports.

Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
"""
import json
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import aira_patch as ap  # noqa: E402

BIN = (ROOT / "patch" / "BITRAZER_MONSTER_PHASE.bin").read_bytes()
ROUTING = json.loads((ROOT / "patch" / "routing_screenshot.json").read_text())


class Parse(unittest.TestCase):
    def setUp(self):
        self.p = ap.parse(BIN)

    def test_identity(self):
        self.assertEqual(self.p["size"], 1212)
        self.assertEqual(self.p["sha256"], "f27c95ff684e9e155f7c71ea75f53fe5d62701623b0ee663f847b30584d23843")
        self.assertEqual(self.p["magic"], "HabPatchFile0001")
        self.assertEqual(self.p["name"], "AiraEfx Patch")

    def test_modules(self):
        got = [(s["type_id"], s["type"], s["raw"]) for s in self.p["subs"]]
        self.assertEqual(got, [
            (16, "SHORT DELAY", [3, 61, 48, 0]),
            (16, "SHORT DELAY", [0, 61, 48, 0]),
            (28, "FORMANT FILTER", [0, 1, 0, 0]),
            (17, "TUBE CLIP", [27, 75, 0, 0]),
            (20, "3 BAND EQ", [50, 50, 50, 0]),
            (18, "COMPRESSOR", [0, 0, 10, 10]),
        ])
        self.assertTrue(all(s["reserved"] == [0, 0, 0] for s in self.p["subs"]))

    def test_parameter_names(self):
        sd = self.p["subs"][0]["params"]
        self.assertEqual(sd, {"TIME": 3, "FEEDBACK": 61, "DRY/WET": 48})
        self.assertEqual(self.p["subs"][5]["params"],
                         {"THRESHOLD": 0, "RATIO": 0, "ATTACK": 10, "RELEASE": 10})

    def test_main_record_hypotheses(self):
        h1 = self.p["main_h1"]
        self.assertEqual((h1["SAMPLE RATE LEVEL"], h1["FILTER CUTOFF LEVEL"], h1["BIT DOWN"]), (100, 100, 50))
        self.assertEqual(self.p["main_h2"]["SAMPLE RATE LEVEL"], 0)

    def test_cable_area(self):
        c = self.p["cable_area_nonzero"]
        self.assertEqual(len(c), 18)
        self.assertEqual((c[0]["offset"], c[0]["value"]), ("0x0082", 15))
        self.assertEqual((c[-1]["offset"], c[-1]["value"]), ("0x03DA", 12))
        self.assertEqual(self.p["tail_nonzero"], 0)
        self.assertEqual(self.p["gap_0x68_0x7F_nonzero"], 0)

    def test_rejects(self):
        with self.assertRaises(ValueError):
            ap.parse(BIN[:-1])
        with self.assertRaises(ValueError):
            ap.parse(b"X" + BIN[1:])


class Diff(unittest.TestCase):
    def test_identical(self):
        self.assertEqual(ap.diff(BIN, BIN), [])

    def test_one_cable_byte(self):
        b = bytearray(BIN)
        b[0x0200] = 7          # a byte in the cable area, as one changed cable might
        d = ap.diff(BIN, bytes(b))
        self.assertEqual(len(d), 1)
        self.assertEqual(d[0]["offset"], "0x0200")
        self.assertIn("cable area index 384", d[0]["field"])
        self.assertIn("row32 12, col32 0", d[0]["field"])

    def test_parameter_field(self):
        b = bytearray(BIN)
        b[0x39] = 50           # SHORT DELAY 1 TIME
        d = ap.diff(BIN, bytes(b))
        self.assertEqual([(x["field"], x["a"], x["b"]) for x in d], [("sub1[1] (param1)", 3, 50)])
        b = bytearray(BIN)
        b[0x2D] = 0            # main byte 5 = BIT DOWN under H1
        self.assertIn("BIT DOWN", ap.diff(BIN, bytes(b))[0]["field"])


class Routing(unittest.TestCase):
    def test_valid_against_patch(self):
        self.assertEqual(ap.validate_routing(ROUTING, ap.parse(BIN)), [])
        self.assertEqual(len(ROUTING["edges"]), 17)
        self.assertIn("APPROXIMATE", ROUTING["status"])
        self.assertTrue(ROUTING["id"].startswith("approximate_routing:"))

    def test_signal_kinds(self):
        p = ap.parse(BIN)
        for e in ROUTING["edges"]:
            src, dst = ap.edge_kind(e["src"], p), ap.edge_kind(e["dst"], p)
            # audio goes to audio inputs, GRF controls to control inputs
            self.assertEqual(src == "audio", dst == "audio", f"{e['src']} -> {e['dst']}")

    def test_invalid_ports_reported(self):
        bad = {"edges": [{"src": "SUB1.OUT2", "dst": "OUTPUT1"},       # SHORT DELAY has one output
                         {"src": "INPUT1", "dst": "SUB6.IN4"},         # COMPRESSOR has three inputs
                         {"src": "GRF9", "dst": "OUTPUT1"},
                         {"src": "INPUT1", "dst": "SUB1.IN1"}, {"src": "INPUT1", "dst": "SUB1.IN1"}]}
        problems = ap.validate_routing(bad, ap.parse(BIN))
        self.assertEqual(len(problems), 4, problems)

    def test_ids_match_public_sysex(self):
        self.assertEqual((ap.SRC["GRF1"], ap.SRC["MAIN.OUT1"], ap.SRC["SUB6.OUT2"]), (2, 8, 21))
        self.assertEqual((ap.DST["MAIN.IN1"], ap.DST["MAIN.MOD6"], ap.DST["SUB1.IN1"], ap.DST["SUB6.IN4"]),
                         (2, 9, 10, 33))

    def test_header(self):
        h = ap.header(BIN, ROUTING)
        self.assertIn("#define MP_EDGE_COUNT 17", h)
        self.assertIn("{16, 3, 61, 48, 0}", h)
        self.assertIn("{ 2,  6},   /* GRF1 -> MAIN.MOD3 */", h)


if __name__ == "__main__":
    unittest.main(verbosity=1)
