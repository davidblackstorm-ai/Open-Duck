"""Synthetic file-only release gates; never compiles/starts Setup or a DAW.

Every executable is dummy fixture data. Only the fake previous-setup pathname
has its SHA mocked to the immutable archive digest; all other hashes use the
real bytes. The unmocked authenticity test must refuse these fixtures.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
from unittest import mock
import uuid

DELIVERY = Path(__file__).resolve().parents[1]
WORKSPACE = DELIVERY.parents[1]
TOOLS = DELIVERY / "tools"
SPEC = importlib.util.spec_from_file_location("installer_release_evidence_under_test", TOOLS / "installer_release_evidence.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
REAL_SHA = GATE.sha
PREVIOUS_SHA = "9727107a4146d147c58676e55baaf3ab64d0741ecd107207210794aae2bb565e"
LEGACY_DLL_SHA = "4790d534b47ccf579a884a73d0884386f22cb146ab18fb7795ff523741a76eff"
PROOF = DELIVERY / "work/installer-release-fixtures" / ("installer-release-" + uuid.uuid4().hex)
PROOF.mkdir(parents=True)


def canonical(value) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()


def put(path: Path, value: bytes = b"SYNTHETIC FILE ONLY") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value)


def write_json(path: Path, value: dict) -> None:
    put(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


class Fixture:
    def __init__(self, name: str):
        self.root = PROOF / name
        self.root.mkdir()
        self.contents = self.root / GATE.BUNDLE
        for name in GATE.INPUTS:
            put(self.root / name, ("SYNTHETIC COMPILER INPUT " + name).encode())
        self.binary = self.contents / "x86_64-win/OpenDuck Pro X.vst3"
        self.resource = self.contents / "Resources/moduleinfo.json"
        put(self.binary, b"DUMMY DLL -- NOT EXECUTABLE")
        put(self.resource, b'{"fixtureOnly":true}')
        self.compiler = self.root / "fake-compiler/ISCC.exe"
        self.previous = self.root / "work/installer-qa-build-v0.2.0/OpenDuck-Installer-QA.exe"
        self.qa_setup = self.root / "work/installer-qa-build-v0.3.0/OpenDuck-Installer-QA.exe"
        self.setup = self.root / "releases/OpenDuck-Pro-X-0.3.0-Windows-x64-Setup.exe"
        for path in (self.compiler, self.previous, self.qa_setup, self.setup):
            put(path, b"DUMMY EXE -- NEVER EXECUTED: " + path.name.encode())
        for name in ("verify_installer.ps1", "installer_evidence_v030.ps1", "compile_installer_v030.ps1"):
            put(self.root / "tools" / name, ("# SYNTHETIC TOOL " + name).encode())
        self.target = self.root / "work/installer-qa-v0.2.0/OpenDuck Pro X.vst3"
        directories = ["Resources", "x86_64-win"]
        payload_files = [{"relative": path.relative_to(self.contents).as_posix(), "length": path.stat().st_size,
                          "sha256": REAL_SHA(path)} for path in (self.resource, self.binary)]
        files = [{"relative": rel, "length": (self.root / rel).stat().st_size, "sha256": REAL_SHA(self.root / rel)}
                 for rel in GATE.INPUTS]
        files.extend({"relative": GATE.BUNDLE + "/" + row["relative"], "length": row["length"], "sha256": row["sha256"]}
                     for row in payload_files)
        files.sort(key=lambda row: row["relative"].lower())
        inventory = {"files": files, "payloadDirectories": directories}
        input_sha = canonical(inventory)
        manifest = {"schema": "openduck-installer-inputs-v1", "version": "0.3.0", **inventory, "inputSHA256": input_sha}
        payload = {"directories": directories, "files": payload_files}
        production_snapshot = {
            "bundle": {"root": str(self.root / "fake-production/OpenDuck Pro X.vst3"), "exists": True,
                       "directories": ["Contents", "Contents/Resources", "Contents/x86_64-win"],
                       "files": [{"relative": "Contents/Resources/moduleinfo.json", "length": 10, "sha256": "1" * 64}]},
            "auxiliary": {"root": str(self.root / "fake-program-files/OpenDuck Pro X"), "exists": False,
                          "directories": [], "files": []},
            "uninstallRegistry": {"path": "HKLM:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A2C52811-2937-42FA-A186-0100469D28E5}_is1",
                "exists": True, "keys": [{"relative": "", "values": [
                    {"name": "DisplayName", "kind": "String", "data": "OpenDuck à — 🎛 ' <&>\nsecond line"},
                    {"name": "Binary", "kind": "Binary", "data": "AP8F"},
                    {"name": "Multi", "kind": "MultiString", "data": ["first", "second à"]},
                    {"name": "InstallLocation", "kind": "ExpandString", "data": "%ProgramFiles%\\OpenDuck Pro X"}]}]}}
        self.qa = {"evidenceSchema": "openduck-installer-qa-v2", "version": "0.3.0",
            **{flag: True for flag in GATE.QA_FLAGS}, "inputManifest": manifest, "inputSHA256": input_sha,
            "payloadManifest": payload, "payloadFileCount": 2, "upgradePayloadSHA256": canonical(payload),
            "reinstallPayloadSHA256": canonical(payload), "builtSHA256": REAL_SHA(self.binary),
            "productionSnapshotSchema": 1, "productionBefore": production_snapshot,
            "productionAfter": copy.deepcopy(production_snapshot), "productionBeforeSHA256": canonical(production_snapshot),
            "productionAfterSHA256": canonical(production_snapshot), "target": str(self.target),
            "legacyInstalledSHA256": LEGACY_DLL_SHA, "qaSetupRelative": self.qa_setup.relative_to(self.root).as_posix(),
            "setupSHA256": REAL_SHA(self.qa_setup), "previousSetupSHA256": PREVIOUS_SHA,
            "verificationToolsSHA256": {"tools/" + name: REAL_SHA(self.root / "tools" / name)
                                       for name in ("verify_installer.ps1", "installer_evidence_v030.ps1")},
            "compiler": {"path": str(self.compiler), "sha256": REAL_SHA(self.compiler),
                "arguments": ["/Qp", "/DQA", "/DQAUpgradeFrom020", "/FOpenDuck-Installer-QA",
                              "/O" + str(self.root / "work/installer-qa-build-v0.3.0"), str(self.root / "installer/OpenDuck.iss")]}}
        self.production = {"schema": "openduck-installer-production-v1", "version": "0.3.0", "complete": True,
            "compileOnly": True, "productionInstallPerformed": False, "inputManifest": copy.deepcopy(manifest),
            "inputSHA256": input_sha,
            "setup": {"relative": self.setup.relative_to(self.root).as_posix(), "sha256": REAL_SHA(self.setup),
                      "length": self.setup.stat().st_size, "productVersion": "0.3.0"},
            "compilationToolsSHA256": {"tools/" + name: REAL_SHA(self.root / "tools" / name)
                                      for name in ("compile_installer_v030.ps1", "installer_evidence_v030.ps1")},
            "compiler": {"path": str(self.compiler), "sha256": REAL_SHA(self.compiler),
                "arguments": ["/Qp", "/O" + str(self.root / "releases"), "/FOpenDuck-Pro-X-0.3.0-Windows-x64-Setup",
                              str(self.root / "installer/OpenDuck.iss")]}}
        self.qa_path = self.root / "build/installer-qa-results-v0.3.0.json"
        self.production_path = self.root / "build/installer-production-v0.3.0.json"
        self.save()
        write_json(self.root / "FIXTURE-DECLARATION.json", {"fixtureOnly": True, "eligibleForReleaseEvidence": False,
            "executablesAreDummyBytes": True, "installerOrDawLaunched": False, "realRegistryAccess": False,
            "mockedAuthenticityPathOnly": str(self.previous), "scope": "Synthetic unit test; cannot pass without the one-path SHA mock."})

    def save(self):
        write_json(self.qa_path, self.qa)
        write_json(self.production_path, self.production)

    def validate(self, authenticated_fixture: bool = True):
        if not authenticated_fixture:
            return GATE.validate(self.root)
        def fixture_sha(path):
            return PREVIOUS_SHA if Path(path).resolve() == self.previous.resolve() else REAL_SHA(path)
        with mock.patch.object(GATE, "sha", side_effect=fixture_sha):
            return GATE.validate(self.root)


class InstallerReleaseEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture(self.id().rsplit(".", 1)[-1])

    def rejects(self, pattern=None):
        self.fixture.save()
        with self.assertRaisesRegex(RuntimeError, pattern or ".+"):
            self.fixture.validate()

    def test_matching_current_inputs_and_two_compile_manifests_pass(self):
        result = self.fixture.validate()
        self.assertIs(result["passed"], True)
        self.assertEqual(result["payloadFileCount"], 2)
        self.assertEqual(result["inputSHA256"], self.fixture.qa["inputSHA256"])
        self.assertIs(result["productionInstallPerformed"], False)

    def test_dummy_archive_cannot_pass_without_authenticity_mock(self):
        with self.assertRaisesRegex(RuntimeError, "Authentic previous-version QA setup"):
            self.fixture.validate(authenticated_fixture=False)

    def test_installer_only_notes_edit_invalidates_stale_pass(self):
        put(self.fixture.root / "installer/RELEASE-NOTES.txt", b"NEW NOTES, SAME DLL")
        self.rejects("changed after QA/compile")

    def test_current_resource_edit_invalidates_stale_pass(self):
        put(self.fixture.resource, b"CORRUPTED MODULEINFO, SAME DLL")
        self.rejects("changed after QA/compile")

    def test_extra_current_payload_file_is_rejected(self):
        put(self.fixture.contents / "Resources/extra.json", b"EXTRA")
        self.rejects("changed after QA/compile")

    def test_extra_current_empty_directory_is_rejected(self):
        (self.fixture.contents / "Extra-empty").mkdir()
        self.rejects("changed after QA/compile")

    def test_reinstall_resource_proof_must_match_full_payload(self):
        self.fixture.qa["reinstallPayloadSHA256"] = "0" * 64
        self.rejects("Upgrade/reinstall")

    def test_upgrade_resource_proof_must_match_full_payload(self):
        self.fixture.qa["upgradePayloadSHA256"] = "0" * 64
        self.rejects("Upgrade/reinstall")

    def test_reinstall_flag_cannot_be_asserted_false(self):
        self.fixture.qa["reinstallPayloadMatches"] = False
        self.rejects("Complete current isolated installer QA")

    def test_changed_qa_setup_is_rejected(self):
        put(self.fixture.qa_setup, b"DIFFERENT QA SETUP")
        self.rejects("QA Setup changed")

    def test_changed_production_setup_is_rejected(self):
        put(self.fixture.setup, b"DIFFERENT PRODUCTION SETUP")
        self.rejects("Production Setup differs")

    def test_setup_product_version_must_match(self):
        self.fixture.production["setup"]["productVersion"] = "0.2.0"
        self.rejects("Production Setup differs")

    def test_setup_recorded_length_must_match(self):
        self.fixture.production["setup"]["length"] += 1
        self.rejects("Production Setup differs")

    def test_changed_verifier_tool_is_rejected(self):
        put(self.fixture.root / "tools/verify_installer.ps1", b"CHANGED VERIFIER")
        self.rejects("tools changed")

    def test_changed_compile_tool_is_rejected(self):
        put(self.fixture.root / "tools/compile_installer_v030.ps1", b"CHANGED COMPILE TOOL")
        self.rejects("tools changed")

    def test_changed_shared_evidence_tool_is_rejected(self):
        put(self.fixture.root / "tools/installer_evidence_v030.ps1", b"CHANGED COMMON HELPER")
        self.rejects("tools changed")

    def test_changed_compiler_file_is_rejected(self):
        put(self.fixture.compiler, b"DIFFERENT COMPILER")
        self.rejects("compiler changed")

    def test_qa_and_production_cannot_use_different_compilers(self):
        other = self.fixture.root / "fake-compiler/OtherISCC.exe"
        put(other, b"ANOTHER DUMMY COMPILER")
        self.fixture.production["compiler"].update(path=str(other), sha256=REAL_SHA(other))
        self.rejects("compiler differ")

    def test_missing_qa_upgrade_flag_is_rejected(self):
        self.fixture.qa["compiler"]["arguments"].remove("/DQAUpgradeFrom020")
        self.rejects("QA compiler arguments")

    def test_production_compiler_cannot_use_qa_define(self):
        self.fixture.production["compiler"]["arguments"].insert(1, "/DQA")
        self.rejects("Production compiler arguments")

    def test_qa_target_cannot_point_outside_isolated_upgrade_directory(self):
        self.fixture.qa["target"] = str(self.fixture.root / "wrong-target")
        self.rejects("Isolated QA target")

    def test_uninstall_must_remove_resources_too(self):
        put(self.fixture.target / "Contents/Resources/moduleinfo.json", b"LEFTOVER RESOURCE")
        self.rejects("still contains managed")

    def test_uninstall_must_remove_managed_documents_too(self):
        put(self.fixture.target / "Documentation/LICENSE.txt", b"LEFTOVER DOCUMENT")
        self.rejects("still contains managed")

    def test_genuine_legacy_installed_binary_is_required(self):
        self.fixture.qa["legacyInstalledSHA256"] = "0" * 64
        self.rejects("Authentic previous-version installed binary")

    def test_production_snapshot_content_change_is_rejected(self):
        self.fixture.qa["productionAfter"]["bundle"]["files"][0]["sha256"] = "2" * 64
        self.rejects("were not preserved")

    def test_equal_forged_rollback_snapshots_do_not_match_stale_digest(self):
        for field in ("productionBefore", "productionAfter"):
            self.fixture.qa[field]["bundle"]["files"][0]["sha256"] = "2" * 64
        self.rejects("were not preserved")

    def test_production_registry_value_kind_change_is_rejected(self):
        self.fixture.qa["productionAfter"]["uninstallRegistry"]["keys"][0]["values"][0]["kind"] = "ExpandString"
        self.rejects("were not preserved")

    def test_current_input_sha_cannot_be_replaced_in_both_records(self):
        self.fixture.qa["inputSHA256"] = self.fixture.production["inputSHA256"] = "0" * 64
        self.fixture.qa["inputManifest"]["inputSHA256"] = "0" * 64
        self.fixture.production["inputManifest"]["inputSHA256"] = "0" * 64
        self.rejects("changed after QA/compile")

    def test_production_must_record_compile_only(self):
        self.fixture.production["productionInstallPerformed"] = True
        self.rejects("Compile-only")

    def test_current_qa_json_is_mandatory_after_old_pass_archived(self):
        self.fixture.qa_path.unlink()
        with self.assertRaises(FileNotFoundError):
            self.fixture.validate()

    def test_powershell_canonical_unicode_binary_and_actual_commands(self):
        source_candidates = [TOOLS, WORKSPACE / "work/v030-native/tools"]
        actual_tools = next((p for p in source_candidates if (p / "installer_evidence_v030.ps1").is_file() and
                             (p / "compile_installer_v030.ps1").is_file()), None)
        self.assertIsNotNone(actual_tools, "Current PowerShell installer helpers required")
        pwsh = os.environ.get("ODX_PWSH") or shutil.which("pwsh") or r"C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe"
        self.assertTrue(Path(pwsh).is_file(), "PowerShell required for the actual cross-language contract test")
        script = self.fixture.root / "powershell-contract.ps1"
        script.write_text(r'''param([string]$Project,[string]$ToolRoot)
$ErrorActionPreference='Stop'
. (Join-Path $ToolRoot 'installer_evidence_v030.ps1')
function Current-Arguments([string]$SourceFile,[string]$VariableName) {
    $root=$Project
    $qaBuild=Join-Path $root 'work\installer-qa-build-v0.3.0'
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($SourceFile,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'Current helper syntax is invalid'}
    $nodes=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq ('$'+$VariableName)},$true))
    if($nodes.Count -ne 1){throw 'Expected a single current compiler arguments assignment'}
    # Evaluate ONLY this reviewed arguments-array assignment, never the script,
    # compiler invocation or installer code.
    Invoke-Expression $nodes[0].Extent.Text
    foreach($currentArgument in (Get-Variable -Name $VariableName).Value) { Write-Output $currentArgument }
}
$qa=[IO.File]::ReadAllText((Join-Path $Project 'build\installer-qa-results-v0.3.0.json'))|ConvertFrom-Json
[ordered]@{input=(Get-InstallerInputManifest $Project);
 snapshotSHA256=(Get-InstallerValueHash $qa.productionBefore);
 qaArguments=@(Current-Arguments (Join-Path $ToolRoot 'verify_installer.ps1') 'compilerArguments');
 productionArguments=@(Current-Arguments (Join-Path $ToolRoot 'compile_installer_v030.ps1') 'arguments')}|ConvertTo-Json -Depth 32
''', encoding="utf-8")
        result = subprocess.run([str(pwsh), "-NoProfile", "-NonInteractive", "-File", str(script),
                                 "-Project", str(self.fixture.root), "-ToolRoot", str(actual_tools)],
                                capture_output=True, text=True, encoding="utf-8", errors="strict", timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        observed = json.loads(result.stdout)
        self.assertEqual(observed["input"], self.fixture.qa["inputManifest"])
        self.assertEqual(observed["snapshotSHA256"], self.fixture.qa["productionBeforeSHA256"])
        self.assertEqual(observed["snapshotSHA256"], GATE.canonical_sha(self.fixture.qa["productionBefore"]))
        self.assertEqual(observed["qaArguments"], self.fixture.qa["compiler"]["arguments"])
        self.assertEqual(observed["productionArguments"], self.fixture.production["compiler"]["arguments"])
        write_json(self.fixture.root / "POWERSHELL-CONTRACT-PASS.json", {"fixtureOnly": True,
            "inputSHA256": observed["input"]["inputSHA256"], "snapshotSHA256": observed["snapshotSHA256"],
            "unicodeAndBinaryRegistryCanonicalMatch": True, "actualCompilerArgumentArraysMatch": True,
            "compilerOrInstallerExecuted": False})


if __name__ == "__main__":
    print(f"Synthetic fixture root: {PROOF}", flush=True)
    unittest.main(verbosity=2)
