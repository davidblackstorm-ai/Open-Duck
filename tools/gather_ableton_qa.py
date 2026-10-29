"""Read-only post-run evidence for the explicitly authorized Ableton QA session.

Reads only the named QA .als, the four baseline-listed settings, the OpenDuck
system bundle and the produced project binary. It does not read Ableton logs,
change preferences, install files, launch Live or stop any process. By default
the report is written to stdout; --output may write only inside the QA folder.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET


ALLOWED_SETTINGS = {"Preferences.cfg", "Library.cfg", "Options.txt", "UsageData.cfg"}
JUCE_MAGIC = b"VC2!"


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def json_read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def local_tag(node: ET.Element) -> str:
    return node.tag.rsplit("}", 1)[-1]


def nodes_with_paths(node: ET.Element, path: str):
    yield node, path
    for index, child in enumerate(node):
        yield from nodes_with_paths(child, f"{path}/{local_tag(child)}[{index}]")


def decode_juce_states(data: bytes):
    offset = 0
    while True:
        offset = data.find(JUCE_MAGIC, offset)
        if offset < 0:
            return
        start = offset
        offset += len(JUCE_MAGIC)
        if start + 8 > len(data):
            continue
        size = struct.unpack_from("<I", data, start + 4)[0]
        if not (0 < size <= 16 * 1024 * 1024 and start + 8 + size <= len(data)):
            continue
        original = data[start + 8:start + 8 + size].rstrip(b"\0")
        try:
            xml = original.decode("utf-8")
            state = ET.fromstring(xml)
        except (UnicodeDecodeError, ET.ParseError):
            continue
        if local_tag(state) == "OpenDuckState":
            yield start, original, state


def extract_qa_states(path: Path) -> dict:
    raw = path.read_bytes()
    unpacked = gzip.decompress(raw) if raw.startswith(b"\x1f\x8b") else raw
    tree = ET.fromstring(unpacked)
    schema2 = []
    legacy_count = 0
    found = set()
    for node, node_path in nodes_with_paths(tree, local_tag(tree)):
        # Ableton's actual ProcessorState text in the QA set is hexadecimal.
        # No clip names, media paths, browser history or user logs are collected.
        candidates = [("text", node.text or "")]
        candidates.extend(("attribute:" + key, value) for key, value in node.attrib.items())
        for source, text in candidates:
            compact = "".join(text.split())
            if len(compact) < 16 or len(compact) % 2 or not re.fullmatch(r"[0-9a-fA-F]+", compact):
                continue
            blob = bytes.fromhex(compact)
            if JUCE_MAGIC not in blob:
                continue
            for offset, original, state in decode_juce_states(blob):
                xml_hash = hashlib.sha256(original).hexdigest()
                key = (node_path, source, offset, xml_hash)
                if key in found:
                    continue
                found.add(key)
                schema = int(state.attrib.get("schema", "1"))
                if schema != 2:
                    legacy_count += 1
                    continue
                parameters = {item.attrib["id"]: item.attrib.get("value", "")
                              for item in state.findall("PARAM") if "id" in item.attrib}
                curves_node = state.find("Curves")
                curves = []
                if curves_node is not None:
                    for curve in curves_node.findall("Curve"):
                        curves.append({
                            "attributes": dict(curve.attrib),
                            "points": [dict(point.attrib) for point in curve.findall("Point")],
                        })
                complete = len(curves) == 6 and len(parameters) >= 131
                try:
                    complete &= {int(curve["attributes"]["mode"]) for curve in curves} == set(range(6))
                    complete &= all(int(curve["attributes"].get("count", "-1")) == len(curve["points"])
                                    and len(curve["points"]) <= 256 for curve in curves)
                except (KeyError, ValueError):
                    complete = False
                schema2.append({
                    "nodePath": node_path, "source": source, "chunkOffset": offset,
                    "xmlByteCount": len(original), "xmlSHA256": xml_hash,
                    "schema": schema, "rootAttributes": dict(state.attrib),
                    "parameterCount": len(parameters), "parameters": parameters,
                    "curveSlotCount": len(curves), "curveSlots": curves,
                    "completeSixSlotState": complete,
                    "originalXml": original.decode("utf-8"),
                })
    return {
        "qaSet": str(path), "qaSetSHA256": hashlib.sha256(raw).hexdigest(),
        "schema2ChunkCount": len(schema2), "legacyOpenDuckChunkCount": legacy_count,
        "allSchema2ChunksComplete": bool(schema2) and all(item["completeSixSlotState"] for item in schema2),
        "schema2Chunks": schema2,
    }


def live_processes() -> list[dict]:
    command = (
        "@(Get-Process -ErrorAction SilentlyContinue | "
        "Where-Object { $_.ProcessName -like 'Ableton Live*' } | "
        "Select-Object ProcessName,Id) | ConvertTo-Json -Compress"
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=True,
    )
    text = result.stdout.strip()
    if not text:
        return []
    value = json.loads(text)
    return value if isinstance(value, list) else [value]


def bundle_files(directory: Path) -> list[dict]:
    result = []
    if not directory.exists():
        return result
    for current, directories, files in os.walk(directory, followlinks=False):
        for name in directories:
            child = Path(current) / name
            require(not child.is_symlink() and not (hasattr(os.path, "isjunction") and os.path.isjunction(child)),
                    f"Unexpected system-bundle reparse directory: {child}")
        for name in files:
            path = Path(current) / name
            require(not path.is_symlink(), f"Unexpected system-bundle file link: {path}")
            result.append({"relative": path.relative_to(directory).as_posix(), "sha256": sha256(path)})
    return sorted(result, key=lambda item: item["relative"].lower())


def gather(root: Path, qa_set: Path, original_set: Path | None = None) -> dict:
    qa_root = root / "work/Ableton-QA-v0.2.0"
    baseline_path = qa_root / "baseline.json"
    baseline = json_read(baseline_path)
    settings = baseline.get("settings", [])
    require({item.get("name") for item in settings} == ALLOWED_SETTINGS and len(settings) == 4,
            "Baseline must identify exactly the four authorized settings files")
    require(qa_set.resolve().is_relative_to(root.resolve()) and qa_set.suffix.lower() == ".als",
            "Only the explicitly supplied project QA .als can be inspected")
    preferences_root = Path(baseline["preferencesRoot"])
    restoration = []
    for item in settings:
        current = preferences_root / item["name"]
        snapshot = qa_root / "Preferences-before" / item["name"]
        existed = item["exists"] is True
        expected_hash = str(item.get("sha256") or "").lower()
        current_hash = sha256(current) if current.is_file() else None
        snapshot_hash = sha256(snapshot) if snapshot.is_file() else None
        current_matches = current.exists() == existed and (not existed or current_hash == expected_hash)
        snapshot_matches = snapshot.exists() == existed and (not existed or snapshot_hash == expected_hash)
        restoration.append({
            "name": item["name"], "initiallyExisted": existed,
            "currentlyExists": current.exists(), "baselineSHA256": item.get("sha256"),
            "currentSHA256": current_hash, "snapshotSHA256": snapshot_hash,
            "restoredExactly": current_matches, "baselineSnapshotIntact": snapshot_matches,
        })
    production_bundle = Path(baseline["bundle"])
    actual_files = bundle_files(production_bundle)
    expected = {item["relative"].replace("\\", "/").lower(): item["sha256"].lower()
                for item in baseline["bundleFiles"]}
    actual = {item["relative"].lower(): item["sha256"] for item in actual_files}
    processes = live_processes()
    initially_running = baseline.get("abletonInitiallyRunning") is True
    current_running = bool(processes)
    extraction = extract_qa_states(qa_set)
    target_binary = root / "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3"
    target_hash = sha256(target_binary) if target_binary.is_file() else None
    target_unchanged = target_hash == str(baseline.get("targetBinarySHA256", "")).lower()
    original = {"checked": False, "reason": "No original QA set path was supplied"}
    if original_set is not None:
        require(original_set.resolve().is_relative_to(root.resolve()), "Original QA set must remain in the project")
        original_hash = sha256(original_set) if original_set.is_file() else None
        original = {"checked": True, "path": str(original_set), "sha256": original_hash,
                    "unchanged": original_hash == str(baseline.get("testProjectSHA256", "")).lower()}
    checks = {
        "fourPreferencesRestoredExactly": all(item["restoredExactly"] for item in restoration),
        "baselinePreferenceSnapshotsIntact": all(item["baselineSnapshotIntact"] for item in restoration),
        "productionBundleUnchanged": actual == expected,
        "abletonClosed": not current_running,
        "initialProcessStateRestored": current_running == initially_running,
        "targetReleaseBinaryUnchanged": target_unchanged,
        "schema2StateCaptured": extraction["schema2ChunkCount"] > 0,
        "schema2StatesComplete": extraction["allSchema2ChunksComplete"],
    }
    if original.get("checked"):
        checks["originalQASetUnchanged"] = original["unchanged"]
    return {
        "version": "0.2.0", "gatheredAtUTC": datetime.now(timezone.utc).isoformat(),
        "baseline": str(baseline_path), "baselineSHA256": sha256(baseline_path),
        "startedAtUTC": baseline.get("startedAtUTC"), "loadingPlan": baseline.get("loadingPlan"),
        "checks": checks, "postRunChecksPassed": all(checks.values()),
        "settings": restoration,
        "systemBundle": {"path": str(production_bundle), "baselineFiles": baseline["bundleFiles"],
                         "currentFiles": actual_files},
        "process": {"initiallyRunning": initially_running, "currentlyRunning": current_running,
                    "currentlyRunningLiveProcesses": processes},
        "releaseBinary": {"path": str(target_binary), "sha256": target_hash},
        "originalQASet": original, "savedStateEvidence": extraction,
        "scope": "Read-only post-run restoration and saved-state evidence; does not certify unobserved audio/UI behavior",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--qa-set", type=Path, required=True)
    parser.add_argument("--original-set", type=Path)
    parser.add_argument("--output", type=Path, help="Optional JSON path inside work/Ableton-QA-v0.2.0")
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        report = gather(root, args.qa_set.resolve(), args.original_set.resolve() if args.original_set else None)
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            output = args.output.resolve()
            require(output.is_relative_to((root / "work/Ableton-QA-v0.2.0").resolve()),
                    "Report output must remain in the project QA folder")
            require(output.suffix.lower() == ".json", "Report output must be a JSON file")
            require(output.name != "baseline.json", "The baseline cannot be overwritten")
            output.write_text(rendered, encoding="utf-8")
            print(json.dumps({"report": str(output), "checks": report["checks"],
                              "schema2ChunkCount": report["savedStateEvidence"]["schema2ChunkCount"]}, indent=2))
        else:
            print(rendered, end="")
        return 0 if report["postRunChecksPassed"] else 2
    except Exception as error:
        print(f"Post-run evidence gathering failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
