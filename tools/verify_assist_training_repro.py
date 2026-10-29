"""Repeat R2 offline training in a fresh directory and compare all six models.

Never overwrites release weights, the original report or historical R1 evidence.
Python/NumPy are build-time dependencies only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Project containing tools/train_assist.py and Source/NeuralModels.h")
    parser.add_argument("--datasets", type=Path, required=True,
                        help="Completed quality-r2 corpus; historical R1 and pilot data are rejected")
    parser.add_argument("--output", type=Path, required=True,
                        help="Fresh directory for the repeated export and training log")
    parser.add_argument("--report", type=Path,
                        help="Default: ROOT/build/training-reproducibility-v0.3.0-r2.json")
    parser.add_argument("--training-report", type=Path,
                        help="Default: ROOT/docs/research-v0.3.0/training.json")
    parser.add_argument("--golden", type=Path,
                        help="Default: ROOT/docs/research-v0.3.0/golden.csv")
    parser.add_argument("--seed", type=int, default=43030)
    parser.add_argument("--steps", type=int, default=1800)
    args = parser.parse_args()
    if args.seed != 43030 or args.steps != 1800:
        parser.error("Release R2 requires seed43030 and 1800steps")
    root, datasets, destination = (p.resolve() for p in (args.root, args.datasets, args.output))
    report_path = (args.report or root / "build/training-reproducibility-v0.3.0-r2.json").resolve()
    original_report_path = (args.training_report or root / "docs/research-v0.3.0/training.json").resolve()
    original_golden_path = (args.golden or root / "docs/research-v0.3.0/golden.csv").resolve()
    original_header = root / "Source/NeuralModels.h"
    training_script = root / "tools/train_assist.py"
    corpus_path = datasets / "manifest.json"
    originals = (original_report_path, original_golden_path, original_header, training_script, corpus_path)
    if any(not p.is_file() for p in originals):
        raise ValueError("Missing original report, golden vectors, weights, training script or corpus manifest")
    if destination.exists():
        raise ValueError("Output must be a fresh directory; existing evidence is never overwritten")
    if report_path.exists() or report_path in originals:
        raise ValueError("Result report already exists or would replace original evidence")
    if any(destination == p or destination in p.parents for p in originals):
        raise ValueError("Output cannot contain original release evidence")

    first_report = json.loads(original_report_path.read_text(encoding="utf-8-sig"))
    corpus = json.loads(corpus_path.read_text(encoding="utf-8-sig"))
    original_hashes = {str(p): sha256(p) for p in originals}
    if (first_report.get("version") != "0.3.0-r2"
            or corpus.get("pipeline_revision") != "quality-r2"
            or corpus.get("teacher_train_only") is not False or corpus.get("passed") is not True
            or first_report.get("corpus_manifest_sha256") != sha256(corpus_path)
            or first_report.get("training_script_sha256") != sha256(training_script)
            or first_report.get("header_sha256") != sha256(original_header)):
        raise ValueError("Original evidence does not match the frozen release R2 contract")
    models = first_report.get("models", [])
    if (len(models) != 6 or {m.get("mode") for m in models} != set(range(6))
            or len({m.get("model_sha256_float32") for m in models}) != 6
            or any((m.get("train_scenes"), m.get("validation_scenes"), m.get("test_scenes")) != (96, 24, 24)
                   or m.get("seed") != 43030 + 1009 * m["mode"] for m in models)):
        raise ValueError("Original report must contain six distinct R2 models with required splits/seeds")

    destination.mkdir(parents=True)
    command = [sys.executable, str(training_script), "--datasets", str(datasets),
               "--output", str(destination / "NeuralModels.h"),
               "--report", str(destination / "training.json"),
               "--golden", str(destination / "golden.csv"),
               "--seed", str(args.seed), "--steps", str(args.steps)]
    with (destination / "training.log").open("w", encoding="utf-8") as stream:
        process = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=False)
    if process.returncode:
        raise RuntimeError("Repeated training failed; inspect the preserved training.log")
    pairs = [("NeuralModels.h", original_header, destination / "NeuralModels.h"),
             ("golden.csv", original_golden_path, destination / "golden.csv")]
    checks = {}
    for label, original, repeated in pairs:
        first, second = sha256(original), sha256(repeated)
        checks[label] = {"originalPath": str(original), "repeatedPath": str(repeated),
                         "originalSHA256": first, "repeatedSHA256": second, "identical": first == second}
    repeated_report = json.loads((destination / "training.json").read_text(encoding="utf-8"))
    models_equal = models == repeated_report.get("models")
    inputs_unchanged = all(sha256(Path(path)) == digest for path, digest in original_hashes.items())
    passed = all(row["identical"] for row in checks.values()) and models_equal and inputs_unchanged
    result = {"passed": passed, "version": "0.3.0-r2", "pipeline_revision": "quality-r2",
              "seed": args.seed, "steps": args.steps, "checks": checks,
              "allSixModelStatisticsExactlyEqual": models_equal, "originalInputsUnchanged": inputs_unchanged,
              "corpusManifestSHA256": sha256(corpus_path), "trainingScriptSHA256": sha256(training_script),
              "originalTrainingReportSHA256": sha256(original_report_path),
              "repeatedTrainingReportSHA256": sha256(destination / "training.json"),
              "repeatedTrainingLogSHA256": sha256(destination / "training.log"),
              "verificationScriptSHA256": sha256(Path(__file__)), "originalInputSHA256": original_hashes,
              "method": "Second independent Python/NumPy invocation over frozen quality-r2 CSVs; seed43030+mode*1009, 1800steps. Exact byte equality of generated C++ weights/golden CSV and exact equality of all six model statistic records. Deterministic export does not establish perceptual audio quality.",
              "command": command}
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
