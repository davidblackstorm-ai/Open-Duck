"""Isolated synthetic validation; none of these fixtures are DAW evidence."""

from array import array
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).with_name("compare_ableton_exports_v030.py")
SPEC = importlib.util.spec_from_file_location("export_compare", TOOL)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def write_wave(path, samples, bits=24, floating=False, rate=8000, channels=2, extensible=False):
    if floating:
        payload = b"".join(struct.pack("<" + ("f" if bits == 32 else "d"), sample) for sample in samples)
    else:
        scale = 1 << (bits - 1)
        integers = (max(-scale, min(scale - 1, round(sample * scale))) for sample in samples)
        payload = b"".join(value.to_bytes(bits // 8, "little", signed=True) for value in integers)
    align = channels * (bits // 8)
    code = 3 if floating else 1
    fmt = struct.pack("<HHIIHH", 0xFFFE if extensible else code, channels, rate, rate * align, align, bits)
    if extensible:
        fmt += struct.pack("<HHI", 22, bits, 3 if channels == 2 else 0)
        fmt += struct.pack("<I", code) + bytes.fromhex("00001000800000aa00389b71")
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(payload)) + payload
    if len(payload) & 1:
        body += b"\0"
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


class ExportComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openduck-export-unit-", dir=TOOL.parent)
        self.folder = Path(self.temp.name).resolve()
        if not self.folder.is_relative_to(TOOL.parent.resolve()):
            raise RuntimeError("Synthetic fixture directory escaped its intended workspace")

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self):
        dry, wet = [], []
        for frame in range(8000 * 4):
            t = frame / 8000
            source = .3 * math.sin(2 * math.pi * 55 * t) + .1 * math.sin(2 * math.pi * 110 * t)
            phase = (t % .5) / .5
            gain = 0 if phase < .1 else (math.sin((phase - .1) / .5 * math.pi / 2) if phase < .6 else 1)
            dry.extend((source, source * .9))
            wet.extend((source * gain, source * .9 * gain))
        return dry, wet

    def run_tool(self, dry, wet, name="result.json", *extra):
        result = subprocess.run([sys.executable, str(TOOL), "--dry", str(dry), "--wet", str(wet),
                                 "--output", str(self.folder / name), *extra], capture_output=True, text=True)
        path = self.folder / name
        report = json.loads(path.read_text(encoding="utf-8")) if path.exists() else None
        return result, report

    def test_known_pump_passes_and_identical_audio_fails(self):
        dry, wet = self.fixture()
        dry_path, wet_path = self.folder / "dry.wav", self.folder / "known-pump.wav"
        write_wave(dry_path, dry)
        write_wave(wet_path, wet)
        before = (dry_path.read_bytes(), wet_path.read_bytes())
        result, report = self.run_tool(dry_path, wet_path, "pump.json", "--bpm", "120")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(report["passed"])
        self.assertEqual(report["metrics"]["completeCycles"], 8)
        self.assertAlmostEqual(report["metrics"]["medianPeriodMs"], 500)
        self.assertEqual((dry_path.read_bytes(), wet_path.read_bytes()), before)
        result, report = self.run_tool(dry_path, dry_path, "identical.json")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])
        self.assertFalse(report["checks"]["audibleDifferenceProxy"])
        self.assertEqual(report["metrics"]["differenceRms"], 0)

    def test_all_encodings_preserve_sample_values(self):
        samples = [-.75, .5, -.125, 0, .25, -.5]
        for bits, floating, extensible in ((16, False, False), (24, False, False), (32, False, True),
                                            (32, True, False), (64, True, True)):
            with self.subTest(bits=bits, floating=floating, extensible=extensible):
                path = self.folder / f"{bits}-{floating}-{extensible}.wav"
                write_wave(path, samples, bits=bits, floating=floating, extensible=extensible)
                meta, decoded = MODULE.read_wave(path)
                self.assertEqual(meta["frames"], 3)
                self.assertEqual(list(decoded), samples)

    def test_different_frame_rate_or_channel_counts_are_rejected(self):
        dry, wet = self.fixture()
        dry_path = self.folder / "dry.wav"
        write_wave(dry_path, dry)
        for field in ("frames", "sampleRate", "channels"):
            with self.subTest(field=field):
                wet_path = self.folder / f"wrong-{field}.wav"
                write_wave(wet_path, wet[:-2] if field == "frames" else wet[::2] if field == "channels" else wet,
                           rate=16000 if field == "sampleRate" else 8000,
                           channels=1 if field == "channels" else 2)
                result, report = self.run_tool(dry_path, wet_path, field + ".json")
                self.assertEqual(result.returncode, 2)
                self.assertFalse(report["passed"])
                self.assertIn(field, report["error"])

    def test_non_finite_float_rejected_and_existing_output_not_overwritten(self):
        dry, wet = self.fixture()
        dry_path, wet_path = self.folder / "dry.wav", self.folder / "bad.wav"
        write_wave(dry_path, dry)
        write_wave(wet_path, [float("nan"), 0], bits=32, floating=True)
        result, report = self.run_tool(dry_path, wet_path)
        self.assertEqual(result.returncode, 2)
        self.assertIn("non-finite", report["error"])
        original = (self.folder / "result.json").read_bytes()
        result, _ = self.run_tool(dry_path, wet_path)
        self.assertEqual(result.returncode, 2)
        self.assertEqual((self.folder / "result.json").read_bytes(), original)

    def test_no_alignment_can_turn_identical_export_into_a_pass(self):
        dry, _ = self.fixture()
        shifted = [0] * 160 + dry[:-160]
        dry_path, wet_path = self.folder / "dry.wav", self.folder / "shifted.wav"
        write_wave(dry_path, dry)
        write_wave(wet_path, shifted)
        result, report = self.run_tool(dry_path, wet_path)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(report["checks"]["audibleDifferenceProxy"])
        self.assertFalse(report["checks"]["enoughCompleteCycles"])
        self.assertIn("No alignment", report["limitations"][0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
