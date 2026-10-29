"""Verify the 35 native musical renders without claiming a listening result.

Reads only the explicitly named audit directory and the two versioned test WAVs.
The runner is responsible for recording the real renderer invocation and binary
fingerprint; CSV/WAV validation alone cannot prove which executable produced them.
"""
from __future__ import annotations
import argparse
from array import array
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import sys
import uuid
import wave

VERSION = "0.3.1"
FIELDS = ("case", "input", "mode", "latency", "rmsGainDb", "peakGainDb", "nullRms", "unityLateNullRms",
          "unityLateGainDb", "earlyGainDb", "recoveryGainDb", "nonfinite", "clipped")
EXPECTED_CASES = [("all-off", "sum", -1), ("bypass", "sum", -2), ("mix0", "sum", -2), ("all-unity", "sum", -2)]
for _mode in range(6):
    EXPECTED_CASES.extend([("amount0", "sum", _mode), ("half-envelope", "sum", _mode),
                           ("INIT", "bass", _mode), ("INIT", "kick", _mode), ("INIT", "sum", _mode)])
EXPECTED_CASES.append(("all-INIT", "sum", -2))
EXPECTED_WAVS = ["dry-bass.wav", "dry-kick.wav", "dry-sum.wav", "all-off-sum-mode-1.wav", "all-INIT-sum-mode-2.wav"]
EXPECTED_WAVS += [f"INIT-{kind}-mode{mode}.wav" for mode in range(6) for kind in ("bass", "kick", "sum")]
FIXTURES = ("LAB-Kick-145.wav", "LAB-Bass-145.wav")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def safe_path(value, exists=True):
    path = Path(value)
    require(path.is_absolute() and re.match(r"^[A-Za-z]:[\\/]", str(path)) and ":" not in str(path)[2:],
            "An absolute local drive path without alternate streams is required")
    require(".." not in path.parts, "Parent path components are forbidden")
    for item in (path, *path.parents):
        try:
            attributes = item.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(attributes.st_mode) and not (getattr(attributes, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)), f"Evidence reparse path forbidden: {item}")
    path = path.resolve()
    require(not exists or path.exists(), f"Required evidence is missing: {path}")
    return path


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def parse_csv(path):
    require(path.is_file() and path.stat().st_size <= 1024 * 1024, "Missing/oversized native audit CSV")
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == list(FIELDS), "Unexpected audit CSV columns")
        rows = []
        for index, source in enumerate(reader):
            require(index < 35 and None not in source and all(value is not None for value in source.values()),
                    "Native audit CSV must contain exactly 35 complete rows")
            row = {"case": source["case"], "input": source["input"]}
            for key in FIELDS[2:]:
                try:
                    if key in ("mode", "latency", "nonfinite", "clipped"):
                        require(bool(re.fullmatch(r"-?\d+", source[key])), f"Noninteger CSV {key}")
                        row[key] = int(source[key])
                    else:
                        row[key] = float(source[key])
                        require(math.isfinite(row[key]), f"Nonfinite CSV {key}")
                except ValueError as error:
                    raise RuntimeError(f"Malformed CSV {key}") from error
            require(row["latency"] == 11274, "Native audit PDC must be 11274 samples at 44.1kHz")
            require(row["nonfinite"] == 0 and row["clipped"] == 0, "Native render reports nonfinite or clipped samples")
            require(row["nullRms"] >= 0 and row["unityLateNullRms"] >= 0, "Negative RMS evidence")
            if row["case"] in ("all-off", "bypass", "mix0", "all-unity", "amount0"):
                require(row["nullRms"] < 1e-7, "Transparent native case does not null below 1e-7 RMS")
            if row["case"] in ("INIT", "all-INIT"):
                require(row["unityLateNullRms"] < 1e-6, "An INIT open window retains unintended loss or coloration")
            rows.append(row)
    keys = [(row["case"], row["input"], row["mode"]) for row in rows]
    require(len(rows) == 35 and len(set(keys)) == 35 and set(keys) == set(EXPECTED_CASES),
            "Native audit case inventory is missing, duplicated or unexpected")
    return rows


def pcm_samples(raw, width):
    if width == 2:
        values = array("h")
        values.frombytes(raw)
        if sys.byteorder != "little":
            values.byteswap()
        yield from values
    elif width == 3:
        for index in range(0, len(raw), 3):
            value = raw[index] | raw[index+1] << 8 | raw[index+2] << 16
            yield value - (1 << 24) if value & (1 << 23) else value
    else:
        raise RuntimeError("Only the actual PCM16 fixtures and PCM24 native outputs are accepted")


def inspect_wave(path, *, width, expected_frames=None, expected_values=None, collect=False):
    require(path.is_file() and 44 <= path.stat().st_size <= 32 * 1024 * 1024, f"Missing/oversized WAV: {path.name}")
    try:
        with wave.open(str(path), "rb") as source:
            channels, rate, frames, actual_width = source.getnchannels(), source.getframerate(), source.getnframes(), source.getsampwidth()
            require(source.getcomptype() == "NONE" and channels == 2 and rate == 44100 and actual_width == width,
                    f"Wrong native WAV format: {path.name}")
            require(frames > 0 and (expected_frames is None or frames == expected_frames), f"Wrong frame count: {path.name}")
            scale = float(1 << (8*width-1))
            total, peak, squares, rails, maximum_error = 0, 0.0, 0.0, 0, 0.0
            values = array("d") if collect else None
            while raw := source.readframes(16384):
                require(len(raw) % (channels*width) == 0, "Truncated PCM frame")
                for integer in pcm_samples(raw, width):
                    sample = integer / scale
                    require(math.isfinite(sample), "Nonfinite native WAV sample")
                    peak = max(peak, abs(sample)); squares += sample*sample
                    rails += integer in (-int(scale), int(scale)-1)
                    if expected_values is not None:
                        expected = expected_values(total)
                        maximum_error = max(maximum_error, abs(sample-expected))
                    if collect:
                        values.append(sample)
                    total += 1
            require(total == frames*channels, f"Incomplete native WAV payload: {path.name}")
            require(rails == 0, f"Native WAV reaches PCM full-scale rails: {path.name}")
            require(expected_values is None or maximum_error <= 1/(1 << 23)+1e-10,
                    f"Dry native WAV differs from the declared input fixture: {path.name}")
    except (wave.Error, EOFError) as error:
        raise RuntimeError(f"Invalid PCM WAV: {path.name}: {error}") from error
    return {"name": path.name, "path": str(path), "sha256": digest(path), "bytes": path.stat().st_size,
            "sampleRate": rate, "channels": channels, "frames": frames, "sampleWidthBytes": width,
            "allSamplesFinite": True, "fullScaleSampleCount": rails, "peak": peak,
            "rms": math.sqrt(squares/max(1,total)), "dryFixtureMaximumError": maximum_error}, values


def validate(root, auditdir):
    root, auditdir = safe_path(root), safe_path(auditdir)
    require(root.is_dir() and auditdir.is_dir(), "Project and native audit paths must be directories")
    fixture_dir = safe_path(root / "tests/fixtures/audio")
    manifest_path = safe_path(fixture_dir / "fixture-manifest.json")
    require(manifest_path.stat().st_size <= 64*1024, "Oversized fixture manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    require(manifest.get("bpm") == 145 and manifest.get("sampleRate") == 44100 and manifest.get("channels") == 2,
            "Wrong musical fixture timing/format")
    records = manifest.get("files")
    require(isinstance(records, list) and len(records) == 2 and {row.get("name") for row in records} == set(FIXTURES),
            "Exactly the two dedicated LAB WAV fixtures are required")
    fixture_records, audio = [], {}
    for name in FIXTURES:
        path = safe_path(fixture_dir / name)
        record = next(row for row in records if row["name"] == name)
        require(record.get("sha256") == digest(path) and record.get("bytes") == path.stat().st_size,
                f"Musical fixture hash/length changed: {name}")
        metadata, values = inspect_wave(path, width=2, collect=True)
        fixture_records.append(metadata); audio[name] = values
    frames = fixture_records[0]["frames"]
    require(all(row["frames"] == frames for row in fixture_records), "Kick and bass fixture lengths differ")
    csv_path = safe_path(auditdir / "quality-measures.csv")
    rows = parse_csv(csv_path)
    actual_wavs = {path.name for path in auditdir.iterdir() if path.suffix.lower() == ".wav"}
    require(actual_wavs == set(EXPECTED_WAVS), "Native audit WAV inventory must contain exactly 23 expected files")
    wave_records = []
    for name in EXPECTED_WAVS:
        expected = None
        if name == "dry-kick.wav": expected = lambda i: audio["LAB-Kick-145.wav"][i]
        elif name == "dry-bass.wav": expected = lambda i: audio["LAB-Bass-145.wav"][i]
        elif name == "dry-sum.wav": expected = lambda i: .5*(audio["LAB-Kick-145.wav"][i]+audio["LAB-Bass-145.wav"][i])
        metadata, _ = inspect_wave(safe_path(auditdir / name), width=3, expected_frames=frames, expected_values=expected)
        wave_records.append(metadata)
    checks = {"exact35NativeCases": len(rows) == 35, "csvAllFiniteAndUnclipped": True,
              "latency11274At44100": True, "transparentCasesNullBelow1eMinus7": True,
              "initOpenWindowsNullBelow1eMinus6": True, "fixtureHashesMatchManifest": True,
              "exact23NativeWaveFiles": len(wave_records) == 23, "waveFormatAndFramesMatchFixtures": True,
              "waveSamplesFiniteAndNoFullScale": True, "dryWavesMatchActualFixtures": True}
    return {"schema": "openduck-quality-audio-v1", "version": VERSION, "passed": all(checks.values()),
            "root": str(root), "auditDirectory": str(auditdir),
            "rendererCaseCount": len(rows), "waveFileCount": len(wave_records), "checks": checks,
            "csv": {"path": str(csv_path), "sha256": digest(csv_path), "bytes": csv_path.stat().st_size},
            "fixtureManifest": {"path": str(manifest_path), "sha256": digest(manifest_path)},
            "fixtures": fixture_records, "waveFiles": wave_records, "cases": rows,
            "verifierSourceSHA256": digest(Path(__file__).resolve()),
            "scope": "Measured file validation only. No normalization, resampling, makeup, global loudness target, listening certification or DAW claim. Real native invocation/binary provenance belongs to the checked runner log."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--audit-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = validate(args.root, args.audit_dir)
        output = safe_path(args.output, exists=False)
        root, auditdir = safe_path(args.root), safe_path(args.audit_dir)
        require(output.suffix.lower() == ".json" and (output.is_relative_to(root) or output.is_relative_to(auditdir)),
                "Output must be a named JSON inside the explicit project or audit directory")
        require(output.name != "fixture-manifest.json", "Never overwrite the fixture manifest")
        require(output.parent.is_dir(), "Output directory must already exist")
        temporary = output.with_name(output.name + "." + uuid.uuid4().hex + ".tmp")
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2); stream.write("\n")
        os.replace(temporary, output)
        print(json.dumps({"passed": result["passed"], "rendererCaseCount": result["rendererCaseCount"],
                          "waveFileCount": result["waveFileCount"], "output": str(output)}, indent=2))
        return 0
    except Exception as error:
        print(f"Quality audio evidence failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
