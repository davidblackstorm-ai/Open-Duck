"""Negative-input tests for the audio evidence gate; no DSP/DAW is launched."""
import copy
import csv
import importlib.util
import json
from pathlib import Path
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify_quality_audio", ROOT / "tools/verify_quality_audio.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class CsvGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = ROOT / "quality-audit" / ("csv-gate-tests-" + uuid.uuid4().hex)
        cls.directory.mkdir(parents=True)

    def rows(self):
        return [dict(case=case, input=kind, mode=mode, latency=11274, rmsGainDb=0, peakGainDb=0,
                     nullRms=0, unityLateNullRms=0, unityLateGainDb=0, earlyGainDb=0,
                     recoveryGainDb=0, nonfinite=0, clipped=0) for case, kind, mode in gate.EXPECTED_CASES]

    def parse(self, rows):
        path = self.directory / (self.id().split(".")[-1] + ".csv")
        with path.open("w", newline="", encoding="utf8") as stream:
            writer = csv.DictWriter(stream, fieldnames=gate.FIELDS)
            writer.writeheader(); writer.writerows(rows)
        return gate.parse_csv(path)

    def test_exact_inventory(self):
        self.assertEqual(len(self.parse(self.rows())), 35)

    def test_missing_case_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "inventory"):
            self.parse(self.rows()[:-1])

    def test_duplicate_case_rejected(self):
        rows = self.rows(); rows[-1] = copy.deepcopy(rows[0])
        with self.assertRaisesRegex(RuntimeError, "inventory"):
            self.parse(rows)

    def test_extra_case_rejected(self):
        rows = self.rows(); rows.append(copy.deepcopy(rows[0]))
        with self.assertRaisesRegex(RuntimeError, "exactly 35"):
            self.parse(rows)

    def test_nonfinite_number_rejected(self):
        rows = self.rows(); rows[5]["rmsGainDb"] = "nan"
        with self.assertRaisesRegex(RuntimeError, "Nonfinite"):
            self.parse(rows)

    def test_counter_rejected(self):
        rows = self.rows(); rows[8]["nonfinite"] = 1
        with self.assertRaisesRegex(RuntimeError, "nonfinite or clipped"):
            self.parse(rows)

    def test_clipped_counter_rejected(self):
        rows = self.rows(); rows[8]["clipped"] = 1
        with self.assertRaisesRegex(RuntimeError, "nonfinite or clipped"):
            self.parse(rows)

    def test_noninteger_counter_rejected(self):
        rows = self.rows(); rows[8]["clipped"] = "0.0"
        with self.assertRaisesRegex(RuntimeError, "Noninteger"):
            self.parse(rows)

    def test_wrong_latency_rejected(self):
        rows = self.rows(); rows[9]["latency"] = 11275
        with self.assertRaisesRegex(RuntimeError, "PDC"):
            self.parse(rows)

    def test_null_boundary_rejected(self):
        rows = self.rows(); rows[0]["nullRms"] = 1e-7
        with self.assertRaisesRegex(RuntimeError, "does not null"):
            self.parse(rows)

    def test_transient_late_window_is_checked(self):
        rows = self.rows()
        next(row for row in rows if row["case"] == "INIT" and row["mode"] == 5)["unityLateNullRms"] = 1e-6
        with self.assertRaisesRegex(RuntimeError, "open window"):
            self.parse(rows)

    def test_unknown_mode_rejected(self):
        rows = self.rows(); rows[5]["mode"] = 6
        with self.assertRaisesRegex(RuntimeError, "inventory"):
            self.parse(rows)


if __name__ == "__main__":
    unittest.main(verbosity=2)
