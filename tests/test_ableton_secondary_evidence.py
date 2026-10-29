"""File-only fixtures for the rollback gate; no real profiles or Live processes."""
import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ableton_secondary_evidence as gate


class SecondaryEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.parent = Path(__file__).resolve().parents[1] / "work/secondary-evidence-fixtures"
        self.parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="metadata-", dir=self.parent)
        self.root = Path(self.temporary.name).resolve()
        self.qa = self.root / "work/Ableton-QA-v0.3.0"
        self.secondary = self.qa / "secondary-profile"
        self.restore = self.secondary / "Restore-snapshots/20261028T123456789Z-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        self.restore.mkdir(parents=True)
        (self.root / "tools").mkdir()
        self.script = self.root / "tools/prepare_ableton_secondary_qa_v030.ps1"
        self.script.write_text("# isolated fixture helper; not executable QA evidence\n", encoding="utf-8")
        at = datetime(2026, 10, 28, 12, 0, 0, tzinfo=timezone.utc)
        delta = at - datetime(1, 1, 1, tzinfo=timezone.utc)
        ticks = (delta.days * 86400 + delta.seconds) * 10_000_000
        identity = {"id": 26920, "processName": "Ableton Live 12 Suite",
                    "startTimeUtc": "2026-10-28T12:00:00.0000000Z", "startTimeUtcTicks": str(ticks)}
        self.primary = {"version": "0.3.0", "baselineComplete": True, "projectRoot": str(self.root),
                        "abletonInitiallyRunning": True, "allowRunningLiveBaseline": True,
                        "initialLiveProcesses": [identity]}
        primary_raw = self.encode(self.primary)
        primary_hash = gate.sha(primary_raw)
        (self.qa / "baseline.json").write_bytes(primary_raw)
        digest_raw = (primary_hash + "\n").encode()
        (self.qa / "baseline.json.sha256").write_bytes(digest_raw)
        common = {"version": "0.3.0", "schema": 1, "fixtureOnly": False, "eligibleForReleaseEvidence": True,
                  "qaValidationPerformed": False, "profileVersion": "12.3b10",
                  "preferencesRoot": r"C:\Does-Not-Exist-Unit-Fixture\Ableton\Live 12.3b10\Preferences",
                  "primaryBaseline": str(self.qa / "baseline.json"), "primaryBaselineSHA256": primary_hash,
                  "initialLiveProcesses": [identity], "scriptSHA256": gate.sha(self.script.read_bytes())}
        self.metadata = {**copy.deepcopy(common), "secondaryBaselineComplete": True, "projectRoot": str(self.root),
                         "primaryBaselineDigestSHA256": gate.sha(digest_raw), "settings": []}
        self.report = {**copy.deepcopy(common), "restoreComplete": True, "secondaryPreferencesRestoredExactly": True,
                       "primaryBaselineUnchanged": True, "betaClosed": True, "initialProcessStatePreserved": True,
                       "noAdditionalLiveProcesses": True, "generatedFilesBackup": str(self.restore), "settings": []}
        for i, name in enumerate(sorted(gate.SETTINGS)):
            exists = name != "Options.txt"
            checksum = hashlib.sha256(name.encode()).hexdigest() if exists else None
            self.metadata["settings"].append({"name": name, "exists": exists, "sha256": checksum,
                "length": 20 + i if exists else None, "attributes": 32 if exists else None,
                "creationTimeUtc": "2026-10-28T10:00:00.0000000Z" if exists else None,
                "lastWriteTimeUtc": "2026-10-28T11:00:00.0000000Z" if exists else None})
            self.report["settings"].append({"name": name, "initiallyExisted": exists,
                                          "restoredExactly": True, "metadataRestored": True, "sha256": checksum})
        self.module = {"processName": "Ableton Live 12 Beta", "baselineSHA256": primary_hash}
        self.post = {"baselineSHA256": primary_hash}
        self.save()

    @staticmethod
    def encode(value):
        return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode()

    def save(self):
        raw = self.encode(self.metadata)
        (self.secondary / "baseline.json").write_bytes(raw)
        (self.secondary / "baseline.json.sha256").write_text(gate.sha(raw) + "\n", encoding="utf-8")
        self.report["secondaryBaselineSHA256"] = gate.sha(raw)
        raw = self.encode(self.report)
        (self.restore / "restore-report.json").write_bytes(raw)
        (self.qa / "SECONDARY-PROFILE-RESTORE.json").write_bytes(raw)

    def validate(self):
        return gate.validate(self.root, self.module, self.post)

    def tearDown(self):
        self.assertTrue(self.root.is_relative_to(self.parent.resolve()))
        self.temporary.cleanup()

    def test_beta_public_exact_copy_is_rollback_only(self):
        result = self.validate()
        self.assertEqual(result["path"], "work/Ableton-QA-v0.3.0/SECONDARY-PROFILE-RESTORE.json")
        self.assertEqual(result["sha256"], gate.sha((self.restore / "restore-report.json").read_bytes()))
        self.assertIs(result["scopeRollbackOnly"], True)
        self.assertIs(result["qaValidationPerformed"], False)
        self.assertEqual(result["profileVersion"], "12.3b10")

    def test_stable_without_report_returns_none(self):
        self.module["processName"] = "Ableton Live 12 Suite"
        (self.qa / "SECONDARY-PROFILE-RESTORE.json").unlink()
        self.assertIsNone(self.validate())

    def test_beta_missing_report_is_rejected(self):
        (self.qa / "SECONDARY-PROFILE-RESTORE.json").unlink()
        with self.assertRaises(RuntimeError): self.validate()

    def test_present_report_is_checked_even_for_stable(self):
        self.module["processName"] = "Ableton Live 12 Suite"
        self.report["betaClosed"] = False
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_fixture_and_qa_validation_claims_are_rejected(self):
        for field, value in (("fixtureOnly", True), ("eligibleForReleaseEvidence", False),
                             ("qaValidationPerformed", True), ("schema", True), ("version", "0.2.0")):
            with self.subTest(field=field):
                old = self.report[field]
                self.report[field] = value; self.save()
                with self.assertRaises(RuntimeError): self.validate()
                self.report[field] = old
        self.save()

    def test_all_restore_flags_require_boolean_true(self):
        for field in ("restoreComplete", "secondaryPreferencesRestoredExactly", "primaryBaselineUnchanged",
                      "betaClosed", "initialProcessStatePreserved", "noAdditionalLiveProcesses"):
            with self.subTest(field=field):
                self.report[field] = 1; self.save()
                with self.assertRaises(RuntimeError): self.validate()
                self.report[field] = True
        self.save()

    def test_script_changed_since_restore_is_rejected(self):
        self.script.write_text("changed", encoding="utf-8")
        with self.assertRaises(RuntimeError): self.validate()

    def test_primary_sha_linkage_is_required(self):
        self.post["baselineSHA256"] = "0" * 64
        with self.assertRaises(RuntimeError): self.validate()

    def test_secondary_baseline_digest_is_required(self):
        (self.secondary / "baseline.json.sha256").write_text("0" * 64, encoding="utf-8")
        with self.assertRaises(RuntimeError): self.validate()

    def test_secondary_primary_digest_linkage_is_required(self):
        self.metadata["primaryBaselineDigestSHA256"] = "0" * 64
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_original_live_identity_must_match_primary(self):
        self.report["initialLiveProcesses"][0]["id"] = 26921
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_original_start_ticks_cannot_disagree_with_time(self):
        self.metadata["initialLiveProcesses"][0]["startTimeUtcTicks"] = "123456789012345678"
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_setting_names_are_exactly_four_unique_names(self):
        self.report["settings"][0]["name"] = self.report["settings"][1]["name"]
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_setting_hash_and_metadata_restore_are_required(self):
        for field, value in (("sha256", "0" * 64), ("metadataRestored", False), ("restoredExactly", False)):
            with self.subTest(field=field):
                i = next(i for i, row in enumerate(self.report["settings"]) if row["initiallyExisted"])
                old = self.report["settings"][i][field]
                self.report["settings"][i][field] = value; self.save()
                with self.assertRaises(RuntimeError): self.validate()
                self.report["settings"][i][field] = old
        self.save()

    def test_wrong_profile_and_profile_root_are_rejected(self):
        self.report["preferencesRoot"] = r"C:\Fake\Ableton\Live 12.4.7\Preferences"
        self.save()
        with self.assertRaises(RuntimeError): self.validate()

    def test_public_json_must_be_byte_exact_helper_copy(self):
        with (self.qa / "SECONDARY-PROFILE-RESTORE.json").open("ab") as stream: stream.write(b"\n")
        with self.assertRaises(RuntimeError): self.validate()

    def test_private_restore_report_cannot_escape_secondary_boundary(self):
        self.report["generatedFilesBackup"] = str(self.root / "outside")
        self.save()
        with self.assertRaises(RuntimeError): self.validate()


if __name__ == "__main__":
    unittest.main(verbosity=2)
