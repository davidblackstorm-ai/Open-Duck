"""Exercise release export selection against isolated files; never open a DAW."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import package_release as package


class ExportSelectionTests(unittest.TestCase):
    def setUp(self):
        self.parent = Path(__file__).resolve().parents[1] / "work/export-selection-fixtures"
        self.parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="qa-exports-", dir=self.parent)
        self.root = Path(self.temporary.name).resolve()
        self.qa = self.root / "work/Ableton-QA-v0.3.0"
        (self.qa / "Exports").mkdir(parents=True)
        self.dry, self.wet = (self.qa / "Exports" / name for name in ("bass-dry.wav", "bass-wet.wav"))
        for path, level in ((self.dry, 12000), (self.wet, 3000)):
            with wave.open(str(path), "wb") as stream:
                stream.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
                stream.writeframes(struct.pack("<hh", level, -level) * 32)
        self.report = {
            "schema": "openduck-ableton-export-comparison-v1", "passed": True,
            "checks": {"sameFormat": True, "audibleDifference": True},
            "dry": {"path": str(self.dry), "sha256": package.digest(self.dry)},
            "wet": {"path": str(self.wet), "sha256": package.digest(self.wet)},
        }
        self.name = "EXPORT-COMPARISON.json"
        self.save()

    def save(self, name=None, report=None):
        (self.qa / (name or self.name)).write_text(json.dumps(report or self.report), encoding="utf-8")

    def select(self, selection=None):
        return package.ableton_export_evidence(self.root, self.qa, selection or {})

    def tearDown(self):
        self.assertTrue(self.root.is_relative_to(self.parent.resolve()))
        self.temporary.cleanup()

    def test_default_report_selects_exact_files_and_hashes(self):
        reports, waves = self.select()
        self.assertEqual(reports, [{"relative": self.name, "sha256": package.digest(self.qa / self.name)}])
        self.assertEqual({r["relative"] for r in waves}, {"Exports/bass-dry.wav", "Exports/bass-wet.wav"})
        self.assertTrue(all(package.digest(self.qa / r["relative"]) == r["sha256"] for r in waves))

    def test_explicit_custom_report_and_reused_wavs_are_deduplicated(self):
        self.save("SECOND-COMPARISON.json")
        reports, waves = self.select({"exportReports": [self.name, "SECOND-COMPARISON.json", "SECOND-COMPARISON.json"]})
        self.assertEqual(len(reports), 2)
        self.assertEqual(len(waves), 2)

    def test_no_optional_report_is_acceptable(self):
        (self.qa / self.name).unlink()
        self.assertEqual(self.select(), ([], []))

    def test_explicit_missing_report_is_rejected(self):
        with self.assertRaises(RuntimeError):
            self.select({"exportReports": ["MISSING.json"]})

    def test_report_traversal_is_rejected(self):
        with self.assertRaises(RuntimeError):
            self.select({"exportReports": ["../EXPORT-COMPARISON.json"]})

    def test_wav_traversal_is_rejected_even_when_resolves_inside(self):
        self.report["wet"]["path"] = str(self.qa / "Exports/../Exports/bass-wet.wav")
        self.save()
        with self.assertRaises(RuntimeError): self.select()

    def test_wav_outside_named_qa_directory_is_rejected(self):
        outside = self.root / "private.wav"
        outside.write_bytes(self.wet.read_bytes())
        self.report["wet"] = {"path": str(outside), "sha256": package.digest(outside)}
        self.save()
        with self.assertRaises(RuntimeError): self.select()

    def test_private_backup_is_rejected(self):
        private = self.qa / "preferences-before" / "audio.wav"
        private.parent.mkdir()
        private.write_bytes(self.wet.read_bytes())
        self.report["wet"] = {"path": str(private), "sha256": package.digest(private)}
        self.save()
        with self.assertRaises(RuntimeError): self.select()

    def test_changed_wav_hash_is_rejected(self):
        with self.wet.open("ab") as stream: stream.write(b"changed")
        with self.assertRaises(RuntimeError): self.select()

    def test_unresolved_check_and_nonboolean_check_are_rejected(self):
        for invalid in (False, 1, "true"):
            self.report["checks"]["audibleDifference"] = invalid
            self.save()
            with self.assertRaises(RuntimeError): self.select()

    def test_wrong_schema_and_non_wav_are_rejected(self):
        wrong = copy.deepcopy(self.report)
        wrong["schema"] = "other"
        self.save(report=wrong)
        with self.assertRaises(RuntimeError): self.select()
        renamed = self.qa / "Exports/not-a-wave.txt"
        renamed.write_bytes(self.wet.read_bytes())
        self.report["wet"] = {"path": str(renamed), "sha256": package.digest(renamed)}
        self.save()
        with self.assertRaises(RuntimeError): self.select()

    def test_alternate_stream_is_rejected(self):
        self.report["wet"]["path"] = str(self.wet) + ":hidden.wav"
        self.save()
        with self.assertRaises(RuntimeError): self.select()

    def test_project_relative_reference_matches_report_generator(self):
        self.report["wet"]["path"] = self.wet.relative_to(self.root).as_posix()
        self.save()
        self.assertEqual(len(self.select()[1]), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
