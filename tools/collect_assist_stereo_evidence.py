"""Assemble measured stereo compatibility evidence; reject incomplete/stale audits."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--built-source-root", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest_path = args.corpus / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not all(manifest.get(key) is True for key in ("passed", "teacher_unchanged", "sources_unchanged")):
        raise ValueError("The teacher corpus has not closed with exact frozen provenance")
    summary = json.loads((args.audit / "summary.json").read_text(encoding="utf-8"))
    if summary.get("passed") is not True or len(summary.get("runs", [])) != 6:
        raise ValueError("All six completed audit runs are required")

    core = ["PluginProcessor.cpp", "PluginProcessor.h", "Engines.h", "BehaviorLibrary.h", "AssistTrainingData.h"]
    current = core + ["LocalAssist.h", "LocalAssist.cpp", "LocalAssistRender.h", "LocalAssistRender.cpp"]
    source_hashes = {}
    for name in current:
        relative = "Source/" + name
        expected = sha(args.source_root / relative)
        if sha(args.built_source_root / relative) != expected:
            raise ValueError("The audited source differs from final source: " + relative)
        if name in core and sha(args.corpus / "frozen-source" / relative) != expected:
            raise ValueError("The core DSP differs from the frozen teacher: " + relative)
        source_hashes[relative] = expected

    modes, evidence = [], {}
    for mode in range(6):
        report_path = args.audit / f"mode-{mode}-render.json"
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if report.get("failed") != 0 or report.get("passed") != 1:
            raise ValueError(f"Mode {mode} did not pass its actual-render audit")
        rows = report.get("modes", [])
        if len(rows) != 1 or rows[0].get("mode") != mode:
            raise ValueError("Wrong or duplicate audit mode")
        row = rows[0]
        for key in ("rows", "featureVectorsExactlyEqual", "actualMonoMetricsExactlyEqual", "archivedCsvObjectivesWithinSerializationTolerance"):
            if row.get(key) != 144:
                raise ValueError(f"Incomplete exact compatibility count for mode {mode}: {key}")
        if row.get("renderedBaselineAndSelected") is not True or row.get("actualAntiphaseStereoFinite") is not True:
            raise ValueError("The real DSP mono/stereo renders did not pass")
        modes.append(row)
        for suffix in ("json", "log"):
            filename = f"mode-{mode}-render.{suffix}"
            relative = "docs/research-v0.3.0/stereo-audit/" + filename
            target = args.source_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.audit / filename, target)
            evidence[relative] = sha(target)

    output = {
        "passed": True,
        "sourceSHA256": source_hashes,
        "corpusManifestSHA256": sha(manifest_path),
        "auditBinarySHA256": summary["binary_sha256"],
        "modes": modes,
        "evidenceSHA256": evidence,
        "method": "864 exact reconstructed mono feature vectors and actual-DSP baseline/selected metrics; 144 rows per sector; one real antiphase render per sector. No inference is used by this audit, so trained weight export does not change these source fingerprints. Separate trained heldout parity tests exercise inference on unseen mono and antiphase audio.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "modes": len(modes), "rows": sum(row["rows"] for row in modes), "output": str(args.output)}))


if __name__ == "__main__":
    main()
