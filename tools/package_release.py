"""Package the verified local 0.3.0 release; never install or publish it.

The source archive includes JUCE and its notices. Build tools, validators,
private DAW logs, Git internals and unrelated work folders are not bundled.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import zipfile
from quality_model_lineage import validate as validate_model_lineage
from verify_quality_audio import validate as validate_quality_audio
from ableton_secondary_evidence import validate as validate_secondary_profile
from steinberg_conformance_evidence import validate as validate_steinberg_variants
from installer_release_evidence import validate as validate_installer_provenance


VERSION = "0.3.0"
EXPECTED_TESTS = 76
SOURCE_DIRECTORIES = ("Source", "tests", "Assets", "docs", "installer", "third_party/JUCE")
SOURCE_ROOT_FILES = ("CMakeLists.txt", "README.md", "LICENSE.txt")
SOURCE_TOOLS = (
    "build.ps1", "verify_installer.ps1", "create_installer_assets.py",
    "compile_installer_v030.ps1", "installer_evidence_v030.ps1", "test_installer_evidence_v030.ps1", "installer_release_evidence.py",
    "dashboard_server.py", "start_dashboard.ps1", "package_release.py", "gather_ableton_qa.py",
    "audit_ableton_automation.py", "prepare_ableton_qa_v030.ps1", "gather_ableton_qa_v030.py",
    "audit_loaded_openduck_v030.ps1",
    "prepare_ableton_secondary_qa_v030.ps1", "test_prepare_ableton_secondary_qa_v030.py",
    "ableton_secondary_evidence.py", "steinberg_conformance_evidence.py", "SingleKickPreview.cpp",
    "compare_ableton_exports_v030.py",
    "test_compare_ableton_exports_v030.py", "write_verification_report_v030.py", "collect_assist_stereo_evidence.py",
    "verify_revision.py", "quality_model_lineage.py", "verify_quality_audio.py", "verify_assist_training_repro.py", "train_assist.py", "run_assist_teacher.py", "AssistTeacher.cpp", "AssistModelParity.cpp", "AssistStereoAudit.cpp",
)
DASHBOARD_FILES = ("index.html", "status.json", "v0.3.0/concept.html", "v0.3.0/concept.css", "v0.3.0/concept.js", "v0.3.0/assets/PremiumFrame.png", "v0.3.0/assets/kick-cycle.js", "v0.3.0/assets/wave-source.json", "v0.3.0/OpenDuck-Pro-X-concept-0.3.0.jpg", "v0.3.0/native-preview.png")
MANDATORY_LOGS = (
    f"tests-v{VERSION}.log", f"pluginval-v{VERSION}.log", f"steinberg-v{VERSION}.log",
    f"host-contract-v{VERSION}.log", f"installer-qa-results-v{VERSION}.json",
    f"dsp-v{VERSION}.log", f"ui-v{VERSION}.log", f"ai-v{VERSION}.log",
    f"bridge-v{VERSION}.log", f"parity-v{VERSION}.log", f"non-daw-v{VERSION}.json",
    f"ai-heldout-v{VERSION}.json", f"quality-v{VERSION}.log", f"audio-v{VERSION}.log",
    f"quality-audio-v{VERSION}.json", f"ai-quality-lineage-v{VERSION}.json",
    "training-reproducibility-v0.3.0-r2.json",
    "steinberg-extensive-global-v0.3.0.log", "steinberg-extensive-local-v0.3.0.log",
    "installer-production-v0.3.0.json",
)
OPTIONAL_LOGS = (
    f"configure-v{VERSION}.log", f"build-v{VERSION}.log", f"installer-build-v{VERSION}.log",
    f"tests-v{VERSION}-stderr.log", f"pluginval-v{VERSION}-stderr.log",
    f"qa-v{VERSION}-install-legacy.log", f"qa-v{VERSION}-upgrade.log",
    f"qa-v{VERSION}-reinstall.log", f"qa-v{VERSION}-uninstall.log",
    f"build-host-v{VERSION}.log", f"configure-host-v{VERSION}.log", f"dependencies-v{VERSION}.log",
    f"installer-verification-v{VERSION}.log",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def read_text(path: Path) -> str:
    require(path.is_file(), f"Required evidence is missing: {path}")
    # PowerShell 5.1 commonly writes UTF-16 output; newer tools use UTF-8.
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig", errors="replace")


def files_in(directory: Path, project: Path):
    require(directory.is_dir(), f"Required source directory is missing: {directory}")
    project_resolved = project.resolve()
    for current, directories, files in os.walk(directory, followlinks=False):
        directories[:] = sorted(
            name for name in directories
            if name not in {".git", "__pycache__"}
            and not (Path(current) / name).is_symlink()
            and not (hasattr(os.path, "isjunction") and os.path.isjunction(Path(current) / name))
        )
        for name in sorted(files):
            if name in {".git", ".DS_Store"} or name.endswith((".pyc", ".pyo")):
                continue
            path = Path(current) / name
            require(path.resolve().is_relative_to(project_resolved), f"Source link escapes the project: {path}")
            yield path


def source_files(root: Path) -> list[Path]:
    result = []
    for name in SOURCE_DIRECTORIES:
        result.extend(files_in(root / name, root))
    for name in SOURCE_ROOT_FILES:
        path = root / name
        require(path.is_file(), f"Required source file is missing: {path}")
        result.append(path)
    for name in SOURCE_TOOLS:
        path = root / "tools" / name
        require(path.is_file(), f"Required reproducible-build tool is missing: {path}")
        result.append(path)
    for name in DASHBOARD_FILES:
        path = root / "dashboard" / name
        require(path.is_file(), f"Required local dashboard asset is missing: {path}")
        result.append(path)
    for name in (".clang-format", "READMEAPI.txt"):
        if (root / name).is_file():
            result.append(root / name)
    return sorted(set(result), key=lambda path: path.relative_to(root).as_posix().lower())


def live_identity_keys(records):
    require(isinstance(records, list), "Live identities must be an explicit list")
    keys, pids = set(), set()
    for item in records:
        require(isinstance(item, dict) and type(item.get("id")) is int and item["id"] > 0,
                "Invalid recorded Live PID")
        require(isinstance(item.get("processName"), str) and item["processName"].startswith("Ableton Live") and
                isinstance(item.get("startTimeUtcTicks"), str) and re.fullmatch(r"\d{16,20}", item["startTimeUtcTicks"]) and
                isinstance(item.get("startTimeUtc"), str) and item["startTimeUtc"].endswith("Z"),
                "Invalid recorded Live start identity")
        require(item["id"] not in pids, "Duplicate recorded Live PID")
        pids.add(item["id"])
        stamp = re.fullmatch(r"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,7}))?Z", item["startTimeUtc"])
        require(stamp is not None, "Invalid Live UTC timestamp")
        elapsed = datetime.fromisoformat(stamp[1] + "+00:00") - datetime(1, 1, 1, tzinfo=timezone.utc)
        expected_ticks = (elapsed.days * 86400 + elapsed.seconds) * 10_000_000 + int((stamp[2] or "").ljust(7, "0"))
        require(str(expected_ticks) == item["startTimeUtcTicks"], "Live timestamp and tick identity disagree")
        keys.add((item["id"], item["startTimeUtcTicks"]))
    return keys


def validate_live_restoration(baseline, post):
    allow = baseline.get("allowRunningLiveBaseline", False)
    initial = live_identity_keys(baseline.get("initialLiveProcesses", []))
    require(type(allow) is bool and type(baseline.get("abletonInitiallyRunning")) is bool and
            baseline["abletonInitiallyRunning"] == bool(initial), "Baseline running process contract is invalid")
    require(not initial or (allow and baseline.get("processIdentitySchema") == 1), "Running baseline lacks explicit permission/identities")
    if allow:
        profile = Path(baseline["preferencesRoot"])
        require(profile.name.lower() == "preferences" and profile.parent.name.lower() == "live 12.4.15",
                "Running baseline identifies another Live preference profile")
    process = post.get("process", {})
    current = live_identity_keys(process.get("currentlyRunningLiveProcesses"))
    require(process.get("allowRunningLiveBaseline") is allow and
            process.get("initiallyRunning") is bool(initial) and process.get("currentlyRunning") is bool(current) and
            live_identity_keys(process.get("initialLiveProcesses")) == initial and current == initial,
            "Initial Live PID/start identities were not preserved exactly or a QA/additional process remains")
    require(post.get("checks", {}).get("abletonClosed") is (not current), "Factual Live-closed flag disagrees with recorded processes")
    require(process.get("additionalLiveProcesses") == [] and process.get("missingInitialLiveProcesses") == [],
            "Post-run contains additional or missing initial Live processes")
    return {pid for pid, _ in initial}


def ableton_export_evidence(root: Path, qa_root: Path, selection: dict) -> tuple[list[dict], list[dict]]:
    """Select only named comparison reports and their fingerprinted QA WAVs."""
    root, qa_root = root.resolve(), qa_root.resolve()
    forbidden = {"preferences-before", "crash", "crashes", "crashreports", "logs", "private-logs"}

    def checked_file(path: Path, suffix: str) -> Path:
        require(".." not in path.parts and ":" not in str(path)[2:], "Export evidence path has traversal or alternate streams")
        require(path.is_absolute() and re.match(r"^[A-Za-z]:[\\/]", str(path)) and path.suffix.lower() == suffix,
                f"Expected explicit local {suffix} export evidence")
        for part in (path, *path.parents):
            try:
                info = part.lstat()
            except FileNotFoundError:
                continue
            require(not stat.S_ISLNK(info.st_mode) and not (getattr(info, "st_file_attributes", 0) &
                    getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)), f"Export evidence reparse path forbidden: {part}")
        path = path.resolve()
        require(path.is_relative_to(qa_root) and path.is_file(), f"Export evidence missing or outside explicit QA directory: {path}")
        require(not any(part.lower() in forbidden for part in path.relative_to(qa_root).parts),
                f"Private preference/log/crash material is not export evidence: {path}")
        return path

    defaults = ["EXPORT-COMPARISON.json", "AUDIO-EXPORT-COMPARISON.json", "EXPORT-COMPARISON-v0.3.0.json"]
    extra = selection.get("exportReports", [])
    require(isinstance(extra, list) and all(isinstance(item, str) for item in extra), "Invalid exportReports selection")
    reports, waves = {}, {}
    for name in dict.fromkeys(defaults + extra):
        require(name and Path(name).name == name and not any(c in name for c in ("/", "\\", ":")) and
                name.lower().endswith(".json"), "Export report must be a named JSON directly inside QA root")
        candidate = qa_root / name
        if not candidate.exists() and name not in extra:
            continue
        report_path = checked_file(candidate, ".json")
        require(report_path.parent == qa_root and report_path.stat().st_size <= 16 * 1024 * 1024,
                "Export comparison must be a bounded JSON directly inside QA root")
        report = json.loads(read_text(report_path))
        require(isinstance(report, dict) and report.get("schema") == "openduck-ableton-export-comparison-v1" and
                report.get("passed") is True and isinstance(report.get("checks"), dict) and report["checks"] and
                all(value is True for value in report["checks"].values()), "Export comparison has unresolved checks or wrong schema")
        selected = []
        for label in ("dry", "wet"):
            record = report.get(label)
            require(isinstance(record, dict) and isinstance(record.get("path"), str) and record["path"] and
                    isinstance(record.get("sha256"), str) and re.fullmatch(r"[a-fA-F0-9]{64}", record["sha256"]),
                    f"Invalid {label} WAV reference/hash in export report")
            supplied = Path(record["path"])
            require(".." not in supplied.parts and ":" not in str(supplied)[2:], "WAV reference has traversal or alternate streams")
            path = checked_file(supplied if supplied.is_absolute() else root / supplied, ".wav")
            with path.open("rb") as stream:
                signature = stream.read(12)
            require(signature.startswith(b"RIFF") and signature[8:12] == b"WAVE", f"Referenced export is not a RIFF WAV: {path}")
            expected = record["sha256"].lower()
            require(digest(path) == expected, f"Referenced {label} export WAV changed: {path}")
            relative = path.relative_to(qa_root).as_posix()
            waves[relative.casefold()] = {"relative": relative, "sha256": expected}
            selected.append(path)
        require(selected[0] != selected[1], "Dry and wet export evidence must reference separate WAVs")
        relative = report_path.relative_to(qa_root).as_posix()
        reports[relative.casefold()] = {"relative": relative, "sha256": digest(report_path)}
    return list(reports.values()), list(waves.values())


def ableton_evidence(root: Path) -> dict:
    """Select only explicit QA artifacts; never walk/copy the preference backup."""
    qa_root = root / f"work/Ableton-QA-v{VERSION}"
    documentation = root / f"docs/Ableton-QA-v{VERSION}.md"
    post_run_path = qa_root / "POST-RUN-REPORT.json"
    selection_path = qa_root / "EVIDENCE-FILES.json"
    require(documentation.is_file(), "The actual Ableton QA report has not been recorded yet")
    post_run = json.loads(read_text(post_run_path))
    require(post_run.get("version") == VERSION and post_run.get("postRunChecksPassed") is True,
            "Ableton post-run restoration/state checks have not passed")
    for name in ("fourPreferencesRestoredExactly", "baselinePreferenceSnapshotsIntact",
                 "productionBundleUnchanged", "qaProcessClosed", "noAdditionalLiveProcesses",
                 "initialLiveProcessesPreserved", "initialProcessStateRestored",
                 "targetReleaseBinaryUnchanged", "schema3StateCaptured", "schema3StatesComplete"):
        require(post_run.get("checks", {}).get(name) is True, f"Ableton post-run check is unresolved: {name}")
    baseline_path = qa_root / "baseline.json"
    require(str(post_run.get("baselineSHA256", "")).lower() == digest(baseline_path),
            "Ableton restoration evidence belongs to a different baseline")
    initial_pids = validate_live_restoration(json.loads(read_text(baseline_path)), post_run)
    module_path = qa_root / "LOADED-MODULES.json"
    module = json.loads(read_text(module_path))
    require(module.get("version") == VERSION and module.get("proofPassed") is True and
            module.get("checks", {}).get("allLoadedOpenDuckModulesAreFinalProjectBinary") is True,
            "Ableton did not verify the actual final project module")
    require(module.get("expectedSHA256") == post_run.get("releaseBinary", {}).get("sha256") and
            module.get("baselineSHA256") == post_run.get("baselineSHA256"),
            "Loaded module evidence belongs to another binary or QA baseline")
    require(type(module.get("processId")) is int and module["processId"] > 0 and module["processId"] not in initial_pids,
            "Loaded module evidence must belong to the new QA instance, never the original open Live process")
    require(isinstance(module.get('hostProductVersion'), str) and re.match(r'^12\.', module['hostProductVersion']),
            'The actual Ableton12 host version must be recorded by the module audit')
    automation_path = qa_root / 'AUTOMATION-AUDIT.json'
    automation = json.loads(read_text(automation_path))
    state = post_run.get('savedStateEvidence', {})
    require(automation.get('setSHA256') == state.get('qaSetSHA256'),
            'Automation evidence identifies another saved QA set')
    require(automation.get('modernPluginCount', 0) >= 2 and
            automation.get('modernAutomatedLaneCount', 0) > 0 and
            automation.get('modernNonnegativeTimeEventCount', 0) > 0,
            'Saved modern instances and newly recorded automation are required')
    require(all(row.get('allRecordedBindingsMatchJuce') is True
                for row in automation.get('plugins', []) if row.get('modernMask') == 63),
            'Recorded automation has an unverified VST3 parameter binding')
    selection = json.loads(read_text(selection_path))
    export_reports, export_waves = ableton_export_evidence(root, qa_root, selection)
    screenshots = selection.get("screenshots")
    require(isinstance(screenshots, list) and screenshots and all(isinstance(item, str) for item in screenshots),
            "EVIDENCE-FILES.json must explicitly list the selected QA screenshots")
    require(len(screenshots) == len(set(screenshots)), "The QA screenshot selection contains duplicates")
    selected = []
    forbidden = {"preferences-before", "crash", "crashes", "crashreports", "logs", "private-logs"}
    for relative in screenshots:
        require("\\" not in relative, "Use forward-slash relative paths in the QA screenshot selection")
        relative_path = Path(relative)
        require(not relative_path.is_absolute() and ".." not in relative_path.parts,
                f"Invalid QA screenshot path: {relative}")
        require(not any(part.lower() in forbidden for part in relative_path.parts),
                f"Private preference/log/crash material cannot be packaged: {relative}")
        path = qa_root / relative_path
        require(path.resolve().is_relative_to(qa_root.resolve()) and path.is_file(),
                f"Selected QA screenshot is missing or escapes its QA directory: {relative}")
        require(path.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp"},
                f"Only explicitly selected image files are accepted as QA screenshots: {relative}")
        with path.open("rb") as stream:
            signature = stream.read(12)
        image_signature = (signature.startswith(b"\x89PNG\r\n\x1a\n") or signature.startswith(b"\xff\xd8") or
                           (signature.startswith(b"RIFF") and signature[8:12] == b"WEBP"))
        require(image_signature, f"Selected screenshot is not a recognized image file: {relative}")
        selected.append({"relative": relative_path.as_posix(), "sha256": digest(path)})
    secondary = validate_secondary_profile(root, module, post_run)
    state = post_run.get("savedStateEvidence", {})
    return {
        "documentation": documentation.relative_to(root).as_posix(),
        "documentationSHA256": digest(documentation),
        "postRunReport": post_run_path.relative_to(root).as_posix(),
        "postRunReportSHA256": digest(post_run_path),
        "selectionManifest": selection_path.relative_to(root).as_posix(),
        "selectionManifestSHA256": digest(selection_path),
        "postRunChecksPassed": True,
        "qaProcessClosed": True,
        "initialLiveProcessCount": len(initial_pids),
        "targetReleaseBinarySHA256": str(post_run.get("releaseBinary", {}).get("sha256", "")).lower(),
        "schema3ChunkCount": state.get("schema3ChunkCount", 0),
        "loadedModuleProof": module_path.relative_to(root).as_posix(),
        "loadedModuleProofSHA256": digest(module_path),
        "hostProductVersion": module['hostProductVersion'], "hostProcessName": module['processName'],
        "automationAudit": automation_path.relative_to(root).as_posix(),
        "automationAuditSHA256": digest(automation_path),
        "screenshots": selected,
        "exportReports": export_reports,
        "exportWaves": export_waves,
        "secondaryProfileRestoration": secondary,
        "scope": "Named QA reports, restoration/state evidence, selected screenshots and fingerprinted comparison WAVs only; no preference backups, private logs or crash reports",
    }


def gates(root: Path, pre_daw: bool = False) -> dict:
    build = root / "build"
    bundle = build / "OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3"
    binary = bundle / "Contents/x86_64-win/OpenDuck Pro X.vst3"
    setup = root / "releases" / f"OpenDuck-Pro-X-{VERSION}-Windows-x64-Setup.exe"
    require(binary.is_file(), f"Release VST3 binary is missing: {binary}")
    require(setup.is_file(), f"Production installer must be compiled before packaging: {setup}")
    binary_mtime = binary.stat().st_mtime
    for path in (build / name for name in MANDATORY_LOGS):
        require(path.is_file() and path.stat().st_size > 0, f"Final evidence is missing or empty: {path}")
        # Repeated training precedes relinking the runtime with those weights.
        # Its current corpus/script/header/golden hashes are checked by lineage.
        if path.name != 'training-reproducibility-v0.3.0-r2.json':
            require(path.stat().st_mtime >= binary_mtime, f"Evidence predates the current Release binary: {path}")
    tests = read_text(build / f"tests-v{VERSION}.log")
    matches = re.findall(r"RESULT\s+(\d+)\s+passed,\s+(\d+)\s+failed", tests)
    require(matches and matches[-1] == (str(EXPECTED_TESTS), "0"),
            f"Native suite must finish with {EXPECTED_TESTS} passed, 0 failed")
    require(not re.search(r"^FAIL\s", tests, re.MULTILINE), "Native suite contains a failed assertion")
    pluginval = read_text(build / f"pluginval-v{VERSION}.log")
    require(pluginval.rstrip().endswith("SUCCESS"), "pluginval did not finish with SUCCESS")
    require(re.search(r"Strictness level:\s*10\b", pluginval), "pluginval strictness 10 evidence is missing")
    require(re.search(r"OpenDuck.*?\bv0\.3\.0\b", pluginval), "pluginval did not identify OpenDuck version 0.3.0")
    steinberg = read_text(build / f"steinberg-v{VERSION}.log")
    require(re.search(r"Result:\s*47\s+tests passed,\s*0\s+tests failed", steinberg),
            "Steinberg validator must report 47 passed and 0 failed")
    host = read_text(build / f"host-contract-v{VERSION}.log")
    require(re.search(r"HOST CONTRACT RESULT\s+11\s+passed,\s*0\s+failed", host),
            "Explicit-path VST3 host contract must report 11 passed and 0 failed")
    require(not re.search(r"^FAIL\s", host, re.MULTILINE), "VST3 host contract contains a failed assertion")
    qa = json.loads(read_text(build / f"installer-qa-results-v{VERSION}.json"))
    require(qa.get("version") == VERSION, "Installer QA belongs to another release")
    for name in ("installPrevious", "upgradeFrom020", "reinstall", "uninstall", "legacyPresetPreserved",
                 "modernPresetPreserved", "binaryMatches", "allPayloadFilesMatch", "allManagedPayloadRemoved",
                 "productionUntouched"):
        require(qa.get(name) is True, f"Installer QA gate was not passed: {name}")
    binary_hash = digest(binary)
    require(str(qa.get("builtSHA256", "")).lower() == binary_hash,
            "Installer QA hash does not match the current Release binary")
    installer_provenance = validate_installer_provenance(root)
    qa_target = Path(str(qa.get("target", ""))).resolve()
    # Archived 0.2.0 setup's path restriction remains intact; genuine upgrade
    # QA reuses its exact project-only destination and preserves existing presets.
    expected_target = (root / "work/installer-qa-v0.2.0/OpenDuck Pro X.vst3").resolve()
    require(qa_target == expected_target, "Installer QA did not use the isolated project target")
    require(not (qa_target / "Contents/x86_64-win/OpenDuck Pro X.vst3").exists(),
            "Installer QA binary is still installed after its uninstall step")
    sources = list(files_in(root / "Source", root)) + list(files_in(root / "Assets", root))
    require(all(path.stat().st_mtime <= binary_mtime for path in sources),
            "Plugin sources or embedded assets changed after the Release binary was built")
    # Adding the independent host-contract target can touch CMake after the plugin
    # link without changing the plugin's inputs. Verify that configure and build
    # actually consumed the current build system rather than forcing a new link.
    cmake_mtime = (root / "CMakeLists.txt").stat().st_mtime
    for name in (f"configure-v{VERSION}.log", f"build-v{VERSION}.log"):
        path = build / name
        require(path.is_file() and path.stat().st_size > 0 and path.stat().st_mtime >= cmake_mtime,
                f"Host configure/build evidence predates the current CMake configuration: {path}")
    installer_inputs = list(files_in(root / "installer", root))
    require(all(path.stat().st_mtime <= setup.stat().st_mtime for path in installer_inputs),
            "Production installer predates a current installer input")
    require(setup.stat().st_mtime >= binary_mtime, "Production installer predates the Release binary")
    require(VERSION in read_text(root / "README.md"), "Project README has not been updated for 0.3.0")
    report = root / "docs" / f"VERIFICATION-v{VERSION}.md"
    report_text = read_text(report)
    require(str(EXPECTED_TESTS) in report_text, "Final verification report still has an old test count")
    for line in report_text.splitlines():
        if any(label in line for label in ("| Validator", "| pluginval", "| QA installer")):
            require(not re.search(r"da completare|pending", line, re.IGNORECASE),
                    f"Final verification report has an unfinished mandatory check: {line}")
    require((root / "docs/AGPL-3.0.txt").is_file(), "Full AGPL license text is missing")
    require((root / "third_party/JUCE/CMakeLists.txt").is_file(), "Corresponding JUCE source is missing")
    preview = build / f"visual-qa-v{VERSION}"
    for name in ("preview-default.png", "knobs-0.png", "knobs-1.png", "knobs-2.png", "QA-StateRoundtrip.odpx"):
        require((preview / name).is_file(), f"Final native preview evidence is missing: {name}")
    non_daw = json.loads(read_text(build / f"non-daw-v{VERSION}.json"))
    require(non_daw.get("version") == VERSION and non_daw.get("complete") is True,
            "Complete revision DSP/UI/AI/bridge/parity/host/validator evidence is missing")
    require(non_daw.get("releaseBinarySHA256") == binary_hash, "Non-DAW checks identify a different VST3")
    steinberg_variants = validate_steinberg_variants(root, non_daw)
    for name in ("legacy", "dsp", "quality", "audio", "ui", "ai", "bridge", "parity", "host", "steinberg", "pluginval"):
        row = non_daw.get("checks", {}).get(name, {})
        require(row.get("passed") is True and row.get("exitCode") == 0, f"Unresolved revision suite: {name}")
        require(digest(root / row["log"]) == row["logSHA256"], f"Changed suite log: {name}")
    for relative, expected in non_daw.get("sourceSHA256", {}).items():
        require(digest(root / relative) == expected, f"Source changed after verification: {relative}")
    heldout_relative = f'build/ai-heldout-v{VERSION}.json'
    require(non_daw.get('generatedEvidenceSHA256', {}).get(heldout_relative) == digest(root / heldout_relative),
            'Heldout results are not the output fingerprinted by the final parity run')
    training = json.loads(read_text(root / "docs/research-v0.3.0/training.json"))
    require(len(training.get("models", [])) == 6, "Six trained local models are required")
    require({row.get('mode') for row in training['models']} == set(range(6)) and
            len({row.get('model_sha256_float32') for row in training['models']}) == 6,
            'Trained models must identify six distinct sector weight sets')
    require(all(row.get('train_scenes') == 96 and row.get('validation_scenes') == 24 and
                row.get('test_scenes') == 24 for row in training['models']),
            'Training must preserve the declared grouped 96/24/24 split for every model')
    for relative in ('docs/research-v0.3.0/golden.csv', 'docs/research-v0.3.0/training.json',
                     'docs/research-v0.3.0/teacher-final/manifest.json'):
        require(non_daw.get('inputEvidenceSHA256', {}).get(relative) == digest(root / relative),
                f'Training/parity input changed after final verification: {relative}')
    require(training.get("header_sha256") == digest(root / "Source/NeuralModels.h"),
            "Trained model export differs from the recorded training run")
    require("trained=true" in read_text(root / "Source/NeuralModels.h"), "Model placeholder cannot ship")
    corpus_root = root / 'docs/research-v0.3.0/teacher-final'
    corpus_path = corpus_root / 'manifest.json'
    corpus = json.loads(read_text(corpus_path))
    require(corpus.get('passed') is True and corpus.get('teacher_unchanged') is True and
            corpus.get('sources_unchanged') is True, 'Frozen actual-DSP corpus did not complete intact')
    require(corpus.get('scenes_per_mode') == 144 and corpus.get('candidates_per_scene') == 8,
            'Unexpected corpus scene/candidate count')
    frozen = corpus_root / 'frozen-source'
    for relative, expected in corpus.get('source_sha256', {}).items():
        require(digest(frozen / relative) == expected, f'Archived teacher source mismatch: {relative}')
    core = ('Source/PluginProcessor.cpp', 'Source/PluginProcessor.h', 'Source/Engines.h',
            'Source/BehaviorLibrary.h', 'Source/AssistTrainingData.h')
    for relative in core:
        require(digest(root / relative) == corpus['source_sha256'][relative],
                f'DSP or scene generator changed after labels were rendered: {relative}')
    data_sources = {row['mode']: row for row in training.get('sources', [])}
    corpus_runs = {row['mode']: row for row in corpus.get('runs', [])}
    require(set(data_sources) == set(corpus_runs) == set(range(6)), 'Incomplete six-sector data lineage')
    for mode in range(6):
        csv_hash = digest(corpus_root / f'mode-{mode}.csv')
        require(data_sources[mode].get('sha256') == csv_hash == corpus_runs[mode].get('csv_sha256') and
                corpus_runs[mode].get('passed') is True and corpus_runs[mode].get('rows') == 144,
                f'Training CSV does not match the frozen teacher run: mode {mode}')
    stereo = validate_model_lineage(root)
    lineage_relative = f'build/ai-quality-lineage-v{VERSION}.json'
    require(json.loads(read_text(root / lineage_relative)) == stereo and
            non_daw.get('generatedEvidenceSHA256', {}).get(lineage_relative) == digest(root / lineage_relative),
            'Final R2 model lineage changed after the native verification run')
    quality = validate_quality_audio(root, build / f'quality-audio-v{VERSION}')
    quality_relative = f'build/quality-audio-v{VERSION}.json'
    require(json.loads(read_text(root / quality_relative)) == quality and
            non_daw.get('generatedEvidenceSHA256', {}).get(quality_relative) == digest(root / quality_relative),
            'Final 35-render musical quality evidence changed after verification')
    heldout = json.loads(read_text(build / f"ai-heldout-v{VERSION}.json"))
    require(heldout.get('modelVersion') == 'OpenDuck-native-teacher-0.3.0-r2', 'Heldout evidence belongs to another model revision')
    require(len(heldout.get("modes", [])) == 6, "Actual DSP heldout results are missing")
    for row in heldout["modes"]:
        require(row.get("heldoutScenes", 0) >= 12 and row.get('allAcceptedPreservationGuardsPassed') is True and
                row.get("acceptedObjectiveMean", float("inf")) <= row.get("baselineObjectiveMean", -1),
                "Heldout evaluation incomplete or accepted an unprotected/worsening result")
    ableton = {"status": "pending-final-DAW-test-authorized", "performed": False} if pre_daw else ableton_evidence(root)
    if not pre_daw:
        require(ableton["targetReleaseBinarySHA256"] == binary_hash,
                "Ableton QA does not identify the current final Release binary")
    return {
        "version": VERSION, "nativeTestsPassed": EXPECTED_TESTS, "nativeTestsFailed": 0,
        "steinbergPassed": 47, "steinbergFailed": 0, "steinbergExtendedVariants": steinberg_variants, "pluginvalStrictness": 10,
        "hostContractPassed": 11, "hostContractFailed": 0,
        "pluginvalResult": "SUCCESS", "installerQA": qa, "releaseBinarySHA256": binary_hash,
        "productionSetupSHA256": digest(setup), "installerProvenance": installer_provenance, "productionInstallPerformed": False,
        "abletonEvidence": ableton, "nonDAW": non_daw, "trainedModelHeaderSHA256": training["header_sha256"],
        "teacherManifestSHA256": digest(corpus_path), "qualityModelLineage": stereo, "qualityAudio": quality,
    }


def safe_output(path: Path, releases: Path) -> Path:
    require(path.resolve().is_relative_to(releases.resolve()), f"Output escaped releases: {path}")
    relative = path.resolve().relative_to(releases.resolve())
    old_version = relative.parts[0] in {"0.1.0", "0.2.0"}
    old_root_artifact = len(relative.parts) == 1 and re.match(
        r"(?:OpenDuck-Pro-X-|SHA256-v)(?:0\.1\.0|0\.2\.0)(?:[-.]|$)", relative.name)
    require(not old_version and not old_root_artifact, f"Refusing to overwrite a previous release artifact: {path}")
    return path


def write_zip(destination: Path, entries: list[tuple[Path, str]], releases: Path) -> None:
    safe_output(destination, releases)
    temporary = safe_output(destination.with_name(destination.name + ".part"), releases)
    names = [name for _, name in entries]
    require(len(names) == len(set(names)), f"Duplicate archive members: {destination.name}")
    require(all(not name.startswith("/") and ".." not in Path(name).parts for name in names), "Unsafe ZIP member name")
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as archive:
            for source, name in entries:
                archive.write(source, name)
        with zipfile.ZipFile(temporary, "r") as archive:
            require(archive.testzip() is None, f"ZIP integrity check failed: {destination.name}")
            require(archive.namelist() == names, f"ZIP member verification failed: {destination.name}")
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def package(root: Path, dry_run: bool = False, pre_daw: bool = False) -> dict:
    require(not pre_daw or dry_run, "Pre-DAW review is validation only; final distribution remains gated")
    evidence = gates(root, pre_daw)
    sources = source_files(root)
    if dry_run:
        return {"gates": evidence, "sourceFileCount": len(sources), "action": "validated only; no files written"}
    releases = root / "releases"
    version_dir = safe_output(releases / VERSION, releases)
    require(not version_dir.exists(), 'Current version output already exists; archive that attempt before packaging anew')
    version_dir.mkdir(parents=True, exist_ok=False)
    prefix = f"OpenDuck-Pro-X-{VERSION}"
    source_zip = safe_output(releases / f"{prefix}-Sorgenti.zip", releases)
    vst_zip = safe_output(releases / f"{prefix}-VST3.zip", releases)
    write_zip(source_zip, [(path, f"{prefix}/" + path.relative_to(root).as_posix()) for path in sources], releases)
    bundle = root / "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3"
    payload = list(files_in(bundle, root))
    require(payload, "VST3 bundle is empty")
    write_zip(vst_zip, [(path, "OpenDuck Pro X.vst3/" + path.relative_to(bundle).as_posix()) for path in payload], releases)

    def copy(source: Path, relative: str) -> None:
        destination = safe_output(version_dir / relative, releases)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    copy(root / "README.md", "README.md")
    report_text = read_text(root / "docs" / f"VERIFICATION-v{VERSION}.md")
    report_text = report_text.replace(f"../build/visual-qa-v{VERSION}/", "Preview/").replace("../build/", "Logs/").replace("../tests/Vst3HostContract.cpp", "Evidence/Vst3HostContract.cpp").replace("](VERIFICATION.md)", "](docs/VERIFICATION.md)").replace(
        f"../work/Ableton-QA-v{VERSION}/", "Evidence/Ableton/").replace(
        f"](Ableton-QA-v{VERSION}.md)", f"](docs/Ableton-QA-v{VERSION}.md)")
    report_text = report_text.replace('](research-v0.3.0/', '](docs/research-v0.3.0/')
    (version_dir / "Report-verifica.md").write_text(report_text, encoding="utf-8")
    for path in files_in(root / "docs", root):
        copy(path, "docs/" + path.relative_to(root / "docs").as_posix())
    docs_report = version_dir / "docs" / f"VERIFICATION-v{VERSION}.md"
    docs_report.write_text(read_text(docs_report).replace(f"../build/visual-qa-v{VERSION}/", "../Preview/").replace("../build/", "../Logs/").replace("../tests/Vst3HostContract.cpp", "../Evidence/Vst3HostContract.cpp").replace(
        f"../work/Ableton-QA-v{VERSION}/", "../Evidence/Ableton/"), encoding="utf-8")
    ableton_doc = version_dir / "docs" / f"Ableton-QA-v{VERSION}.md"
    ableton_doc.write_text(read_text(ableton_doc).replace(
        f"../work/Ableton-QA-v{VERSION}/", "../Evidence/Ableton/"), encoding="utf-8")
    copy(root / "tests/Vst3HostContract.cpp", "Evidence/Vst3HostContract.cpp")
    for name in ("LICENSE.txt", "RELEASE-NOTES.txt", "MUSIC.txt"):
        if (root / "installer" / name).is_file():
            copy(root / "installer" / name, "Installer/" + name)
    for name in MANDATORY_LOGS + OPTIONAL_LOGS:
        path = root / "build" / name
        if path.is_file() and path.stat().st_size:
            copy(path, "Logs/" + name)
    preview = root / "build" / f"visual-qa-v{VERSION}"
    for path in files_in(preview, root):
        if path.suffix.lower() in {".png", ".odpx", ".wav"} or path.name in {'dsp030-measures.json', 'assist-bridge-summary.json'}:
            copy(path, "Preview/" + path.relative_to(preview).as_posix())
    audio_directory = root / "build" / f"quality-audio-v{VERSION}"
    for path in files_in(audio_directory, root):
        if path.suffix.lower() in {".wav", ".csv"}:
            copy(path, "AudioQA/" + path.relative_to(audio_directory).as_posix())
    (version_dir / "AudioQA/README.txt").write_text(
        "Native DSP renders without normalization. dry-bass/kick/sum are references. "
        "The35-case CSV reports intentional ducking separately from null/recovery tests. "
        "Fingerprints and measured file gates are in ../Logs/quality-audio-v0.3.0.json. "
        "These files do not certify human listening or DAW tests.\n", encoding="utf-8")
    for name in DASHBOARD_FILES:
        copy(root / "dashboard" / name, "Dashboard/" + name)
    ableton = evidence["abletonEvidence"]
    copy(root / ableton["postRunReport"], "Evidence/Ableton/POST-RUN-REPORT.json")
    copy(root / ableton["selectionManifest"], "Evidence/Ableton/EVIDENCE-FILES.json")
    if ableton.get("secondaryProfileRestoration"):
        secondary = ableton["secondaryProfileRestoration"]
        source = root / secondary["path"]
        require(digest(source) == secondary["sha256"], 'Secondary rollback metadata changed before copying')
        copy(source, "Evidence/Ableton/SECONDARY-PROFILE-RESTORE.json")
    qa_root = root / f"work/Ableton-QA-v{VERSION}"
    for name in ("AUTOMATION-AUDIT.json", "LOADED-MODULES.json"):
        path = qa_root / name
        require(path.is_file(), f"Final Ableton audit is missing: {name}")
        copy(path, "Evidence/Ableton/" + name)
    copy(root / "tools/audit_ableton_automation.py", "tools/audit_ableton_automation.py")
    for screenshot in ableton["screenshots"]:
        copy(qa_root / screenshot["relative"], "Evidence/Ableton/" + screenshot["relative"])
    for selected in ableton["exportReports"] + ableton["exportWaves"]:
        relative = "Evidence/Ableton/" + selected["relative"]
        copy(qa_root / selected["relative"], relative)
        require(digest(version_dir / relative) == selected["sha256"], f"Selected Ableton export changed during copying: {relative}")
    manifest = {**evidence, "packagedAtUTC": datetime.now(timezone.utc).isoformat(),
                "sourceArchive": source_zip.name, "sourceFileCount": len(sources),
                "vst3Archive": vst_zip.name, "vst3FileCount": len(payload)}
    manifest_path = safe_output(version_dir / "RELEASE-MANIFEST.json", releases)
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    new_artifacts = [source_zip, vst_zip, releases / f"{prefix}-Windows-x64-Setup.exe"]
    new_artifacts += list(files_in(version_dir, root))
    lines = []
    for path in sorted(new_artifacts, key=lambda item: item.relative_to(releases).as_posix().lower()):
        relative = path.relative_to(releases).as_posix()
        if path.is_relative_to(version_dir):
            forbidden_parts = {"preferences-before", "crash", "crashes", "crashreports", "private-logs"}
            private_names = {"log.txt", "pluginscanner.txt", "pluginscandb.txt", "analytics.db",
                             "indexer.txt", "webconnector.txt", "wc.log"}
            require(not any(part.lower() in forbidden_parts for part in path.relative_to(version_dir).parts)
                    and path.name.lower() not in private_names,
                    f"Private Ableton backup/log/crash material cannot be included in release output: {relative}")
        require(not ("0.1.0" in path.name and path.suffix.lower() in {".exe", ".zip", ".log"}),
                f"A previous release artifact was mixed into current hash coverage: {relative}")
        lines.append(f"{digest(path)}  {relative}")
    hashes = safe_output(releases / f"SHA256-v{VERSION}.txt", releases)
    hashes.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # Re-read every checksummed file after all copies and ZIP replacements finish.
    for line in lines:
        expected, relative = line.split("  ", 1)
        require(digest(releases / relative) == expected, f"Final artifact changed during packaging: {relative}")
    return {"version": VERSION, "sourceArchive": str(source_zip), "vst3Archive": str(vst_zip),
            "setup": str(releases / f"{prefix}-Windows-x64-Setup.exe"),
            "supportingFiles": str(version_dir), "sha256": str(hashes),
            "integrityVerifiedFiles": len(lines), "sourceFileCount": len(sources)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--pre-daw", action="store_true", help="Review complete non-DAW gates before the authorized final Ableton QA session; requires --dry-run")
    parser.add_argument("--dry-run", action="store_true", help="Verify final gates without writing artifacts")
    args = parser.parse_args()
    try:
        result = package(args.root.resolve(), args.dry_run, args.pre_daw)
    except Exception as error:
        print(f"Packaging refused: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
