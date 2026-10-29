"""Summarize actual guarded teacher pilot CSVs; never tune thresholds or train models."""
from pathlib import Path
import argparse
import csv
import hashlib
import json


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pilot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pilot = args.pilot.resolve()
    manifest_path = pilot / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if manifest.get("pipeline_revision") != "quality-r2" or manifest.get("teacher_train_only") is not True:
        raise ValueError("An actual training-family-only quality-r2 pilot is required")
    if args.output.exists():
        raise FileExistsError("Do not overwrite an existing pilot report")
    runs = {r["mode"]: r for r in manifest.get("runs", [])}
    modes = []
    for mode in range(6):
        path = pilot / f"mode-{mode}.csv"
        with path.open(newline="", encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
        if mode not in runs or sha(path) != runs[mode].get("csv_sha256"):
            raise ValueError("CSV differs from completed pilot manifest")
        if any(int(row["split"]) != 0 for row in rows):
            raise ValueError("Pilot leaked validation or test families")
        candidate_count = manifest["candidates_per_scene"]
        if any(int(r["eligibleCandidates"]) + int(r["rejectedCandidates"]) != candidate_count for r in rows):
            raise ValueError("Guard outcome counts do not match actual candidate renders")
        improved = [r for r in rows if float(r["teacherObjective"]) < float(r["baselineObjective"]) - 1e-6]
        modes.append({
            "mode": mode, "rows": len(rows), "families": sorted({int(r["scene"]) % 12 for r in rows}),
            "elapsedSeconds": runs[mode]["elapsed_seconds"], "inputSourceAttempts": sum(int(r["attempt"]) for r in rows),
            "inputSourceMaxAttempts": max(int(r["attempt"]) for r in rows),
            "eligibleCandidates": sum(int(r["eligibleCandidates"]) for r in rows),
            "guardRejectedCandidates": sum(int(r["rejectedCandidates"]) for r in rows),
            "scenesWithEligibleCandidate": sum(int(r["eligibleCandidates"]) > 0 for r in rows),
            "scenesWithMeasuredImprovement": len(improved), "baselineRetained": len(rows) - len(improved),
            "baselineObjectiveMean": sum(float(r["baselineObjective"]) for r in rows) / len(rows),
            "selectedObjectiveMean": sum(float(r["teacherObjective"]) for r in rows) / len(rows),
            "worstAcceptedQuietDb": min((float(r["selectedQuietDb"]) for r in improved), default=None),
            "worstAcceptedRecoveryDb": min((float(r["selectedRecoveryDb"]) for r in improved), default=None),
            "csvSHA256": sha(path), "logSHA256": sha(pilot / f"mode-{mode}.log"),
        })
    result = {"passed": manifest.get("passed") is True, "pipeline_revision": "quality-r2",
              "purpose": "Train-family-only feasibility/coverage calibration; no heldout tuning and no release models",
              "pilotManifestSHA256": sha(manifest_path), "elapsedSeconds": manifest.get("elapsed_seconds"),
              "sourceCoverage": "Each retained input has usable main/key, >=2 key hits and measured dry quiet/recovery coverage. Candidate eligibility additionally uses actual baseline-intent masks and shared absolute/relative guards. A retained baseline is not declared an admissible improvement.",
              "modes": modes}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
