"""Write the factual 0.3.0 verification report from completed, fingerprinted gates.

Only docs/VERIFICATION-v0.3.0.md is replaced. No test, installer, DAW, preference
reader, training job or build is launched. --pre-daw explicitly leaves Ableton
pending; without it actual saved-state/restoration/module/automation evidence and
the manually written Ableton report are mandatory. Nothing is inferred about UI
gestures or human listening from a JSON summary.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import sys
import uuid
from quality_model_lineage import validate as validate_model_lineage
from verify_quality_audio import validate as validate_quality_audio
from ableton_secondary_evidence import validate as validate_secondary_profile
from steinberg_conformance_evidence import validate as validate_steinberg_variants
from installer_release_evidence import validate as validate_installer_provenance

VERSION = "0.3.0"
MODES = ("Ducking", "Spectral", "Multiband", "Morph", "Groove", "Transient")
CHECKS = ("legacy", "dsp", "quality", "audio", "ui", "ai", "bridge", "parity", "host", "steinberg", "pluginval")
LABELS = {"legacy": "Suite nativa storica", "dsp": "DSP 0.3", "ui": "UI nativa 0.3",
          "quality": "Regressioni qualità audio", "audio": "Render musicali kick/basso",
          "ai": "AI capture/contratto", "bridge": "Bridge AI reale", "parity": "Parità e held-out",
          "host": "Contratto host VST3", "steinberg": "Validator Steinberg", "pluginval": "pluginval"}
CORE = ("Source/PluginProcessor.cpp", "Source/PluginProcessor.h", "Source/Engines.h",
        "Source/BehaviorLibrary.h", "Source/AssistTrainingData.h")
INSTALLER_CHECKS = ("installPrevious", "upgradeFrom020", "reinstall", "uninstall", "legacyPresetPreserved",
                    "modernPresetPreserved", "binaryMatches", "allPayloadFilesMatch", "allManagedPayloadRemoved", "productionUntouched")
DAW_CHECKS = ("fourPreferencesRestoredExactly", "baselinePreferenceSnapshotsIntact", "productionBundleUnchanged",
              "qaProcessClosed", "noAdditionalLiveProcesses", "initialLiveProcessesPreserved",
              "initialProcessStateRestored", "targetReleaseBinaryUnchanged",
              "originalQASetUnchanged", "originalQASetSnapshotIntact", "schema3StateCaptured",
              "schema3StatesComplete", "schema3ModernEngineCaptured")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def text(path):
    raw = path.read_bytes()
    return raw.decode("utf-16") if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else raw.decode("utf-8-sig")


def number(value, low=-math.inf, high=math.inf):
    require(isinstance(value, (float, int)) and not isinstance(value, bool), "Expected a numeric evidence field")
    require(math.isfinite(value) and low <= value <= high, "Nonfinite or invalid numeric evidence")
    return value


def fmt(value, digits=7):
    return "n/d" if value is None else f"{number(value):.{digits}g}"


def md(value):
    return str(value).replace("|", "\\|").replace("\r", " ").replace("\n", " ")


class Evidence:
    def __init__(self, root):
        require(root.is_absolute() and root.is_dir(), "--root must be an existing absolute project directory")
        self.root = root.resolve()
        self.hashes = {}

    def path(self, value, exists=True):
        supplied = Path(value)
        require(".." not in supplied.parts, "Relative parent path is not an evidence reference")
        path = supplied if supplied.is_absolute() else self.root / supplied
        for part in (path, *path.parents):
            try:
                info = part.lstat()
            except FileNotFoundError:
                continue
            require(not stat.S_ISLNK(info.st_mode) and not (getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)),
                    f"Evidence/output reparse path forbidden: {part}")
        path = path.resolve()
        require(path != self.root and path.is_relative_to(self.root), f"Evidence escapes project: {path}")
        if exists:
            require(path.is_file(), f"Evidence missing: {path}")
        return path

    def sha(self, value):
        path = self.path(value)
        current = digest(path)
        if path in self.hashes:
            require(self.hashes[path] == current, f"Evidence changed while collecting: {path}")
        self.hashes[path] = current
        return current

    def checked(self, value, expected):
        require(isinstance(expected, str) and re.fullmatch(r"[a-fA-F0-9]{64}", expected), f"Invalid recorded hash: {value}")
        require(self.sha(value) == expected.lower(), f"Evidence hash mismatch: {value}")

    def json(self, value):
        path = self.path(value)
        self.sha(path)
        return json.loads(text(path))

    def link(self, value, label=None):
        path = self.path(value)
        relative = path.relative_to(self.root).as_posix()
        target = relative[5:] if relative.startswith("docs/") else "../" + relative
        return f"[{md(label or path.name)}]({target})"

    def stable(self):
        for path, expected in self.hashes.items():
            require(digest(path) == expected, f"Evidence changed before report write: {path}")


def six(rows, label):
    require(isinstance(rows, list) and len(rows) == 6 and {row.get("mode") for row in rows} == set(range(6)), f"{label}: six distinct modes 0..5 required")
    return sorted(rows, key=lambda row: row["mode"])


def collect(e, pre_daw):
    binary = "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3"
    setup = f"releases/OpenDuck-Pro-X-{VERSION}-Windows-x64-Setup.exe"
    proof_path = f"build/non-daw-v{VERSION}.json"
    proof = e.json(proof_path)
    require(proof.get("version") == VERSION and proof.get("complete") is True, "Non-DAW run is incomplete or belongs to another version")
    e.checked(binary, proof.get("releaseBinarySHA256"))
    require(set(proof.get("checks", {})) == set(CHECKS), "All eleven final non-DAW checks are required")
    require(proof.get("productionInstallPerformed") is False, "This report expects no production installation by the verification runner")
    for variant in validate_steinberg_variants(e.root, proof).values():
        e.checked(variant['log'], variant['logSHA256'])
        e.checked(variant['command'][0], variant['executableSHA256'])
    for key in CHECKS:
        row = proof["checks"][key]
        require(row.get("passed") is True and row.get("exitCode") == 0, f"Unresolved final check: {key}")
        e.checked(row["log"], row["logSHA256"])
        command = row.get("command", [])
        require(command and "--allow-untrained" not in command, f"Missing or development-only command: {key}")
        e.checked(command[0], row["executableSHA256"])
        log = text(e.path(row["log"]))
        require(not re.search(r"^FAIL\s", log, re.M), f"Failed assertion in final {key} log")
        if key == "pluginval":
            require(log.rstrip().endswith("SUCCESS") and re.search(r"Strictness level:\s*10\b", log), "pluginval strictness10 SUCCESS required")
            require("--skip-gui-tests" not in command, "GUI tests cannot be silently omitted")
        elif key == "audio":
            require(row.get("count") == 35 and re.search(r"RENDERED\s+35\s+cases;", log), "All35 musical renders required")
        else:
            pattern = r"Result:\s*(\d+)\s+tests passed,\s*(\d+)\s+tests failed" if key == "steinberg" else r"RESULT\s+(\d+)\s+passed,\s*(\d+)\s+failed"
            matches = re.findall(pattern, log)
            require(matches and int(matches[-1][0]) == row.get("count") and int(matches[-1][1]) == 0 and row["count"] > 0,
                    f"Summary/count mismatch in {key}")
    sources = proof.get("sourceSHA256", {})
    require(set(CORE) | {"Source/NeuralModels.h"} <= sources.keys(), "Final source fingerprints are incomplete")
    for relative, expected in sources.items():
        e.checked(relative, expected)
    for field in ("inputEvidenceSHA256", "generatedEvidenceSHA256"):
        for relative, expected in proof.get(field, {}).items():
            e.checked(relative, expected)
    training_path = "docs/research-v0.3.0/training.json"
    corpus_path = "docs/research-v0.3.0/teacher-final/manifest.json"
    golden_path = "docs/research-v0.3.0/golden.csv"
    for relative in (training_path, corpus_path, golden_path):
        e.checked(relative, proof.get("inputEvidenceSHA256", {}).get(relative))
    training = e.json(training_path)
    models = six(training.get("models"), "Training")
    e.checked("Source/NeuralModels.h", training.get("header_sha256"))
    require(re.search(r"\btrained\s*=\s*true\s*;", text(e.path("Source/NeuralModels.h"))), "Model placeholder cannot be reported as trained")
    require(len({row.get("model_sha256_float32") for row in models}) == 6, "Six distinct model weight sets required")
    for row in models:
        require((row.get("train_scenes"), row.get("validation_scenes"), row.get("test_scenes")) == (96, 24, 24), "Grouped 96/24/24 split required")
        require(re.fullmatch(r"[a-fA-F0-9]{64}", str(row.get("model_sha256_float32", ""))), "Missing per-model weight hash")
        number(row["heldout_active_control_MAE"], 0, 1)
        number(row["heldout_shape_accuracy"], 0, 1)
    corpus = e.json(corpus_path)
    require(all(corpus.get(key) is True for key in ("passed", "teacher_unchanged", "sources_unchanged")), "Frozen teacher corpus is incomplete or changed")
    require(corpus.get("scenes_per_mode") == 144 and corpus.get("candidates_per_scene") == 8, "Actual 144-scene/8-candidate corpus required")
    runs = six(corpus.get("runs"), "Teacher runs")
    datasets = six(training.get("sources"), "Training inputs")
    for row, source in zip(runs, datasets):
        relative = f"docs/research-v0.3.0/teacher-final/mode-{row['mode']}.csv"
        require(row.get("passed") is True and row.get("rows") == 144 and source.get("sha256") == row.get("csv_sha256"), "Teacher/training CSV lineage mismatch")
        e.checked(relative, row["csv_sha256"])
    for relative, expected in corpus.get("source_sha256", {}).items():
        e.checked("docs/research-v0.3.0/teacher-final/frozen-source/" + relative, expected)
    for relative in CORE:
        e.checked(relative, corpus["source_sha256"][relative])
    stereo_path = f"build/ai-quality-lineage-v{VERSION}.json"
    stereo = validate_model_lineage(e.root)
    require(e.json(stereo_path) == stereo, "R2 provenance evidence differs from current frozen runtime")
    e.checked(stereo_path, proof.get("generatedEvidenceSHA256", {}).get(stereo_path))
    quality_path = f"build/quality-audio-v{VERSION}.json"
    quality = validate_quality_audio(e.root, e.root / f"build/quality-audio-v{VERSION}")
    require(e.json(quality_path) == quality, "The35 musical renders changed after verification")
    e.checked(quality_path, proof.get("generatedEvidenceSHA256", {}).get(quality_path))
    heldout_path = f"build/ai-heldout-v{VERSION}.json"
    e.checked(heldout_path, proof.get("generatedEvidenceSHA256", {}).get(heldout_path))
    heldout = e.json(heldout_path)
    require(heldout.get('modelVersion') == 'OpenDuck-native-teacher-0.3.0-r2', 'Heldout evidence belongs to another model revision')
    heldout_rows = six(heldout.get("modes"), "Actual-DSP held-out")
    for row in heldout_rows:
        n = number(row["heldoutScenes"], 12)
        for key in ("baselineObjectiveMean", "modelOnlyObjectiveMean", "acceptedObjectiveMean"):
            number(row[key], 0)
        for key in ("modelOnlyWins", "modelAndRefinementWins"):
            number(row[key], 0, n)
        require(row["acceptedObjectiveMean"] <= row["baselineObjectiveMean"] and row.get('allAcceptedPreservationGuardsPassed') is True,
                "Accepted held-out result worsens baseline or fails preservation guards")
    measures_path = "build/visual-qa-v0.3.0/dsp030-measures.json"
    e.checked(measures_path, proof.get("generatedEvidenceSHA256", {}).get(measures_path))
    measures = e.json(measures_path)
    require(measures.get("failed") == 0 and measures.get("passed") == proof["checks"]["dsp"]["count"], "DSP measures do not belong to the passing final suite")
    require(measures.get("latencySamples48000") == 12112, "Prepared PDC measure changed")
    require(len(measures.get("sectorWetResidualRatios", [])) == 6, "Six sector depth measures required")
    for value in measures["sectorWetResidualRatios"]:
        number(value, 0)
    number(measures["halfAmountMeasuredGain"], 0)
    number(measures["amountZeroNullRms"], 0)
    number(measures["worstAutomationBlockMs"], 0)
    installer_path = f"build/installer-qa-results-v{VERSION}.json"
    installer = e.json(installer_path)
    require(installer.get("version") == VERSION and all(installer.get(key) is True for key in INSTALLER_CHECKS), "Isolated installer QA is unresolved")
    require(str(installer.get("builtSHA256", "")).lower() == e.sha(binary), "Installer QA tested another DLL")
    provenance = validate_installer_provenance(e.root)
    e.checked(installer_path, provenance['qaReportSHA256'])
    e.checked('build/installer-production-v0.3.0.json', provenance['productionManifestSHA256'])
    e.sha(setup)
    require(e.path(setup).stat().st_mtime >= e.path(binary).stat().st_mtime, "Production setup predates final DLL")
    result = dict(binary=binary, setup=setup, proof_path=proof_path, proof=proof, training_path=training_path,
                  training=training, models=models, corpus_path=corpus_path, corpus=corpus, golden_path=golden_path,
                  stereo_path=stereo_path, stereo=stereo, quality_path=quality_path, quality=quality, heldout_path=heldout_path, heldout=heldout,
                  heldout_rows=heldout_rows, measures_path=measures_path, measures=measures,
                  installer_path=installer_path, installer=installer, daw=None, exports=[])
    if not pre_daw:
        result["daw"] = collect_daw(e, result)
    return result


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


def collect_daw(e, facts):
    qa = "work/Ableton-QA-v0.3.0/"
    manual = f"docs/Ableton-QA-v{VERSION}.md"
    require(e.path(manual).stat().st_size > 0, "Actual manually written Ableton QA report required")
    e.sha(manual)
    post = e.json(qa + "POST-RUN-REPORT.json")
    require(post.get("version") == VERSION and post.get("postRunChecksPassed") is True and
            all(post.get("checks", {}).get(key) is True for key in DAW_CHECKS), "Actual Ableton post-run restoration/state checks unresolved")
    require(post.get("releaseBinary", {}).get("sha256") == e.sha(facts["binary"]), "Ableton tested another DLL")
    e.checked(qa + "baseline.json", post.get("baselineSHA256"))  # Metadata only; preference files are never opened here.
    initial_pids = validate_live_restoration(e.json(qa + "baseline.json"), post)
    module = e.json(qa + "LOADED-MODULES.json")
    require(module.get("version") == VERSION and module.get("proofPassed") is True and
            module.get("checks", {}).get("allLoadedOpenDuckModulesAreFinalProjectBinary") is True and
            module.get("expectedSHA256") == e.sha(facts["binary"]) and module.get("baselineSHA256") == post["baselineSHA256"], "Actual loaded module proof does not match final binary/baseline")
    require(type(module.get("processId")) is int and module["processId"] > 0 and module["processId"] not in initial_pids,
            "Loaded module proof must identify the new QA instance, never an original open Live process")
    require(isinstance(module.get('hostProductVersion'), str) and re.match(r'^12\.', module['hostProductVersion']),
            'The actual Ableton12 host version is missing')
    secondary = validate_secondary_profile(e.root, module, post)
    if secondary:
        e.checked(secondary['path'], secondary['sha256'])
    automation = e.json(qa + "AUTOMATION-AUDIT.json")
    state = post.get("savedStateEvidence", {})
    require(automation.get("setSHA256") == state.get("qaSetSHA256"), "Automation audit belongs to another QA set")
    e.checked(state["qaSet"], state["qaSetSHA256"])
    require(automation.get("modernPluginCount", 0) >= 2 and automation.get("modernAutomatedLaneCount", 0) > 0 and automation.get("modernNonnegativeTimeEventCount", 0) > 0, "Saved modern-instance automation evidence missing")
    modern = [row for row in automation.get("plugins", []) if row.get("modernMask") == 63]
    require(len(modern) >= 2 and all(row.get("allRecordedBindingsMatchJuce") is True for row in modern), "Unverified modern automation bindings")
    optional = ["EXPORT-COMPARISON.json", "AUDIO-EXPORT-COMPARISON.json", "EXPORT-COMPARISON-v0.3.0.json"]
    selection_path = qa + "EVIDENCE-FILES.json"
    if e.path(selection_path, exists=False).is_file():
        selection = e.json(selection_path)
        extra = selection.get("exportReports", [])
        require(isinstance(extra, list) and all(isinstance(item, str) for item in extra), "Invalid exportReports selection")
        optional.extend(extra)
    for name in dict.fromkeys(optional):
        require(Path(name).name == name and name.lower().endswith(".json"), "Export report must be a named JSON directly inside QA root")
        relative = qa + name
        if not e.path(relative, exists=False).is_file():
            continue
        export = e.json(relative)
        require(export.get("schema") == "openduck-ableton-export-comparison-v1", "Unknown export comparison schema")
        require(export.get("passed") is True and export.get("checks") and all(value is True for value in export["checks"].values()), "An available export comparison has unresolved checks")
        for key in ("dry", "wet"):
            e.checked(export[key]["path"], export[key]["sha256"])
        verifier = "tools/compare_ableton_exports_v030.py"
        facts["exports"].append({"path": relative, "data": export, "verifier": verifier, "verifierSHA256": e.sha(verifier)})
    return {"manual": manual, "post": post, "module": module, "automation": automation, "qa": qa, "secondary": secondary}


def render(e, f, pre_daw):
    proof, measures = f["proof"], f["measures"]
    lines = [f"# OpenDuck Pro X {VERSION} — rapporto di verifica", "",
             "Generato da evidenze effettive il " + datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC") + ".", "",
             "**Gate non-DAW e QA installer superati; sessione finale Ableton pendente e autorizzata.**" if pre_daw else
             "**Gate non-DAW e QA installer superati; evidenze della sessione finale Ableton e del ripristino verificate.**", "",
             "Il rapporto usa conteggi e hash dei file controllati. Le prove numeriche non costituiscono un voto percettivo né una dichiarazione automatica di ascolto umano o di gesti UI osservati.", "",
             "## Identità e provenienza", "", "| Artefatto | SHA256 |", "| --- | --- |"]
    identities = [("DLL VST3 finale", f["binary"]), ("Setup di produzione", f["setup"]),
                  ("Manifest non-DAW", f["proof_path"]), ("Header dei pesi NeuralModels.h", "Source/NeuralModels.h"),
                  ("Training", f["training_path"]), ("Manifest corpus congelato", f["corpus_path"]),
                  ("Golden inference", f["golden_path"]), ("Held-out DSP", f["heldout_path"]),
                  ("Provenienza DSP/guard/modelli R2", f["stereo_path"]), ("Misure35 render musicali", f["quality_path"]), ("QA installer", f["installer_path"])]
    for label, path in identities:
        lines.append(f"| {md(label)} | `{e.sha(path)}` |")
    lines += ["", "Il codice corrispondente completo è nell'archivio Sorgenti; i file Source non vengono collegati come se fossero presenti nella sola cartella delle evidenze. Il manifest non-DAW contiene gli hash di Source e tests. Nucleo audio:", "", "| Sorgente | SHA256 |", "| --- | --- |"]
    lines.extend(f"| `{path}` | `{proof['sourceSHA256'][path]}` |" for path in CORE)
    lines += ["", "## Gate finali effettivi", "", "| Verifica | Esito | Evidenza |", "| --- | --- | --- |"]
    for key in CHECKS:
        row = proof["checks"][key]
        outcome = "35 render; gate null, PDC, finitezza e clipping superati; exit0" if key == "audio" else "Strictness 10, SUCCESS, exit 0; GUI inclusa" if key == "pluginval" else f"{row['count']} passed, 0 failed; exit 0"
        lines.append(f"| {LABELS[key]} | {outcome} | {e.link(row['log'])} |")
    for label, row in proof['checks']['steinberg']['variants'].items():
        lines.append(f"| Steinberg {md(label)} | {row['count']} passed, 0 failed; exit0 | {e.link(row['log'])} |")
    lines.append(f"| QA installer | 0.2.0 → 0.3.0, reinstall e uninstall superati; preset preservati; produzione invariata | {e.link(f['installer_path'])} |")
    lines.append(f"| Compilazione setup professionale | Input identici al QA verificati per hash; nessuna installazione di produzione | {e.link('build/installer-production-v0.3.0.json')} |")
    lines.append("| Ableton Live 12 | PENDENTE, autorizzato dopo gli altri gate | Nessuna sessione attestata in modalità pre-DAW |" if pre_daw else
                 f"| {md(f['daw']['module']['processName'])} {md(f['daw']['module']['hostProductVersion'])} | Ripristino, stato salvato, modulo e audit verificati; osservazioni UI/audio nel verbale manuale | {e.link(f['daw']['manual'])} |")
    if not pre_daw and f['daw'].get('secondary'):
        lines.append(f"| Profilo beta supplementare | Ripristino dei quattro file verificato; questo documento attesta soltanto il rollback | {e.link(f['daw']['secondary']['path'])} |")
    lines += ["", "Nessuna nuova installazione di produzione è stata eseguita dagli strumenti di verifica. Il setup è destinato all'installazione dell'utente. I log sono distribuiti sotto `Logs/`, le evidenze native più pesanti sotto `Preview/`; i documenti del corpus/training sono sotto `docs/research-v0.3.0/`.", "",
              "## Sei modelli locali: dati e risultati distinti", "",
              "Sei MLP FP32 96–24–16–43. Per ogni settore: 96 scene di training, 24 di validazione e 24 di test, separate per famiglia di sintesi, senza dividere crop della medesima sorgente fra i gruppi. Il corpus usa 144 scene e 8 candidati per settore, renderizzati dal teacher C++ sul DSP reale. Non usa label ottenuti da un renderer Python sostitutivo.", "",
              f"Fonti: {e.link(f['training_path'])}, {e.link(f['corpus_path'])}, {e.link(f['golden_path'])}, {e.link(f['heldout_path'])}.", "",
              "| Settore | Train/val/test | MAE controlli attivi sul test | Accuratezza shape sul test |", "| --- | --- | ---: | ---: |"]
    for row in f["models"]:
        lines.append(f"| {MODES[row['mode']]} | {row['train_scenes']}/{row['validation_scenes']}/{row['test_scenes']} | {fmt(row['heldout_active_control_MAE'])} | {fmt(row['heldout_shape_accuracy'])} |")
    lines += ["", "MAE e accuratezza shape misurano l'export sui parametri, non il gradimento del suono. Le seguenti prove audio usano il DSP compilato su famiglie held-out; un obiettivo inferiore è migliore secondo il criterio numerico del renderer:", "",
              "| Settore | Scene audio | Vittorie modello solo | Vittorie modello + refinement | Obiettivo baseline | Obiettivo modello solo | Obiettivo accettato |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in f["heldout_rows"]:
        lines.append(f"| {MODES[row['mode']]} | {row['heldoutScenes']} | {row['modelOnlyWins']} | {row['modelAndRefinementWins']} | {fmt(row['baselineObjectiveMean'])} | {fmt(row['modelOnlyObjectiveMean'])} | {fmt(row['acceptedObjectiveMean'])} |")
    attack_cases = sum(scene.get('baseline', {}).get('protectedAttacks', 0) > 0
                       for row in f['heldout_rows'] for scene in row.get('scenes', []))
    scene_count = sum(len(row.get('scenes', [])) for row in f['heldout_rows'])
    lines += ['', f'Scene held-out con attacchi main non coincidenti misurati: {attack_cases}/{scene_count}. '
              'La copertura degli attacchi va distinta da quiet, bande e recupero: '
              f'il controllo dedicato di rifiuto è documentato nella suite {e.link(proof["checks"]["ai"]["log"])}; '
              'un campo protectedAttacks pari a zero non viene presentato come prova aggiuntiva degli attacchi.']
    lines += ["", "Il risultato accettato include la baseline quando non c'è miglioramento: le astensioni impediscono il peggioramento, ma non sono vittorie del modello. Il refinement è una ricerca limitata attorno all'inferenza; i suoi risultati non vengono attribuiti al solo modello. Nessun punteggio percettivo o confronto di ascolto con plugin commerciali è dedotto da questi numeri.", "",
              "| Settore | SHA256 float32 dei pesi dichiarato dal training |", "| --- | --- |"]
    lines.extend(f"| {MODES[row['mode']]} | `{row['model_sha256_float32']}` |" for row in f["models"])
    lines += ["", "## Provenienza R2 e preservazione audio", "",
              f"Evidenza: {e.link(f['stereo_path'])}. Nuovo corpus generato dopo il fix della legge gain e della maschera stereo. DSP, renderer e guard del teacher coincidono byte per byte con i sorgenti di runtime congelati; i sei modelli sono stati riaddestrati con seed e corpus R2. Non si attribuisce al vecchio corpus R1 la correzione del nuovo DSP.", "",
              f"I35 render musicali ({e.link(f['quality_path'])}) usano kick e basso separati, poi sommati, ogni settore e la catena completa. I gate verificano assenza di campioni non finiti/clipping, PDC, null senza intervento e recupero nelle finestre aperte. I WAV e i risultati RMS sono conservati senza normalizzazione: la riduzione durante il ducking resta intenzionale e non viene chiamata perdita zero. Non costituiscono una certificazione percettiva.", "",
              "## Misure audio del build finale", "", f"Fonte: {e.link(f['measures_path'])}.", "",
              "| Grandezza | Valore misurato |", "| --- | ---: |",
              f"| PDC preparato a 48 kHz | {measures['latencySamples48000']} campioni = {fmt(measures['latencySamples48000']/48, 9)} ms |",
              f"| Null RMS Amount 0 contro ingresso ritardato | {fmt(measures['amountZeroNullRms'])} |",
              f"| Guadagno Amount 50 su curva chiusa | {fmt(measures['halfAmountMeasuredGain'])} |",
              f"| Peggior callback della prova di automazione | {fmt(measures['worstAutomationBlockMs'])} ms |", "",
              "| Settore | Residuo wet del plateau chiuso | Attenuazione del rapporto |", "| --- | ---: | ---: |"]
    for mode, value in enumerate(measures["sectorWetResidualRatios"]):
        db = f"{fmt(20*math.log10(value))} dB" if value > 0 else "zero numerico nella prova"
        lines.append(f"| {MODES[mode]} | {fmt(value)} | {db} |")
    lines += ["", "La profondità del plateau non descrive ogni curva, frequenza o segnale musicale. Il callback è misurato su 24 blocchi da 257 campioni a 48 kHz, tutti i sei settori e automazione dei crossover/slope: deadline 5,35417 ms; il valore osservato non garantisce il medesimo carico su ogni CPU.", "",
              "Il PDC fisso è `ceil(sampleRate × 0,215) + 6 × 128 + 1024`: 11.274 campioni/255,646 ms a 44,1 kHz; 12.112/252,333 ms a 48 kHz; 22.432/233,667 ms a 96 kHz. Comprende offset anticipato massimo, lookahead, sei stadi FIR e STFT. Resta anche con lookahead zero, bypass o settori OFF: è una latenza elevata per il monitoring diretto. Le prove bypass/dry sono allineate al PDC, non confrontate con un ingresso non ritardato.", "",
              "## Ambito funzionale e limiti dichiarati", "",
              "700 comportamenti totali distribuiti fra i sei settori, sette categorie per settore e 16 shape; il catalogo non equivale a 700 preset per ciascun settore. INIT usa il primo shape, Amount/MIX100 e punti OFF. I punti e il disegno libero sono opzionali. Stato di progetto e riapertura dell'editor restano distinti da una nuova istanza INIT.", "",
              "Capture AI di 5 o 10 secondi e inferenza/refinement funzionano localmente in C++/CPU, senza Python, download o servizio cloud nel VST. Il DSP supporta i trigger MIDI; l'AI si astiene senza eventi MIDI registrati nella capture. Si astiene anche sul clock Hz/Repeat libero quando la fase non è riproducibile e se il contesto cambia. Le sei assistenze restano isolate; Group deve essere Off per l'applicazione AI.", "",
              "I progetti schema1/2 mantengono il ramo storico e il suo PDC finché viene scelto l'upgrade esplicito al routing seriale0.3. Il supporto di funzioni ispirate a Kickstart/Duck non dichiara identità dei loro algoritmi, importazione di librerie proprietarie o replica completa di ogni operazione Duck2. Le osservazioni UI/audio del DAW appartengono soltanto al verbale manuale effettivamente disponibile."]
    if not pre_daw:
        daw = f["daw"]
        process_description = (
            "La sola istanza QA risulta chiusa; tutti i processi Live iniziali risultano ancora presenti con gli stessi PID e orari di avvio, senza processi aggiuntivi. Questo confronto non attesta da solo il contenuto del progetto originale aperto: le osservazioni appartengono al verbale manuale."
            if daw["post"]["process"]["initiallyRunning"] else
            "Live risulta chiuso come nello stato iniziale; nessun processo QA rimane aperto.")
        lines += ["", "## Sessione finale Ableton e ripristino", "",
                  f"Verbale manuale: {e.link(daw['manual'])}. Il presente generatore non riassume gesti UI né dichiara ascolti che non può ricavare dalle prove numeriche.", "",
                  "Le quattro preferenze autorizzate risultano ripristinate esattamente e gli snapshot originali sono intatti. " + process_description + " Set QA originale/copia di baseline, bundle di produzione e DLL finale risultano invariati secondo i check del post-run. Nessun contenuto delle preferenze viene aperto da questo generatore o incluso nel report.", "",
                  "| Evidenza DAW | SHA256 |", "| --- | --- |"]
        for name in ("POST-RUN-REPORT.json", "LOADED-MODULES.json", "AUTOMATION-AUDIT.json"):
            path = daw["qa"] + name
            lines.append(f"| {e.link(path)} | `{e.sha(path)}` |")
        lines += [f"| {e.link(daw['manual'])} | `{e.sha(daw['manual'])}` |", "",
                  f"Stati schema3 salvati: {daw['post']['savedStateEvidence']['schema3ChunkCount']}. Modulo caricato coerente con lo stesso DLL finale e lo stesso baseline. L'audit automazione identifica il medesimo set, con {daw['automation']['modernAutomatedLaneCount']} lane moderne e {daw['automation']['modernNonnegativeTimeEventCount']} eventi a tempo non negativo; i binding registrati sono verificati."]
        if f["exports"]:
            lines += ["", "### Esportazioni audio disponibili", "",
                      "Confronti numerici dry/wet senza shifting, resampling o normalizzazione. Non sono una misura del PDC e non costituiscono un giudizio di ascolto."]
            for export in f["exports"]:
                data = export["data"]
                lines += ["", f"Report: {e.link(export['path'])}, SHA256 `{e.sha(export['path'])}`.",
                          f"Sorgente del comparatore attualmente verificata (`{export['verifier']}`): `{export['verifierSHA256']}`. Questo hash identifica il codice disponibile per riprodurre il confronto, non una firma retroattiva dell'esecuzione.",
                          f"Dry SHA256 `{data['dry']['sha256']}`; wet SHA256 `{data['wet']['sha256']}`. Tutti i check registrati sono veri; sorgenti WAV ancora corrispondenti agli hash.",
                          f"Differenza RMS relativa: {fmt(data.get('metrics', {}).get('differenceToDryRms'))}; cicli completi: {md(data.get('metrics', {}).get('completeCycles', 'n/d'))}; periodo mediano: {fmt(data.get('metrics', {}).get('medianPeriodMs'))} ms."]
        else:
            lines += ["", "Nessun report di confronto esportazioni presente fra i nomi riconosciuti o nella selezione `exportReports`; nessuna prova di esportazione viene inventata."]
    else:
        lines += ["", "## Gate DAW ancora aperto", "",
                  "La singola sessione finale Ableton è autorizzata ma ancora pendente. Non sono attestati avvio, gesti UI, esportazioni, ascolti o ripristino delle preferenze. Dopo il verbale manuale e le prove post-run, rieseguire il generatore senza `--pre-daw`."]
    lines += ["", "I backup privati delle preferenze, i restore snapshot, i log Live e i crash report restano esclusi dalla distribuzione. Gli archivi e il manifesto SHA256 di consegna vengono verificati separatamente dal packaging; questo rapporto non dichiara un'installazione utente già eseguita.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Absolute project directory containing the final passing evidence")
    parser.add_argument("--pre-daw", action="store_true", help="Close only non-DAW/installer evidence; explicitly leave final Ableton QA pending")
    args = parser.parse_args()
    try:
        evidence = Evidence(args.root)
        facts = collect(evidence, args.pre_daw)
        rendered = render(evidence, facts, args.pre_daw)
        destination = evidence.path(f"docs/VERIFICATION-v{VERSION}.md", exists=False)
        require(destination.parent.is_dir(), "Existing docs directory required")
        evidence.stable()
        temporary = evidence.path(destination.with_name(destination.name + "." + uuid.uuid4().hex + ".tmp"), exists=False)
        try:
            with temporary.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(rendered)
            os.replace(temporary, destination)
        finally:
            if temporary.exists():
                temporary.unlink()
        print(json.dumps({"report": str(destination), "sha256": digest(destination), "preDaw": args.pre_daw,
                          "nativeCount": facts["proof"]["checks"]["legacy"]["count"], "models": 6,
                          "abletonEvidenceVerified": not args.pre_daw}, indent=2))
        return 0
    except Exception as error:
        print(f"Verification report refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
