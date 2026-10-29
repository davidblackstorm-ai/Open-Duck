"""Package only an actually verified OpenDuck Pro X 0.3.1 release.

Read-only validation precedes all output. No test/DAW/installer is launched.
Only a new releases/0.3.1 directory is published; previous releases and the
compile-only Setup are never overwritten. No pending-observation bypass exists.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import uuid
import zipfile

VERSION = "0.3.1"
PREFIX = f"OpenDuck-Pro-X-{VERSION}"
BUNDLE = "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3"
BINARY = BUNDLE + "/Contents/x86_64-win/OpenDuck Pro X.vst3"
MODULEINFO = BUNDLE + "/Contents/Resources/moduleinfo.json"
NATIVE_REPORT = f"build/non-daw-v{VERSION}.json"
QA_ROOT = f"work/Ableton-QA-v{VERSION}"
CHECK_NAMES = {"legacy", "dsp", "quality", "audio", "ui", "ai", "bridge", "parity", "host",
               "lifecycle", "vst3_lifecycle", "state_commit", "assist_exception", "steinberg", "pluginval",
               "waveform_capture", "scope_freeze", "scope_edge", "waveform_ui"}
WAVEFORM_COUNTS = {"waveform_capture": 108, "scope_freeze": 18, "scope_edge": 21, "waveform_ui": 109}
EXTRA_COUNTS = {"lifecycle": 41, "vst3_lifecycle": 82, "state_commit": 7, "assist_exception": 6, **WAVEFORM_COUNTS}
NATIVE_COUNTS = {"legacy": 76, "dsp": 51, "quality": 16, "audio": 35, "ui": 43,
                 "ai": 76, "bridge": 179, "parity": 15, "host": 11, **EXTRA_COUNTS}
CONFIGURATION_FILES = {"CMakeLists.txt", "cmake/OpenDuckWindowsResources.cmake"}
OBSERVATIONS = {"first_insert_responsive", "editor_open_close_resize", "fresh_init_schema3",
                "legacy_schema1_2_recall", "six_sector_curve_independence", "audio_sc_midi_clock",
                "automation_saved", "session_save_reopen", "instance_independence"}
SETTINGS = {"Preferences.cfg", "Library.cfg", "Options.txt", "UsageData.cfg"}
SOURCE_DIRS = ("Source", "tests", "Assets", "cmake", "installer", "third_party/JUCE")
SKIP_DIRS = {".git", ".svn", ".vs", ".cache", "__pycache__", "node_modules", "cmakefiles", "build",
             "releases", "private", "preferences-before", "recovery-before-close", "crashreports"}
PRIVATE_SUFFIXES = {".pdb", ".ilk", ".ipdb", ".iobj", ".dmp", ".mdmp", ".als", ".alc", ".adg", ".asd"}
BUILD_SUFFIXES = {".exe", ".dll", ".obj", ".lib", ".exp", ".res", ".a", ".so", ".dylib", ".o"}
PRIVATE_NAMES = {"preferences.cfg", "library.cfg", "usagedata.cfg", "options.txt", "log.txt",
                 "pluginscanner.txt", "pluginscandb.txt", "analytics.db", "webconnector.txt", "wc.log"}
TOOL_SUFFIXES = {".py", ".ps1", ".cpp", ".h", ".cmake", ".txt"}
DOC_SUFFIXES = {".md", ".txt", ".json", ".csv", ".h", ".cpp", ".py", ".ps1", ".cmake", ".log",
                ".png", ".jpg", ".jpeg", ".svg", ".html", ".css", ".js", ".pdf"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def secure(root, relative, *, exists=True):
    """Resolve only project-relative files without links, traversal or ADS."""
    root = Path(root)
    require(isinstance(relative, str) and relative and "\\" not in relative and ":" not in relative,
            f"Invalid project-relative path: {relative}")
    parts = PurePosixPath(relative)
    require(not parts.is_absolute() and all(p not in (".", "..") for p in parts.parts), "Unsafe project-relative path")
    path = root.joinpath(*parts.parts)
    for item in (path, *path.parents):
        try:
            attributes = item.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(attributes.st_mode) and not (getattr(attributes, "st_file_attributes", 0) & 0x400),
                f"Reparse/symlink path forbidden: {item}")
        if item == root:
            break
    require(path.resolve().is_relative_to(root.resolve()), "Path escaped project")
    if exists:
        require(path.exists(), f"Required file/directory missing: {relative}")
    return path


def text(path):
    data = Path(path).read_bytes()
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def read_json(root, relative):
    path = secure(root, relative)
    require(path.is_file() and path.stat().st_size <= 64 * 1024 * 1024, f"Invalid/oversized JSON evidence: {relative}")
    result = json.loads(text(path))
    require(isinstance(result, dict), f"JSON evidence must be an object: {relative}")
    return result


def check_hash_map(root, records, description, *, minimum=1):
    require(isinstance(records, dict) and len(records) >= minimum, f"Missing {description} fingerprints")
    for relative, expected in records.items():
        require(isinstance(expected, str) and re.fullmatch(r"[a-f0-9]{64}", expected), f"Invalid {description} hash")
        path = secure(root, relative)
        require(path.is_file() and sha(path) == expected, f"{description} changed: {relative}")


def call_validator(root, name, *args, **kwargs):
    """Use the actual validator from this project, not a different checkout."""
    path = secure(root, f"tools/{name}.py")
    spec = importlib.util.spec_from_file_location(f"odx_pack031_{name}", path)
    require(spec is not None and spec.loader is not None, f"Cannot load validator: {name}")
    module = importlib.util.module_from_spec(spec)
    old_path = sys.path[:]
    old_bytecode = sys.dont_write_bytecode
    try:
        sys.path.insert(0, str(root / "tools"))
        sys.dont_write_bytecode = True
        spec.loader.exec_module(module)
        return module.validate(root, *args, **kwargs)
    finally:
        sys.path[:] = old_path
        sys.dont_write_bytecode = old_bytecode


def tree_files(root, relative, *, source=False):
    base = secure(root, relative)
    require(base.is_dir(), f"Required source directory missing: {relative}")
    for current, dirs, files in os.walk(base, followlinks=False):
        dirs[:] = sorted(name for name in dirs if name.casefold() not in SKIP_DIRS)
        for name in dirs:
            secure(root, (Path(current) / name).relative_to(root).as_posix())
        for name in sorted(files):
            file = Path(current) / name
            rel = file.relative_to(root).as_posix()
            lower = name.casefold()
            if lower == ".git" or file.suffix.casefold() in {".pyc", ".pyo"}:
                continue
            if source and (file.suffix.casefold() in PRIVATE_SUFFIXES | BUILD_SUFFIXES or lower in PRIVATE_NAMES):
                continue
            yield secure(root, rel)


def source_inventory(root):
    files = []
    for name in SOURCE_DIRS:
        files.extend(tree_files(root, name, source=True))
    for name in ("CMakeLists.txt", "README.md", "LICENSE.txt", "READMEAPI.txt", ".clang-format"):
        path = secure(root, name, exists=False)
        if name in {"CMakeLists.txt", "README.md", "LICENSE.txt"}:
            require(path.is_file(), f"Required source root file missing: {name}")
        if path.is_file():
            files.append(path)
    for path in tree_files(root, "docs", source=True):
        if path.suffix.casefold() in DOC_SUFFIXES:
            files.append(path)
    # All first-level build/verification source scripts are included, never the
    # bundled validator executables, interpreter installations or private work.
    for path in secure(root, "tools").iterdir():
        if path.is_file() and path.suffix.casefold() in TOOL_SUFFIXES:
            files.append(secure(root, path.relative_to(root).as_posix()))
    dashboard = secure(root, "dashboard", exists=False)
    if dashboard.is_dir():
        for path in tree_files(root, "dashboard", source=True):
            if path.suffix.casefold() in {".html", ".css", ".js", ".json", ".png", ".jpg", ".jpeg", ".svg"}:
                files.append(path)
    files = sorted(set(files), key=lambda p: p.relative_to(root).as_posix().casefold())
    names = {path.relative_to(root).as_posix() for path in files}
    require("cmake/OpenDuckWindowsResources.cmake" in names, "Corresponding source misses Windows resource dependency fix")
    require("tools/pack_release031.py" in names, "Promote this packaging source into project tools before packaging")
    # This public, pinned transformation document is part of the reproducible
    # checker inputs. No other private work tree is selected for the source ZIP.
    transform = secure(root, "work/hotfix031/waveform/SCOPE-EXACT-TRANSFORMS.json")
    require(transform.is_file(), "Corresponding source misses the reviewed scope transformation document")
    files.append(transform)
    require("third_party/JUCE/LICENSE.md" in names and "docs/AGPL-3.0.txt" in names,
            "Existing JUCE/project license notices must accompany their source")
    require({"tests/fixtures/audio/LAB-Kick-145.wav", "tests/fixtures/audio/LAB-Bass-145.wav"} <= names,
            "Approved reproducible native audio fixtures must accompany the test sources")
    # Media inside third-party JUCE belongs to the supplied framework source;
    # project WAV inputs are restricted to the existing explicit QA fixtures and
    # original installer composition, not any user's TEST LAB project media.
    for path in files:
        rel = path.relative_to(root).as_posix()
        if path.suffix.casefold() in {".wav", ".flac", ".mp3", ".aif", ".aiff"} and not rel.startswith("third_party/JUCE/"):
            require(rel in {"tests/fixtures/audio/LAB-Kick-145.wav", "tests/fixtures/audio/LAB-Bass-145.wav",
                            "installer/OriginalTechno.wav"}, f"Unapproved personal/audio media in source: {rel}")
    return files


def validate_native(root):
    proof = read_json(root, NATIVE_REPORT)
    binary_sha = sha(secure(root, BINARY))
    require(proof.get("version") == VERSION and proof.get("complete") is True and not proof.get("failure") and
            proof.get("releaseBinarySHA256") == binary_sha, "A complete final nineteen-gate run for this DLL is required")
    versions = re.findall(r'"Version"\s*:\s*"([^"]+)"', text(secure(root, MODULEINFO)))
    require(versions and set(versions) == {VERSION} and proof.get("bundleMetadataSHA256") == sha(secure(root, MODULEINFO)),
            "Final VST3 metadata version/hash mismatch")
    require(all(type(value) is int and value > 0 for value in EXTRA_COUNTS.values()) and
            proof.get("expectedExtraCounts") == EXTRA_COUNTS and proof.get("expectedNativeCounts") == NATIVE_COUNTS,
            "Hotfix/waveform/native regression counts were not pinned in the final run")
    require(set(proof.get("configurationSHA256", {})) == CONFIGURATION_FILES, "CMake configuration/helper fingerprints missing")
    check_hash_map(root, proof["configurationSHA256"], "CMake configuration/helper")
    check_hash_map(root, proof.get("sourceSHA256"), "Source/tests")
    current = {path.relative_to(root).as_posix(): sha(path) for directory in ("Source", "tests")
               for path in secure(root, directory).rglob("*") if path.is_file()}
    require(current == proof["sourceSHA256"], "Source/test inventory changed after final verification")
    for name in ("inputEvidenceSHA256", "verifierToolSHA256", "generatedEvidenceSHA256"):
        check_hash_map(root, proof.get(name), name)
    require({"build/visual-qa-v0.3.1/scope-capture031-measures.json",
             "build/visual-qa-v0.3.1/native-waveform-width1010.png"} <= set(proof["generatedEvidenceSHA256"]),
            "Actual waveform capture/UI evidence fingerprints are missing")
    checks = proof.get("checks", {})
    require(set(checks) == CHECK_NAMES, "All nineteen final gates must be present exactly once")
    evidence = {NATIVE_REPORT, MODULEINFO}
    for name, record in checks.items():
        require(record.get("passed") is True and record.get("exitCode") == 0, f"Unresolved native gate: {name}")
        log = secure(root, record.get("log"))
        require(sha(log) == record.get("logSHA256"), f"Native log changed: {name}")
        command = record.get("command", [])
        require(isinstance(command, list) and command and Path(command[0]).is_absolute(), "Native command identity missing")
        executable = Path(command[0])
        require(executable.resolve().is_relative_to(root), "Native executable is outside the project")
        executable = secure(root, executable.relative_to(root).as_posix())
        require(sha(executable) == record.get("executableSHA256"), f"Native executable changed: {name}")
        output = text(log)
        require(not re.search(r"^FAIL\s", output, re.MULTILINE), f"Native failure remains in {name}")
        results = re.findall(r"RESULT\s+(\d+)\s+passed,\s*(\d+)\s+failed", output)
        require(all(int(p) > 0 and int(f) == 0 for p, f in results), f"Failed child RESULT in {name}")
        if name == "pluginval":
            require(record.get("count") is None and output.rstrip().endswith("SUCCESS") and
                    re.search(r"Strictness level:\s*10\b", output), "pluginval strictness10/SUCCESS not established")
        elif name == "steinberg":
            require(record.get("count") == 47 and re.search(r"Result:\s*47\s+tests passed,\s*0\s+tests failed", output),
                    "Steinberg basic47 result missing")
            variants = record.get("variants", {})
            require(set(variants) == {"extensive-global", "extensive-local"}, "Both Steinberg extensive lifetimes required")
            for variant in variants.values():
                variant_log = secure(root, variant.get("log"))
                require(variant.get("passed") is True and variant.get("exitCode") == 0 and variant.get("count") == 537 and
                        variant.get("executableSHA256") == sha(executable) and sha(variant_log) == variant.get("logSHA256") and
                        re.search(r"Result:\s*537\s+tests passed,\s*0\s+tests failed", text(variant_log)), "Steinberg extensive537 proof mismatch")
                evidence.add(variant_log.relative_to(root).as_posix())
        elif name == "audio":
            require(record.get("count") == 35 and record.get("pairedAudioEquivalencePassed") is True and
                    re.search(r"RENDERED\s+35\s+cases;", output), "Actual35-case audio render missing")
        else:
            require(results and int(results[-1][0]) == record.get("count") > 0, f"Native RESULT/count mismatch: {name}")
            if name in NATIVE_COUNTS:
                require(record["count"] == NATIVE_COUNTS[name], f"Incomplete strict native regression suite: {name}")
        evidence.add(log.relative_to(root).as_posix())
    lineage = call_validator(root, "quality_host_hotfix_lineage031")
    quality = call_validator(root, "verify_quality_audio031", root / f"build/quality-audio-v{VERSION}")
    audio = checks["audio"]
    paired = call_validator(root, "verify_host_hotfix_audio_equivalence031", root / "build/quality-audio-v0.3.0",
                            root / f"build/quality-audio-v{VERSION}", new_binary_sha256=binary_sha,
                            new_renderer_sha256=audio["executableSHA256"], renderer_log_sha256=audio["logSHA256"])
    for relative, actual in ((f"build/ai-quality-lineage-v{VERSION}.json", lineage),
                             (f"build/quality-audio-v{VERSION}.json", quality),
                             (f"build/host-hotfix-audio-equivalence-v{VERSION}.json", paired)):
        require(actual.get("passed") is True and actual == read_json(root, relative) and
                proof["generatedEvidenceSHA256"].get(relative) == sha(secure(root, relative)), f"Recorded evidence is stale: {relative}")
        evidence.add(relative)
    require(proof.get("pairedAudioEquivalence") == paired, "Native report paired proof differs from real revalidation")
    require(quality.get("rendererCaseCount") == paired.get("caseCount") == 35 and
            quality.get("waveFileCount") == paired.get("waveCount") == 23, "Incomplete paired audio inventory")
    evidence.update(proof["generatedEvidenceSHA256"])
    evidence.add("build/training-reproducibility-v0.3.0-r2.json")
    return {"report": proof, "binarySHA256": binary_sha, "lineage": lineage, "quality": quality,
            "paired": paired, "publicFiles": sorted(evidence)}


def identities(records):
    require(isinstance(records, list), "Live process identity list missing")
    result = set()
    for row in records:
        require(isinstance(row, dict) and type(row.get("id")) is int and row["id"] > 0 and
                isinstance(row.get("startTimeUtcTicks"), str) and re.fullmatch(r"\d{16,20}", row["startTimeUtcTicks"]) and
                isinstance(row.get("processName"), str) and row["processName"].startswith("Ableton Live"), "Invalid Live identity")
        result.add((row["id"], row["startTimeUtcTicks"], row["processName"]))
    require(len(result) == len(records), "Duplicate Live identities")
    return result


def validate_ableton(root, native):
    """Read actual manual observations and the closed/restored collector checkpoint.

    This never reruns the collector or opens preferences/processes. An explicitly
    requested later reopening is reported separately from the closed checkpoint.
    """
    prefix = QA_ROOT + "/"
    observations = read_json(root, prefix + "OBSERVATIONS-031.json")
    post = read_json(root, prefix + "POST-RUN-REPORT.json")
    baseline = read_json(root, prefix + "baseline.json")
    module = read_json(root, prefix + "LOADED-MODULES.json")
    automation = read_json(root, prefix + "AUTOMATION-AUDIT.json")
    documentation = secure(root, f"docs/Ableton-QA-v{VERSION}.md")
    require(observations.get("version") == VERSION and observations.get("status") == "passed",
            "Actual final Ableton observations are pending/missing")
    non_daw = observations.get("nonDAW", {})
    require(non_daw.get("allFinalGatesPassed") is True and non_daw.get("reportSHA256") == sha(secure(root, NATIVE_REPORT)),
            "Ableton session does not identify the completed final native build")
    require(post.get("version") == VERSION and post.get("postRunChecksPassed") is True and
            baseline.get("version") == VERSION and baseline.get("baselineComplete") is True and baseline.get("fixtureOnly", False) is False,
            "Actual Ableton restored checkpoint is missing, synthetic or failed")
    require(post.get("baselineSHA256") == sha(secure(root, prefix + "baseline.json")) ==
            text(secure(root, prefix + "baseline.json.sha256")).strip().lower(), "Ableton baseline metadata changed")
    for name in ("recoveryArchiveUnchanged", "fourPreferencesRestoredExactly", "baselinePreferenceSnapshotsIntact",
                 "productionBundleUnchanged", "qaProcessClosed", "noAdditionalLiveProcesses", "initialLiveProcessesPreserved",
                 "initialProcessStateRestored", "targetReleaseBinaryUnchanged", "originalQASetUnchanged",
                 "originalQASetSnapshotIntact", "schema3StateCaptured", "schema3StatesComplete", "schema3ModernEngineCaptured"):
        require(post.get("checks", {}).get(name) is True, f"Unresolved Ableton checkpoint: {name}")
    require(baseline.get("targetBinarySHA256") == post.get("releaseBinary", {}).get("sha256") == native["binarySHA256"],
            "Ableton checkpoint belongs to another DLL")
    settings = post.get("settings", [])
    require(len(settings) == 4 and {row.get("name") for row in settings} == SETTINGS and
            all(row.get("restoredExactly") is True and row.get("baselineSnapshotIntact") is True for row in settings),
            "Four named preference snapshots/restoration records are required")
    initial = identities(baseline.get("initialLiveProcesses", []))
    process = post.get("process", {})
    require(identities(process.get("initialLiveProcesses")) == identities(process.get("currentlyRunningLiveProcesses")) == initial and
            process.get("additionalLiveProcesses") == process.get("missingInitialLiveProcesses") == [],
            "Initial Live processes were not preserved at the checkpoint")
    require(module.get("version") == VERSION and module.get("proofPassed") is True and
            module.get("expectedSHA256") == native["binarySHA256"] and module.get("baselineSHA256") == post["baselineSHA256"],
            "Actual loaded final-module identity proof is required")
    require(module.get("schema") == "openduck-loaded-module031-v1" and
            module.get("nonDAWReportSHA256") == sha(secure(root, NATIVE_REPORT)) and
            module.get("auditToolSHA256") == sha(secure(root, "tools/audit_loaded_openduck_v031.ps1")) and
            isinstance(module.get("processStartTimeUtcTicks"), str) and re.fullmatch(r"\d{16,20}", module["processStartTimeUtcTicks"]) and
            all(module.get("checks", {}).get(name) is True for name in ("stableHostIdentityMatched", "finalNineteenNativeGatesMatched",
                     "allLoadedOpenDuckModulesMatchFinalBinary", "allLoadedOpenDuckModulesMatchExpectedSelectedPath")),
            "The guarded031 PID/executable/start-time module audit is required")
    loaded = module.get("loadedOpenDuckModules", [])
    require(loaded and all(row.get("sha256") == native["binarySHA256"] and row.get("matchesTargetHash") is True for row in loaded),
            "A different module was loaded by Live")
    host = observations.get("host", {})
    require(type(module.get("processId")) is int and module["processId"] > 0 and host.get("pid") == module["processId"] and
            isinstance(module.get("hostProductVersion"), str) and re.match(r"^12\.", module["hostProductVersion"]),
            "Actual Ableton12 host identity missing")
    require(observations.get("installedModule", {}).get("matchesFinalGateBinary") is True and
            observations["installedModule"].get("sha256") == native["binarySHA256"], "Manual session module differs from final native build")
    rows = observations.get("observations", [])
    require(len(rows) == len(OBSERVATIONS) and {row.get("id") for row in rows} == OBSERVATIONS,
            "Nine named real Ableton observations are required")
    require(all(row.get("status") == "passed" and isinstance(row.get("evidence"), list) and row["evidence"] and
                all(isinstance(value, str) and value.strip() for value in row["evidence"]) for row in rows),
            "A manual Ableton observation remains unexercised/unresolved")
    ai = observations.get("localAI", [])
    require(len(ai) == 6 and {row.get("mode") for row in ai} == set(range(6)), "Six real sector AI observations required")
    applied = 0
    for row in ai:
        require(row.get("status") == "passed" and isinstance(row.get("decision"), str) and row["decision"].strip() and
                type(row.get("capturedSeconds")) in (int, float) and row["capturedSeconds"] > 0 and
                type(row.get("validHits")) is int and row["validHits"] >= 0 and
                type(row.get("applyUndoObserved")) is bool, f"AI observation incomplete for sector {row.get('mode')}")
        # An actual abstention is an observation, not an invented Apply success.
        if row["applyUndoObserved"]:
            applied += 1
        else:
            require(isinstance(row.get("reason"), str) and row["reason"].strip(),
                    "Declare why Apply/Undo was not exercised (e.g. actual assist abstention)")
    state = post.get("savedStateEvidence", {})
    require(state.get("schema3ChunkCount", 0) > 0 and state.get("allSchema3ChunksComplete") is True and
            state.get("modernSchema3ChunkCount", 0) > 0, "Actual complete modern schema3 recall evidence missing")
    require(automation.get("version") == VERSION and automation.get("proofPassed") is True and
            automation.get("amountPhaseBindingProof", {}).get("passed") is True and
            automation.get("expectedReleaseBinarySHA256") == native["binarySHA256"] and
            automation.get("nonDAWReportSHA256") == sha(secure(root, NATIVE_REPORT)) and
            automation.get("baselineSHA256") == post["baselineSHA256"],
            "Current031 Amount/Phase binding audit must identify this baseline and final nineteen-gate build")
    require(automation.get("auditToolSHA256") == sha(secure(root, "tools/audit_ableton_automation_v031.py")) and
            automation.get("parameterReferenceSHA256") == sha(secure(root, "tools/gather_ableton_qa_v031.py")),
            "Automation auditor/413-parameter reference changed after collection")
    mapping_sources = automation.get("mappingSourceSHA256", {})
    require(set(mapping_sources) == {"Source/PluginProcessor.cpp", "CMakeLists.txt",
                "third_party/JUCE/modules/juce_audio_processors/utilities/juce_VST3ClientExtensions.cpp"},
            "Automation parameter-mapping source fingerprints are incomplete")
    check_hash_map(root, mapping_sources, "Automation parameter mapping", minimum=3)
    require(automation.get("setSHA256") == state.get("qaSetSHA256") and automation.get("modernPluginCount", 0) >= 2 and
            automation.get("modernAutomatedLaneCount", 0) > 0 and automation.get("modernNonnegativeTimeEventCount", 0) > 0 and
            all(row.get("allRecordedBindingsMatchJuce") is True for row in automation.get("plugins", []) if row.get("modernMask") == 63),
            "Actual saved modern automation with correct VST3 bindings required")
    restoration = observations.get("restoration", {})
    require(restoration.get("closedCheckpointSHA256") == sha(secure(root, prefix + "POST-RUN-REPORT.json")) and
            all(restoration.get(name) is True for name in ("fourPreferencesRestoredExactly", "originalQASetUnchanged", "recoveryArchiveUnchanged")),
            "Manual observations do not identify the verified restoration checkpoint")
    public = [f"docs/Ableton-QA-v{VERSION}.md", prefix + "POST-RUN-REPORT.json", prefix + "OBSERVATIONS-031.json",
              prefix + "LOADED-MODULES.json", prefix + "AUTOMATION-AUDIT.json"]
    screenshots = observations.get("screenshots", [])
    selection_path = secure(root, prefix + "EVIDENCE-FILES.json", exists=False)
    if selection_path.is_file():
        selection = read_json(root, prefix + "EVIDENCE-FILES.json")
        screenshots = selection.get("screenshots", screenshots)
        public.append(prefix + "EVIDENCE-FILES.json")
    require(isinstance(screenshots, list) and all(isinstance(name, str) for name in screenshots) and len(screenshots) == len(set(screenshots)),
            "Explicit selected screenshots must be a unique list")
    for relative in screenshots:
        path = secure(root, prefix + relative)
        require(path.suffix.casefold() in {".png", ".jpg", ".jpeg", ".webp"}, "Selected evidence must be an image")
        require(not any(part.casefold() in SKIP_DIRS for part in PurePosixPath(relative).parts), "Private QA material cannot be selected")
        signature = path.read_bytes()[:12]
        require(signature.startswith((b"\x89PNG\r\n\x1a\n", b"\xff\xd8")) or
                signature.startswith(b"RIFF") and signature[8:12] == b"WEBP", "Invalid selected screenshot")
        public.append(prefix + relative)
    limitations = observations.get("limitations", [])
    require(isinstance(limitations, list) and all(isinstance(row, str) for row in limitations), "Invalid observed limitation list")
    if applied < 6:
        limitations = [*limitations, f"Apply/Undo osservato in {applied}/6 settori; gli altri esiti AI sono documentati senza attribuire un Apply non eseguito."]
    require(VERSION in text(documentation), "Actual Ableton report has not been updated to 0.3.1")
    return {"requiredObservationsRecorded": True, "hostVersion": module["hostProductVersion"], "hostProcessId": module["processId"],
            "baselineSHA256": post["baselineSHA256"], "loadedBinarySHA256": native["binarySHA256"], "aiObservedSectors": 6,
            "aiApplyUndoObservedSectors": applied, "aiDecisions": ai, "schema3ChunkCount": state["schema3ChunkCount"],
            "savedAutomationLanes": automation["modernAutomatedLaneCount"], "restoredClosedCheckpoint": True,
            "finalUserRequestedState": observations.get("finalUserRequestedState", {}), "limitations": limitations,
            "publicFiles": public, "publicEvidenceSHA256": {relative: sha(secure(root, relative)) for relative in public},
            "observationSHA256": sha(secure(root, prefix + "OBSERVATIONS-031.json"))}


def validate(root):
    root = Path(root)
    require(root.is_absolute() and root.is_dir(), "Use an existing absolute project --root")
    root = root.resolve()
    secure(root, "CMakeLists.txt")
    require(re.search(r"project\s*\(\s*OpenDuckProX\s+VERSION\s+0\.3\.1\b", text(root / "CMakeLists.txt"), re.I),
            "CMake project version is not 0.3.1")
    require("openduck_fix_windows_resource_dependencies(OpenDuckProX)" in text(root / "CMakeLists.txt"),
            "Windows RC dependency correction is not applied")
    native = validate_native(root)
    installer = call_validator(root, "installer_release_evidence_v031")
    require(installer.get("passed") is True, "Current strict installer provenance did not pass")
    ableton = validate_ableton(root, native)
    sources = source_inventory(root)
    bundle = list(tree_files(root, BUNDLE))
    require(bundle and not any(path.suffix.casefold() in PRIVATE_SUFFIXES for path in bundle), "Private debug data in VST3 payload")
    require(VERSION in text(secure(root, "installer/RELEASE-NOTES.txt")), "Release notes version is stale")
    return root, native, installer, ableton, sources, bundle


def italian_report(native, installer, ableton):
    names = {"legacy": "Compatibilità e regressioni", "dsp": "DSP", "quality": "Qualità audio mirata", "audio": "Render audio reali",
             "ui": "Interfaccia nativa", "ai": "AI locale", "bridge": "Integrazione AI", "parity": "Parità modelli",
             "host": "Host VST3", "lifecycle": "Ciclo di vita e PDC", "vst3_lifecycle": "Host VST3 con HWND reale",
             "state_commit": "Ripristino stato atomico", "assist_exception": "Contenimento eccezioni AI",
             "steinberg": "Steinberg", "pluginval": "pluginval", "waveform_capture": "Cattura waveform per colpo",
             "scope_freeze": "Freeze e ripresa waveform", "scope_edge": "Scala, MIDI, Trace e concorrenza",
             "waveform_ui": "Waveform nativa e assi temporali"}
    lines = [f"# OpenDuck Pro X {VERSION} — rapporto di verifica", "",
             f"DLL finale SHA256: `{native['binarySHA256']}`.", "",
             "I conteggi seguenti provengono dal manifest della verifica realmente eseguita sulla DLL consegnata. Il confezionamento non avvia nuovi test.", "",
             "| Verifica | Esito registrato |", "|---|---|"]
    for name, row in native["report"]["checks"].items():
        result = "SUCCESS, strictness 10" if name == "pluginval" else f"{row['count']} superati"
        lines.append(f"| {names[name]} | {result} |")
    lines += ["", "Steinberg esteso: 537 superati con istanza globale e 537 con istanze locali.", "",
              "Il confronto 0.3.0 → 0.3.1 verifica 35 casi numerici e 23 WAV con PCM identico. La correzione riguarda pubblicazione della latenza, completamento del ripristino prima della notifica host contenimento delle eccezioni AI e cattura/visualizzazione waveform sincronizzata; non attribuisce un giudizio percettivo.", "",
              "Installer: aggiornamento isolato da 0.3.0, reinstallazione e disinstallazione verificati; payload completo confrontato, preset conservati e produzione invariata. Il programma di confezionamento non esegue l'installazione di produzione.", "",
              f"Ableton {ableton['hostVersion']}: nove osservazioni richieste registrate, richiamo schema3 e automazione salvata; {ableton['aiObservedSectors']}/6 AI osservate, Apply/Undo effettivamente osservato in {ableton['aiApplyUndoObservedSectors']}/6. Il checkpoint chiuso documenta il ripristino delle quattro preferenze e la conservazione dei file originali.", "",
              "Il report Ableton originale è in Evidence/Ableton/. Eventuale riapertura finale richiesta dall'utente è distinta dal checkpoint di ripristino.", "",
              "Limiti dichiarati:", "", "- PDC fisso: circa 252,3 ms a 48 kHz; non è un processore a latenza zero.",
              "- Modelli R2 conservati con provenienza verificata; analisi locale di 5/10 secondi. AI può astenersi; non sostituisce eventi MIDI/Hz non acquisiti.",
              "- Non sono certificati ascolto umano, ogni DAW, prove prolungate di RAM/thread, installazione su un Windows pulito o tutte le configurazioni audio possibili."]
    lines += ["- " + value for value in ableton["limitations"]]
    lines += ["", f"Setup SHA256: `{installer['productionSetupSHA256']}`.",
              f"Manifest input installer SHA256: `{installer['inputSHA256']}`.", "",
              "Sorgenti corrispondenti, sorgenti JUCE e avvisi di licenza sono nello ZIP Sorgenti. I termini già presenti restano invariati; questo rapporto non afferma una licenza commerciale JUCE né nuovi diritti sull'artwork di riferimento.", ""]
    return "\n".join(lines)


def write_zip(path, entries):
    names = [name for _, name in entries]
    require(len(names) == len(set(name.casefold() for name in names)), "Duplicate ZIP member name")
    require(all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts for name in names), "Unsafe ZIP member")
    with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as archive:
        for source, name in entries:
            before = sha(source)
            archive.write(source, name)
            require(sha(source) == before, f"Source changed while writing archive: {source}")
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None and archive.namelist() == names, "ZIP integrity/member validation failed")
        for source, name in entries:
            with archive.open(name) as stream:
                require(hashlib.file_digest(stream, "sha256").hexdigest() == sha(source), f"ZIP member differs from current source: {name}")


def package(root, *, dry_run=False):
    root, native, installer, ableton, sources, bundle = validate(root)
    if dry_run:
        return {"version": VERSION, "validatedOnly": True, "filesWritten": False, "sourceFileCount": len(sources),
                "sourceBytes": sum(path.stat().st_size for path in sources), "binarySHA256": native["binarySHA256"],
                "actualAbletonObservationsRecorded": True, "aiApplyUndoObservedSectors": ableton["aiApplyUndoObservedSectors"]}
    require(root.drive.casefold() == "e:", "Write the release only in the E: project selected by the user")
    releases = secure(root, "releases")
    final = secure(root, f"releases/{VERSION}", exists=False)
    require(not final.exists(), "Release0.3.1 already exists; explicitly archive that attempt before packaging again")
    stage = secure(root, f"releases/.pack-{VERSION}-{uuid.uuid4().hex}", exists=False)
    stage.mkdir(exist_ok=False)
    before_sources = {path.relative_to(root).as_posix(): sha(path) for path in sources}

    def copy(relative, destination):
        source = secure(root, relative)
        target = stage / destination
        require(target.resolve().is_relative_to(stage), "Copy target escaped release staging")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        require(sha(target) == sha(source), f"Release copy differs: {relative}")

    try:
        source_zip = stage / f"{PREFIX}-Sorgenti.zip"
        vst_zip = stage / f"{PREFIX}-VST3.zip"
        write_zip(source_zip, [(path, f"{PREFIX}/" + path.relative_to(root).as_posix()) for path in sources])
        write_zip(vst_zip, [(path, "OpenDuck Pro X.vst3/" + path.relative_to(root / BUNDLE).as_posix()) for path in bundle])
        setup_relative = f"releases/{PREFIX}-Windows-x64-Setup.exe"
        copy(setup_relative, Path(setup_relative).name)
        copy("installer/RELEASE-NOTES.txt", "NOTE-DI-RILASCIO.txt")
        copy("installer/LICENSE.txt", "LICENZA-E-AVVISI.txt")
        copy("LICENSE.txt", "LICENSE.txt")
        copy("third_party/JUCE/LICENSE.md", "Licenze/JUCE-LICENSE.md")
        (stage / "RAPPORTO-TEST.md").write_text(italian_report(native, installer, ableton), encoding="utf-8")
        (stage / "LEGGIMI.txt").write_text(
            f"OpenDuck Pro X {VERSION} - Windows x64 / VST3\n\n"
            f"Eseguire {PREFIX}-Windows-x64-Setup.exe quando la DAW è chiusa. "
            "Installazione e aggiornamento sono gestiti dall'installer; i preset personali vengono conservati.\n\n"
            "Verificare SHA256SUMS.txt per l'integrità. Lo ZIP VST3 contiene il bundle; "
            "lo ZIP Sorgenti contiene il progetto corrispondente e JUCE con le rispettive licenze. "
            "Visual Studio C++/Windows SDK, CMake e Python per le verifiche non sono incorporati.\n\n"
            "Leggere NOTE-DI-RILASCIO.txt e RAPPORTO-TEST.md. Le evidenze native sono in Logs/; "
            "il collaudo reale Ableton e gli esiti AI dichiarati sono in Evidence/Ableton/. "
            "Astensione AI non significa Apply eseguito.\n\n"
            "Non sono inclusi dump, PDB, preferenze, recovery o progetti personali.\n", encoding="utf-8")
        for relative in native["publicFiles"]:
            copy(relative, "Logs/" + Path(relative).name)
        for relative in (f"build/installer-qa-results-v{VERSION}.json", f"build/installer-production-v{VERSION}.json"):
            copy(relative, "Logs/" + Path(relative).name)
        for relative in ableton["publicFiles"]:
            destination = "Evidence/Ableton/" + (relative[len(QA_ROOT) + 1:] if relative.startswith(QA_ROOT + "/") else Path(relative).name)
            copy(relative, destination)
        for path in tree_files(root, f"build/visual-qa-v{VERSION}", source=True):
            if path.suffix.casefold() in {".png", ".jpg", ".jpeg"} or path.name in {"dsp030-measures.json", "assist-bridge-summary.json"}:
                copy(path.relative_to(root).as_posix(), "Preview/" + path.relative_to(root / f"build/visual-qa-v{VERSION}").as_posix())
        # Revalidate after ZIP/copy work. Never publish artifacts from a changing
        # build or from evidence that changed while compression was running.
        _, native_after, installer_after, ableton_after, sources_after, _ = validate(root)
        require(native_after == native and installer_after == installer and ableton_after == ableton and
                {path.relative_to(root).as_posix(): sha(path) for path in sources_after} == before_sources,
                "Source/build/evidence changed during packaging")
        manifest = {"schema": "openduck-release031-v1", "version": VERSION, "packagedAtUTC": datetime.now(timezone.utc).isoformat(),
                    "sourceFileCount": len(sources), "sourceSHA256": before_sources, "releaseBinarySHA256": native["binarySHA256"],
                    "nonDAWReportSHA256": sha(secure(root, NATIVE_REPORT)), "nativeGateCount": 19,
                    "installer": installer, "ableton": {key: value for key, value in ableton.items() if key != "publicFiles"},
                    "allFunctionsOrHostsCertified": False,
                    "excluded": "Preference contents/backups, recovery, personal ALS/media, crash dumps/private logs, PDB and build/runtime installations",
                    "packageToolSHA256": sha(Path(__file__).resolve())}
        (stage / "RELEASE-MANIFEST.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        files = sorted((path for path in stage.rglob("*") if path.is_file()), key=lambda p: p.relative_to(stage).as_posix().casefold())
        lines = [f"{sha(path)}  {path.relative_to(stage).as_posix()}" for path in files]
        (stage / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        for path, line in zip(files, lines):
            require(sha(path) == line[:64], "Final release artifact changed while hashing")
        require(not final.exists(), "Another release appeared before publication")
        os.rename(stage, final)
        return {"version": VERSION, "releaseDirectory": str(final), "setup": str(final / Path(setup_relative).name),
                "sourceArchive": str(final / source_zip.name), "vst3Archive": str(final / vst_zip.name),
                "sha256": str(final / "SHA256SUMS.txt"), "fileCount": len(files), "sourceFileCount": len(sources)}
    except BaseException:
        # Do not recursively delete a computed path after an error. Keep the
        # explicitly named staging attempt for inspection; final is unpublished.
        print(f"Packaging stopped; unpublished staging retained: {stage}", file=sys.stderr)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true", help="Require all final evidence, but write nothing")
    args = parser.parse_args()
    try:
        result = package(args.root, dry_run=args.dry_run)
    except Exception as error:
        print(f"Packaging refused: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
