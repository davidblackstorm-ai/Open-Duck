"""Repeat only the changed UI proof after a completed nine-check 0.3.0 run.

Allowed delta: tests/Revision030UiTests.cpp and its rebuilt executable. The VST3,
every other Source/tests file, all input/generated evidence and the eight other
executable/log fingerprints must remain identical. No DAW or installer is run.
The previous manifest and UI log are archived before the UI executable is run.
A failing UI run or PNG check clears complete; previous passes are never invented.
"""

from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time
import uuid
import zlib


VERSION = "0.3.0"
ALLOWED_SOURCE = "tests/Revision030UiTests.cpp"
ALL_CHECKS = {"legacy", "dsp", "ui", "ai", "bridge", "parity", "host", "steinberg", "pluginval"}
INPUTS = {"docs/research-v0.3.0/golden.csv", "docs/research-v0.3.0/training.json",
          "docs/research-v0.3.0/teacher-final/manifest.json"}
GENERATED = {"build/ai-heldout-v0.3.0.json", "build/visual-qa-v0.3.0/dsp030-measures.json",
             "build/visual-qa-v0.3.0/assist-bridge-summary.json"}
UI_PNGS = {"native-default.png", "native-trigger.png", "native-bands.png",
           "native-width-1010.png", "native-width-1326.png", "native-width-1767.png"}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def relative_file(root, relative):
    require(isinstance(relative, str) and "\\" not in relative, "Expected a forward-slash project-relative path")
    path = root / relative
    require(not Path(relative).is_absolute() and path.resolve().is_relative_to(root), f"Evidence path escapes project: {relative}")
    require(path.is_file(), f"Required file is missing: {relative}")
    return path


def source_hashes(root):
    result = {}
    for directory in ("Source", "tests"):
        require((root / directory).is_dir(), f"Missing source directory: {directory}")
        for path in sorted((root / directory).rglob("*")):
            if path.is_file():
                require(path.resolve().is_relative_to(root), f"Source link escapes project: {path}")
                result[path.relative_to(root).as_posix()] = sha(path)
    return result


def tracked_invariants(root, proof, expected_sources, expected_ui_executable, skip_ui_log=False):
    """Never rebaseline an unexpected difference; verify it against prior evidence."""
    require(source_hashes(root) == expected_sources, "Source/tests changed outside the permitted UI refresh")
    binary = root / "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3"
    require(binary.is_file() and sha(binary) == proof["releaseBinarySHA256"], "The release VST3 changed; run all nine checks again")
    for field, required in (("inputEvidenceSHA256", INPUTS), ("generatedEvidenceSHA256", GENERATED)):
        tracked = proof.get(field, {})
        require(set(tracked) == required, f"Unexpected or incomplete {field} coverage")
        for relative, expected in tracked.items():
            require(sha(relative_file(root, relative)) == expected, f"Tracked evidence changed: {relative}")
    for key, check in proof["checks"].items():
        command = check.get("command", [])
        require(command and isinstance(command[0], str), f"Missing command for {key}")
        executable = Path(command[0])
        require(executable.is_absolute() and executable.resolve().is_relative_to(root) and executable.is_file(),
                f"Executable outside this project or missing: {key}")
        expected = expected_ui_executable if key == "ui" else check["executableSHA256"]
        require(sha(executable) == expected, f"Executable changed: {key}")
        if key != "ui" or not skip_ui_log:
            require(sha(relative_file(root, check["log"])) == check["logSHA256"], f"Log changed: {key}")


def validate_png(path):
    """Check one complete PNG with CRC-valid chunks and no bytes after IEND."""
    data = path.read_bytes()
    require(data.startswith(PNG_SIGNATURE), f"Not a PNG: {path.name}")
    position, chunks, size = len(PNG_SIGNATURE), [], None
    while position < len(data):
        require(position + 12 <= len(data), f"Truncated PNG chunk: {path.name}")
        length, kind = struct.unpack_from(">I4s", data, position)
        end = position + 12 + length
        require(end <= len(data), f"PNG chunk exceeds file: {path.name}")
        payload = data[position + 8:position + 8 + length]
        crc = struct.unpack_from(">I", data, position + 8 + length)[0]
        require(zlib.crc32(kind + payload) & 0xFFFFFFFF == crc, f"PNG CRC mismatch: {path.name}")
        if not chunks:
            require(kind == b"IHDR" and length == 13, f"Missing PNG IHDR: {path.name}")
            size = struct.unpack_from(">II", payload)
            require(all(value > 0 for value in size), f"Invalid PNG dimensions: {path.name}")
        elif kind == b"IHDR":
            raise RuntimeError(f"Duplicate PNG header: {path.name}")
        chunks.append(kind)
        position = end
        if kind == b"IEND":
            require(length == 0 and b"IDAT" in chunks, f"Invalid PNG IEND/data: {path.name}")
            require(position == len(data), f"Trailing data or concatenated PNG: {path.name}")
            return {"width": size[0], "height": size[1], "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    raise RuntimeError(f"PNG has no IEND: {path.name}")


def read_log(path):
    data = path.read_bytes()
    return data.decode("utf-16") if data.startswith((b"\xff\xfe", b"\xfe\xff")) else data.decode("utf-8-sig", errors="replace")


def write_new(path, payload):
    with path.open("xb") as output:
        output.write(payload)


def atomic_json(path, value):
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    write_new(temporary, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    temporary.replace(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--reason", default="UI screenshot writer now truncates and replaces successful PNGs instead of appending; DSP and release DLL unchanged")
    parser.add_argument("--check-only", action="store_true", help="Validate the narrow refresh preconditions without writing or executing anything")
    args = parser.parse_args(argv)
    require(1 <= args.timeout <= 1200, "Timeout must be between1and1200seconds")
    root = args.root.resolve()
    manifest_path = root / f"build/non-daw-v{VERSION}.json"
    previous_bytes = manifest_path.read_bytes()
    previous_sha = hashlib.sha256(previous_bytes).hexdigest()
    previous = json.loads(previous_bytes.decode("utf-8-sig"))
    require(previous.get("version") == VERSION and previous.get("complete") is True,
            "A completed nine-check run is required before an incremental UI refresh")
    require(set(previous.get("checks", {})) == ALL_CHECKS, "The previous proof does not contain all nine checks")
    require(not previous.get("failure") and all(row.get("passed") is True and row.get("exitCode") == 0
                                               for row in previous["checks"].values()), "The previous proof contains a failed check")
    require(previous["checks"]["ui"].get("count") == 39, "Expected the established39-check UI suite")
    old_sources = previous.get("sourceSHA256", {})
    current_sources = source_hashes(root)
    require(set(current_sources) == set(old_sources), "Source/tests file inventory changed")
    changed = {name for name in old_sources if old_sources[name] != current_sources[name]}
    require(changed == {ALLOWED_SOURCE}, f"Only {ALLOWED_SOURCE} may differ; changed={sorted(changed)}")
    executable = root / "build/Revision030UiTests_artefacts/Release/Revision030UiTests.exe"
    preview = root / "build/visual-qa-v0.3.0"
    log_path = root / "build/ui-v0.3.0.log"
    require(previous["checks"]["ui"]["command"] == [str(executable), str(preview)], "Unexpected UI command")
    require(previous["checks"]["ui"]["log"] == log_path.relative_to(root).as_posix(), "Unexpected UI log path")
    require(executable.is_file() and preview.is_dir(), "Rebuild the UI executable and retain the QA preview directory first")
    ui_executable_sha = sha(executable)
    require(ui_executable_sha != previous["checks"]["ui"]["executableSHA256"], "The changed UI test executable has not been rebuilt")
    tracked_invariants(root, previous, current_sources, ui_executable_sha)
    if args.check_only:
        print("PASS narrow UI refresh preconditions; no files written and no executable run")
        return 0
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    archive = root / "work/v0.3.0/verification-attempts" / f"ui-refresh-{stamp}"
    archive.mkdir(parents=True, exist_ok=False)
    write_new(archive / manifest_path.name, previous_bytes)
    write_new(archive / log_path.name, log_path.read_bytes())
    before_png_times = {name: (preview / name).stat().st_mtime_ns for name in UI_PNGS if (preview / name).is_file()}
    proof = copy.deepcopy(previous)
    started_utc, started = utc(), time.monotonic()
    failure, png_evidence, code = None, {}, -1
    try:
        with log_path.open("wb") as output:
            try:
                job = subprocess.run([str(executable), str(preview)], cwd=root, stdout=output,
                                     stderr=subprocess.STDOUT, timeout=args.timeout,
                                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                code = job.returncode
            except subprocess.TimeoutExpired:
                failure = f"UI executable exceeded{args.timeout}seconds"
        text = read_log(log_path)
        result = re.findall(r"RESULT\s+(\d+)\s+passed,\s*(\d+)\s+failed", text)
        ui_passed = code == 0 and bool(result) and tuple(map(int, result[-1])) == (39, 0) and not re.search(r"^FAIL\s", text, re.MULTILINE)
        require(ui_passed, failure or "UI suite did not pass39checks with zero failures")
        for name in UI_PNGS:
            path = preview / name
            require(path.is_file(), f"UI screenshot missing: {name}")
            require(name not in before_png_times or path.stat().st_mtime_ns > before_png_times[name], f"UI screenshot was not freshly written: {name}")
        for path in sorted(preview.rglob("*")):
            if path.is_file() and path.suffix.lower() == ".png":
                require(path.resolve().is_relative_to(preview), f"PNG link escapes QA directory: {path}")
                png_evidence[path.relative_to(root).as_posix()] = validate_png(path)
        require(len(png_evidence) >= len(UI_PNGS), "No complete UI screenshot set was generated")
        tracked_invariants(root, previous, current_sources, ui_executable_sha, skip_ui_log=True)
    except (OSError, RuntimeError, ValueError, struct.error) as error:
        failure = str(error)
    finished = utc()
    # Do not overwrite a manifest concurrently replaced by another verifier.
    if sha(manifest_path) != previous_sha:
        atomic_json(archive / "refresh-failure.json", {"complete": False, "failure": "Manifest changed concurrently", "startedUTC": started_utc, "finishedUTC": finished})
        raise RuntimeError("Manifest changed concurrently; prior/new verifier evidence was not overwritten")
    ui = proof["checks"]["ui"]
    ui.update({"passed": failure is None, "count": 39 if failure is None else 0,
               "exitCode": code, "seconds": round(time.monotonic() - started, 3),
               "logSHA256": sha(log_path), "executableSHA256": ui_executable_sha,
               "pngValidationPassed": failure is None, "pngEvidence": png_evidence})
    proof["sourceSHA256"][ALLOWED_SOURCE] = current_sources[ALLOWED_SOURCE]
    proof["finishedUTC"] = finished
    proof["complete"] = failure is None and all(row["passed"] is True for row in proof["checks"].values())
    if failure:
        proof["failure"] = f"Incremental UI evidence refresh failed: {failure}"
    proof.setdefault("incrementalRefreshes", []).append({
        "kind": "UI evidence only", "reason": args.reason, "startedUTC": started_utc, "finishedUTC": finished,
        "previousManifest": (archive / manifest_path.name).relative_to(root).as_posix(), "previousManifestSHA256": previous_sha,
        "previousUILog": (archive / log_path.name).relative_to(root).as_posix(), "previousUILogSHA256": previous["checks"]["ui"]["logSHA256"],
        "source": ALLOWED_SOURCE, "previousSourceSHA256": old_sources[ALLOWED_SOURCE], "newSourceSHA256": current_sources[ALLOWED_SOURCE],
        "previousUIExecutableSHA256": previous["checks"]["ui"]["executableSHA256"], "newUIExecutableSHA256": ui_executable_sha,
        "unchangedReleaseBinarySHA256": previous["releaseBinarySHA256"], "reusedChecks": sorted(ALL_CHECKS - {"ui"}),
        "unchangedInputsGeneratedEvidenceAndOtherCheckHashes": failure is None,
        "pngCount": len(png_evidence), "passed": failure is None,
    })
    for key in ALL_CHECKS - {"ui"}:
        require(proof["checks"][key] == previous["checks"][key], f"Unexpected modification of preserved check: {key}")
    require(proof["inputEvidenceSHA256"] == previous["inputEvidenceSHA256"] and proof["generatedEvidenceSHA256"] == previous["generatedEvidenceSHA256"], "Unexpected evidence rebaseline")
    atomic_json(manifest_path, proof)
    write_new(archive / "refreshed-non-daw-v0.3.0.json", manifest_path.read_bytes())
    print(("PASS" if proof["complete"] else "FAIL") + f" UI evidence refresh;39checks required,{len(png_evidence)}single PNGs; eight prior checks retained")
    if failure:
        print(failure)
    return 0 if proof["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
