"""Read-only, explicit-path Ableton 0.3.0 post-run evidence.

Based on tools/gather_ableton_qa.py. Reads only the named QA/original sets, four
baseline-listed preference files, baseline snapshots and the two named plugin
bundles. Never reads Live logs, launches/stops Live, or changes preferences.
Preference contents/snapshots are private and must never enter a release ZIP.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

VERSION = "0.3.0"
QA_RELATIVE = "work/Ableton-QA-v0.3.0"
ALLOWED_SETTINGS = {"Preferences.cfg", "Library.cfg", "Options.txt", "UsageData.cfg"}
JUCE_MAGIC = b"VC2!"
MAX_XML = 16 * 1024 * 1024
MAX_ALS = 256 * 1024 * 1024
V3_BASES = "speedMode speed hz snap snapEnabled smooth trigger loop end endHold loopStart loopEnd midiChannel noteLow noteHigh velocityLow velocityHigh velocityDepth locked detectorLow detectorHigh threshold hysteresis holdoff detectorAttack detectorRelease listen lookahead offset bands linked slope phaseMode crossLow crossHigh lowAmount midAmount highAmount lowSolo midSolo highSolo group riseSlope output swing character runStopped".split()
LEGACY_IDS = set("amount attack bandAttack0 bandAttack1 bandAttack2 bandHigh bandLow bandMid bandRelease0 bandRelease1 bandRelease2 bypass crossHigh crossLow curveAmount0 curveAmount1 curveAmount2 curveAmount3 curveAmount4 curveAmount5 curveAttack0 curveAttack1 curveAttack2 curveAttack3 curveAttack4 curveAttack5 curveBand0 curveBand1 curveBand2 curveBand3 curveBand4 curveBand5 curveFocus0 curveFocus1 curveFocus2 curveFocus3 curveFocus4 curveFocus5 curveGrid0 curveGrid1 curveGrid2 curveGrid3 curveGrid4 curveGrid5 curveLength0 curveLength1 curveLength2 curveLength3 curveLength4 curveLength5 curvePhase0 curvePhase1 curvePhase2 curvePhase3 curvePhase4 curvePhase5 curveRelease0 curveRelease1 curveRelease2 curveRelease3 curveRelease4 curveRelease5 curveShape0 curveShape1 curveShape2 curveShape3 curveShape4 curveShape5 curveTension0 curveTension1 curveTension2 curveTension3 curveTension4 curveTension5 detectorAttack detectorRelease freeBpm grid grooveDepth hold hysteresis mix morph on0 on1 on2 on3 on4 on5 point0 point1 point2 point3 point4 point5 point6 point7 release selected shape shift solo0 solo1 solo2 solo3 solo4 solo5 spectralDepth spectralHigh spectralLow step0 step1 step10 step11 step12 step13 step14 step15 step2 step3 step4 step5 step6 step7 step8 step9 swing sync tension threshold trigger".split())
REQUIRED_IDS = LEGACY_IDS | {"v3" + base[0].upper() + base[1:] + str(mode) for base in V3_BASES for mode in range(6)}
assert len(LEGACY_IDS) == 131 and len(REQUIRED_IDS) == 413


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def safe_path(value, boundary=None):
    path = Path(value)
    require(path.is_absolute(), f"Absolute local path required: {path}")
    require(re.match(r"^[A-Za-z]:[\\/]", str(path)) is not None and ":" not in str(path)[2:],
            "Only local drive paths without alternate data streams are allowed")
    require(not any(part in (".", "..") for part in path.parts), "Relative path components are forbidden")
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            continue
        require(not (getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)
                and not stat.S_ISLNK(info.st_mode), f"Reparse/link path forbidden: {item}")
    path = path.resolve()
    if boundary is not None:
        require(path != boundary and path.is_relative_to(boundary), f"Path escapes its boundary: {path}")
    return path


def read_json(path):
    require(path.stat().st_size <= 16 * 1024 * 1024, "Unexpected metadata size")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def local_tag(node):
    return node.tag.rsplit("}", 1)[-1]


def nodes_with_paths(node, path):
    yield node, path
    for index, child in enumerate(node):
        yield from nodes_with_paths(child, f"{path}/{local_tag(child)}[{index}]")


def parse_xml(data):
    require(b"<!DOCTYPE" not in data.upper(), "DOCTYPE is forbidden in evidence XML")
    return ET.fromstring(data)


def decode_juce_states(data):
    offset = 0
    while (start := data.find(JUCE_MAGIC, offset)) >= 0:
        offset = start + len(JUCE_MAGIC)
        if start + 8 > len(data):
            continue
        size = struct.unpack_from("<I", data, start + 4)[0]
        if not (0 < size <= MAX_XML and start + 8 + size <= len(data)):
            continue
        original = data[start + 8:start + 8 + size].rstrip(b"\0")
        try:
            state = parse_xml(original)
        except (ET.ParseError, RuntimeError):
            continue
        if local_tag(state) == "OpenDuckState":
            yield start, original, state


def finite(value, low=-math.inf, high=math.inf):
    number = float(value)
    require(math.isfinite(number) and low <= number <= high, "Nonfinite/out-of-range state value")
    return number


def curve_record(node):
    attributes = dict(node.attrib)
    points = [dict(point.attrib) for point in node.findall("Point")]
    errors = []
    try:
        count = int(attributes["count"])
        require(0 <= count <= 256 and count == len(points) and len(node) == count, "Invalid point count")
        previous = -1.0
        for point in points:
            require({"x", "y", "segment", "curvature", "reversed"} <= point.keys(), "Missing segment metadata")
            x = finite(point["x"], 0, 1)
            require(x >= previous, "Points are not sorted")
            previous = x
            finite(point["y"], 0, 1)
            finite(point["curvature"], -1, 1)
            require(int(point["segment"]) in range(6) and point["reversed"] in ("0", "1"), "Invalid segment type/reversed")
        for flag in ("edit", "freeDraw", "linear"):
            require(attributes.get(flag) in ("0", "1"), "Invalid curve flag")
    except (KeyError, ValueError, RuntimeError) as error:
        errors.append(str(error))
    return {"attributes": attributes, "points": points, "valid": not errors, "errors": errors}


def state_record(state):
    errors = []
    parameters = {}
    curves = [curve_record(node) for node in state.findall("Curves/Curve")]
    bands = [curve_record(node) for node in state.findall("Bands3/BandCurve")]
    memories = [dict(node.attrib) for node in state.findall("ShapeMemories3/Memory")]
    groups = [curve_record(node) for node in state.findall("GroupShapes3/GroupShape")]
    try:
        require(all(len(state.findall(name)) == 1 for name in ("Curves", "Bands3", "ShapeMemories3", "GroupShapes3")),
                "Missing/duplicate schema3 containers")
        nodes = state.findall("PARAM")
        for node in nodes:
            require("id" in node.attrib and node.attrib["id"] not in parameters, "Missing/duplicate parameter ID")
            finite(node.attrib["value"])
            parameters[node.attrib["id"]] = node.attrib["value"]
        require(len(parameters) >= 413 and REQUIRED_IDS <= parameters.keys(), "Missing actual 413-parameter APVTS contract")
        require(int(state.attrib["schema"]) == 3, "Wrong schema")
        for name in ("modernMask", "revision3Mask"):
            require(0 <= int(state.attrib[name]) <= 63, "Invalid revision mask")
        expected_routing = "serial-0.3" if int(state.attrib["revision3Mask"]) else "legacy-budget"
        require(state.attrib.get("audioRouting") == expected_routing, "Wrong declared audio routing")
        require(len(curves) == 6 and {int(c["attributes"]["mode"]) for c in curves} == set(range(6)), "Missing/duplicate six curve slots")
        require(all(c["valid"] for c in curves), "Invalid main curve geometry")
        require(len(bands) == 18 and {(int(c["attributes"]["mode"]), int(c["attributes"]["band"])) for c in bands}
                == {(mode, band) for mode in range(6) for band in range(3)}, "Missing/duplicate eighteen band curves")
        require(all(c["valid"] for c in bands), "Invalid band curve geometry")
        for band in bands:
            for name in ("phase", "length", "tension", "amount", "attack", "release", "riseSlope"):
                finite(band["attributes"][name])
        require(len(memories) == 96 and {(int(m["mode"]), int(m["shape"])) for m in memories}
                == {(mode, shape) for mode in range(6) for shape in range(16)}, "Incomplete profile transformation memories")
        for memory in memories:
            for name in ("phase", "length", "tension", "riseSlope"):
                finite(memory[name])
        group_modes = [int(group["attributes"]["mode"]) for group in groups]
        require(len(group_modes) == len(set(group_modes)) and set(group_modes) <= set(range(6))
                and all(group["valid"] for group in groups), "Invalid shared group geometry")
    except (KeyError, ValueError, RuntimeError) as error:
        errors.append(str(error))
    return {"schema": 3, "rootAttributes": dict(state.attrib), "parameterCount": len(parameters),
            "parameters": parameters, "requiredParameterCount": len(REQUIRED_IDS),
            "v3ParameterCount": sum(key.startswith("v3") for key in parameters),
            "curveSlotCount": len(curves), "curveSlots": curves,
            "bandCurveSlotCount": len(bands), "bandCurveSlots": bands,
            "shapeMemoryCount": len(memories), "shapeMemories": memories,
            "groupShapes": groups,
            "completeSchema3State": not errors, "validationErrors": errors}


def extract_qa_states(path):
    require(path.stat().st_size <= MAX_ALS, "QA set exceeds bounded evidence size")
    raw = path.read_bytes()
    if raw.startswith(b"\x1f\x8b"):
        with gzip.GzipFile(fileobj=io.BytesIO(raw)) as stream:
            unpacked = stream.read(MAX_ALS + 1)
    else:
        unpacked = raw
    require(len(unpacked) <= MAX_ALS, "Unpacked QA set exceeds bounded evidence size")
    tree = parse_xml(unpacked)
    chunks, found, legacy_count = [], set(), 0
    for node, node_path in nodes_with_paths(tree, local_tag(tree)):
        candidates = [("text", node.text or ""), *(("attribute:" + key, value) for key, value in node.attrib.items())]
        for source, text in candidates:
            compact = "".join(text.split())
            if len(compact) < 16 or len(compact) % 2 or not re.fullmatch(r"[0-9a-fA-F]+", compact):
                continue
            blob = bytes.fromhex(compact)
            for offset, original, state in decode_juce_states(blob):
                xml_hash = hashlib.sha256(original).hexdigest()
                key = node_path, source, offset, xml_hash
                if key in found:
                    continue
                found.add(key)
                if state.attrib.get("schema", "1") != "3":
                    legacy_count += 1
                    continue
                record = state_record(state)
                record.update(nodePath=node_path, source=source, chunkOffset=offset,
                              xmlByteCount=len(original), xmlSHA256=xml_hash,
                              originalXml=original.decode("utf-8"))
                chunks.append(record)
    return {"qaSet": str(path), "qaSetSHA256": hashlib.sha256(raw).hexdigest(),
            "schema3ChunkCount": len(chunks), "legacyOpenDuckChunkCount": legacy_count,
            "allSchema3ChunksComplete": bool(chunks) and all(c["completeSchema3State"] for c in chunks),
            "modernSchema3ChunkCount": sum(int(c["rootAttributes"].get("revision3Mask", "0")) > 0 for c in chunks),
            "schema3Chunks": chunks}


def live_processes():
    command = "@(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'Ableton Live*' } | Sort-Object Id | ForEach-Object { $started=$_.StartTime.ToUniversalTime(); [pscustomobject]@{id=[int]$_.Id; processName=$_.ProcessName; startTimeUtc=$started.ToString('O'); startTimeUtcTicks=$started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)} }) | ConvertTo-Json -Compress"
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=True)
    value = json.loads(result.stdout) if result.stdout.strip() else []
    return value if isinstance(value, list) else [value]


def process_identity_keys(records):
    require(isinstance(records, list), "Live process identities must be a list")
    keys, ids = set(), set()
    for item in records:
        require(isinstance(item, dict) and type(item.get("id")) is int and item["id"] > 0,
                "Invalid Live process ID")
        require(isinstance(item.get("processName"), str) and item["processName"].startswith("Ableton Live") and
                isinstance(item.get("startTimeUtcTicks"), str) and
                re.fullmatch(r"\d{16,20}", item["startTimeUtcTicks"]) and
                isinstance(item.get("startTimeUtc"), str) and item["startTimeUtc"].endswith("Z"),
                "Invalid Live process start-time identity")
        require(item["id"] not in ids, "Duplicate Live PID in identity evidence")
        ids.add(item["id"])
        stamp = re.fullmatch(r"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,7}))?Z", item["startTimeUtc"])
        require(stamp is not None, "Invalid Live UTC timestamp")
        elapsed = datetime.fromisoformat(stamp[1] + "+00:00") - datetime(1, 1, 1, tzinfo=timezone.utc)
        expected_ticks = (elapsed.days * 86400 + elapsed.seconds) * 10_000_000 + int((stamp[2] or "").ljust(7, "0"))
        require(str(expected_ticks) == item["startTimeUtcTicks"], "Live timestamp and tick identity disagree")
        keys.add((item["id"], item["startTimeUtcTicks"]))
    return keys


def bundle_files(directory):
    safe_path(directory)
    if not directory.exists():
        return []
    require(directory.is_dir() and directory.name == "OpenDuck Pro X.vst3", "Unexpected production bundle")
    result = []
    for current, directories, files in os.walk(directory, followlinks=False):
        for name in directories:
            safe_path(Path(current) / name, directory)
        for name in files:
            path = safe_path(Path(current) / name, directory)
            require(path.is_file(), "Non-file bundle payload")
            result.append({"relative": path.relative_to(directory).as_posix(), "sha256": sha256(path)})
    return sorted(result, key=lambda item: item["relative"].lower())


def gather(root, qa_set, original_set=None):
    root = safe_path(root)
    qa_root = safe_path(root / QA_RELATIVE, root)
    baseline_path = safe_path(qa_root / "baseline.json", qa_root)
    digest_path = safe_path(qa_root / "baseline.json.sha256", qa_root)
    require(sha256(baseline_path) == digest_path.read_text(encoding="utf-8-sig").strip().lower(), "Baseline metadata changed")
    baseline = read_json(baseline_path)
    require(baseline.get("version") == VERSION and baseline.get("baselineComplete") is True, "Incomplete/wrong baseline")
    require(safe_path(baseline["projectRoot"]) == root, "Baseline project mismatch")
    qa_set = safe_path(qa_set, root)
    require(qa_set == safe_path(baseline["qaSet"], root) and qa_set.suffix.lower() == ".als", "Only the planned named QA set can be inspected")
    original_path = safe_path(baseline["originalSet"], root)
    if original_set is not None:
        require(safe_path(original_set, root) == original_path, "Original-set path does not match baseline")
    require(original_path != qa_set, "Original and modified QA set must differ")
    settings = baseline["settings"]
    require(len(settings) == 4 and {item["name"] for item in settings} == ALLOWED_SETTINGS, "Exactly four whitelisted settings required")
    preferences_root = safe_path(baseline["preferencesRoot"])
    require(not preferences_root.is_relative_to(root) and not root.is_relative_to(preferences_root), "Preference and project boundaries overlap")
    allow_running = baseline.get("allowRunningLiveBaseline", False)
    initial_processes = baseline.get("initialLiveProcesses", [])
    initial_keys = process_identity_keys(initial_processes)
    require(type(allow_running) is bool and type(baseline.get("abletonInitiallyRunning")) is bool and
            baseline["abletonInitiallyRunning"] == bool(initial_keys), "Invalid initial Live process contract")
    require(not initial_keys or (allow_running and baseline.get("processIdentitySchema") == 1),
            "Running-Live baseline was not explicitly authorized")
    if allow_running:
        require(preferences_root.name.lower() == "preferences" and preferences_root.parent.name.lower() == "live 12.4.15",
                "Running-Live baseline must identify the explicit Live 12.4.15 profile")
    restoration = []
    for item in settings:
        current = safe_path(preferences_root / item["name"], preferences_root)
        snapshot = safe_path(qa_root / "Preferences-before" / item["name"], qa_root)
        require(type(item["exists"]) is bool, "Invalid baseline existence flag")
        existed = item["exists"]
        expected = item.get("sha256")
        require(not existed or bool(re.fullmatch(r"[0-9a-fA-F]{64}", str(expected))), "Invalid preference hash")
        for path in (current, snapshot):
            require(not path.exists() or path.is_file(), "A whitelisted setting is not a regular file")
        current_hash = sha256(current) if current.is_file() else None
        snapshot_hash = sha256(snapshot) if snapshot.is_file() else None
        restoration.append({"name": item["name"], "initiallyExisted": existed,
                            "currentlyExists": current.exists(), "baselineSHA256": expected,
                            "currentSHA256": current_hash, "snapshotSHA256": snapshot_hash,
                            "restoredExactly": current.exists() == existed and (not existed or current_hash == expected.lower()),
                            "baselineSnapshotIntact": snapshot.exists() == existed and (not existed or snapshot_hash == expected.lower())})
    production_bundle = safe_path(baseline["bundle"])
    actual_files = bundle_files(production_bundle)
    expected = {item["relative"].replace("\\", "/").lower(): item["sha256"].lower() for item in baseline["bundleFiles"]}
    actual = {item["relative"].lower(): item["sha256"] for item in actual_files}
    target_binary = safe_path(root / "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3", root)
    require(target_binary == safe_path(baseline["targetBinary"], root), "Target binary path changed")
    target_hash = sha256(target_binary)
    original_hash = sha256(original_path)
    original_snapshot = safe_path(qa_root / "TEST-LAB-before.als", qa_root)
    processes = live_processes()
    current_keys = process_identity_keys(processes)
    additional_keys, missing_keys = current_keys - initial_keys, initial_keys - current_keys
    extraction = extract_qa_states(qa_set)
    checks = {"fourPreferencesRestoredExactly": all(item["restoredExactly"] for item in restoration),
              "baselinePreferenceSnapshotsIntact": all(item["baselineSnapshotIntact"] for item in restoration),
              "productionBundleUnchanged": actual == expected and production_bundle.exists() == baseline["bundleExists"],
              "abletonClosed": not processes,
              "qaProcessClosed": not additional_keys,
              "noAdditionalLiveProcesses": not additional_keys,
              "initialLiveProcessesPreserved": not missing_keys,
              "initialProcessStateRestored": current_keys == initial_keys,
              "targetReleaseBinaryUnchanged": target_hash == baseline["targetBinarySHA256"],
              "originalQASetUnchanged": original_hash == baseline["testProjectSHA256"],
              "originalQASetSnapshotIntact": sha256(original_snapshot) == baseline["testProjectSHA256"],
              "schema3StateCaptured": extraction["schema3ChunkCount"] > 0,
              "schema3StatesComplete": extraction["allSchema3ChunksComplete"],
              "schema3ModernEngineCaptured": extraction["modernSchema3ChunkCount"] > 0}
    return {"version": VERSION, "gatheredAtUTC": datetime.now(timezone.utc).isoformat(),
            "baseline": str(baseline_path), "baselineSHA256": sha256(baseline_path),
            "startedAtUTC": baseline["startedAtUTC"], "loadingPlan": baseline["loadingPlan"],
            "checks": checks, "postRunChecksPassed": all(value for key, value in checks.items() if key != "abletonClosed"), "settings": restoration,
            "systemBundle": {"path": str(production_bundle), "baselineFiles": baseline["bundleFiles"], "currentFiles": actual_files},
            "process": {"initiallyRunning": baseline["abletonInitiallyRunning"], "allowRunningLiveBaseline": allow_running,
                        "initialLiveProcesses": initial_processes, "currentlyRunning": bool(processes),
                        "currentlyRunningLiveProcesses": processes,
                        "additionalLiveProcesses": [item for item in processes if (item["id"], item["startTimeUtcTicks"]) in additional_keys],
                        "missingInitialLiveProcesses": [item for item in initial_processes if (item["id"], item["startTimeUtcTicks"]) in missing_keys]},
            "releaseBinary": {"path": str(target_binary), "sha256": target_hash},
            "originalQASet": {"checked": True, "path": str(original_path), "sha256": original_hash, "unchanged": checks["originalQASetUnchanged"]},
            "savedStateEvidence": extraction,
            "scope": "Read-only restoration and saved schema3 evidence; no preference contents, private Live logs, or unobserved audio/UI certification"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--qa-set", type=Path, required=True)
    parser.add_argument("--original-set", type=Path)
    parser.add_argument("--output", type=Path, help="Optional new JSON report inside the fixed QA folder")
    args = parser.parse_args()
    try:
        report = gather(args.root, args.qa_set, args.original_set)
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            root = safe_path(args.root)
            qa_root = safe_path(root / QA_RELATIVE, root)
            output = safe_path(args.output, qa_root)
            require(output.parent == qa_root and output.name not in ("baseline.json", "RESTORE-REPORT.json") and output.suffix.lower() == ".json", "Use a report filename directly inside QA root")
            with output.open("x", encoding="utf-8") as stream:
                stream.write(rendered)
            print(json.dumps({"report": str(output), "checks": report["checks"], "schema3ChunkCount": report["savedStateEvidence"]["schema3ChunkCount"]}, indent=2))
        else:
            print(rendered, end="")
        return 0 if report["postRunChecksPassed"] else 2
    except Exception as error:
        print(f"Post-run evidence gathering failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
