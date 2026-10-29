"""Independently inspect a named project QA .als without changing it or Live.

The script joins actual Live AutomationEnvelope PointeeId values to each
PluginFloatParameter AutomationTarget, verifies recorded VST3 parameter IDs,
and compares the final two modern OpenDuck processor-state chunks.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
import math
import re
import sys
from gather_ableton_qa_v031 import REQUIRED_IDS
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


def value(node, path, default=None):
    found = node.find(path)
    return found.get("Value", default) if found is not None else default


def vst3_id(name):
    # JUCE String::hashCode followed by the default Studio One-compatible mask.
    # Verified against juce_VST3ClientExtensions.cpp:43-50 in the project's JUCE.
    result = 0
    for character in name:
        result = (31 * result + ord(character)) & 0xffffffff
    return result & 0x7fffffff


def processor_state(info):
    state = info.find("Preset/Vst3Preset/ProcessorState")
    if state is None:
        raise ValueError("QA plugin has no ProcessorState")
    blob = bytes.fromhex("".join((state.text or "").split()))
    if blob[:4] != b"VC2!":
        raise ValueError("ProcessorState is not the expected JUCE binary XML format")
    length = struct.unpack_from("<I", blob, 4)[0]
    raw = blob[8:8 + length].rstrip(b"\0")
    xml = ET.fromstring(raw)
    if xml.tag != "OpenDuckState":
        raise ValueError("State belongs to a different plugin")
    nodes = xml.findall("PARAM")
    parameters = {node.get("id"): node.get("value") for node in nodes}
    if len(parameters) != len(nodes) or not all(math.isfinite(float(v)) for v in parameters.values()):
        raise ValueError("Duplicate/non-finite OpenDuck processor parameters")
    curves = []
    parent = xml.find("Curves")
    if parent is not None:
        curves = [{"attributes": dict(node.attrib),
                   "points": [dict(point.attrib) for point in node.findall("Point")]}
                  for node in parent.findall("Curve")]
    return {"attributes": dict(xml.attrib), "parameters": parameters, "curves": curves,
            "xmlSHA256": hashlib.sha256(raw).hexdigest(), "blobSHA256": hashlib.sha256(blob).hexdigest(),
            "bands3": ET.tostring(xml.find("Bands3")) if xml.find("Bands3") is not None else None,
            "shapeMemories3": ET.tostring(xml.find("ShapeMemories3")) if xml.find("ShapeMemories3") is not None else None}


def raw_range(name, normalized):
    if name.startswith("curvePhase"):
        return [-50 + item * 100 for item in normalized]
    if name == "trigger":
        return [item * 2 for item in normalized]
    if name.startswith("curveAmount") or name in {"amount", "mix"}:
        return [item * 100 for item in normalized]
    if name.startswith(("on", "solo")):
        return normalized
    return None


def bounds(values):
    return [min(values), max(values)] if values else None


def audit(path):
    original = path.read_bytes()
    xml = ET.fromstring(gzip.decompress(original) if original.startswith(b"\x1f\x8b") else original)
    parents = {child: parent for parent in xml.iter() for child in parent}
    envelope_map = {}
    envelopes = list(xml.iter("AutomationEnvelope"))
    for envelope in envelopes:
        target = value(envelope, "EnvelopeTarget/PointeeId")
        envelope_map.setdefault(target, []).append(envelope)
    plugins = []
    for ordinal, info in enumerate(xml.iter("Vst3PluginInfo"), 1):
        device = parents[parents[info]]
        try:
            state = processor_state(info)
        except (ValueError, ET.ParseError, struct.error):
            # A mixed Live set can contain other VST3s with arbitrary state
            # formats. Never interpret those as OpenDuck processor chunks.
            declared_name = value(info, "Name", "")
            if "openduck" in declared_name.lower():
                raise
            continue
        track = device
        while track.tag not in {"AudioTrack", "MidiTrack", "GroupTrack", "MasterTrack"} and track in parents:
            track = parents[track]
        parameters = []
        for parameter in device.findall("ParameterList/PluginFloatParameter"):
            name = value(parameter, "ParameterName", "")
            if not name:
                continue
            host_id = int(value(parameter, "ParameterId", "-1"))
            target = parameter.find("ParameterValue/AutomationTarget")
            target_id = target.get("Id") if target is not None else None
            matched_envelopes = envelope_map.get(target_id, [])
            events = []
            for envelope in matched_envelopes:
                for event in envelope.findall("Automation/Events/FloatEvent"):
                    events.append({"eventId": event.get("Id"), "time": float(event.get("Time")),
                                   "normalizedValue": float(event.get("Value"))})
            arranged = [event for event in events if event["time"] >= 0]
            normalized = [event["normalizedValue"] for event in events]
            if not all(math.isfinite(event["time"]) and math.isfinite(event["normalizedValue"]) and
                       -.000001 <= event["normalizedValue"] <= 1.000001 for event in events):
                raise ValueError("Non-finite/out-of-range saved automation event")
            manual = float(value(parameter, "ParameterValue/Manual", "nan"))
            parameters.append({
                "liveSlot": int(parameter.get("Id")), "parameterName": name,
                "vst3ParameterId": host_id, "expectedJuceVst3Id": vst3_id(name),
                "boundIdMatchesJuce": host_id == vst3_id(name),
                "liveAutomationTargetId": int(target_id) if target_id is not None else None,
                "manualNormalizedValue": manual if math.isfinite(manual) else None,
                "envelopeCount": len(matched_envelopes), "floatEventCount": len(events),
                "initialSentinelEventCount": len(events) - len(arranged),
                "nonnegativeTimeEventCount": len(arranged),
                "allTimeRange": bounds([event["time"] for event in events]),
                "recordedTimeRange": bounds([event["time"] for event in arranged]),
                "normalizedValueRange": bounds(normalized), "recordedNormalizedValueRange": bounds([event["normalizedValue"] for event in arranged]), "rawParameterValueRange": raw_range(name, bounds(normalized)) if normalized else None,
                "events": events,
            })
        modes = []
        for curve in state["curves"]:
            mode = int(curve["attributes"]["mode"])
            modes.append({
                "mode": mode, "shape": state["parameters"].get(f"curveShape{mode}"),
                "phasePercent": state["parameters"].get(f"curvePhase{mode}"),
                "amountPercent": state["parameters"].get(f"curveAmount{mode}"),
                "pointCount": len(curve["points"]), "curveFlags": curve["attributes"],
            })
        plugins.append({
            "pluginOrdinal": ordinal, "trackTag": track.tag, "trackId": track.get("Id"),
            "trackName": value(track, "Name/EffectiveName", value(track, "Name/UserName", "")),
            "presetId": info.find("Preset/Vst3Preset").get("Id"),
            "schema": state["attributes"].get("schema"), "modernMask": int(state["attributes"].get("modernMask", "0")),
            "revision3Mask": int(state["attributes"].get("revision3Mask", "0")),
            "schema3CompleteParameterSet": set(state["parameters"]) == REQUIRED_IDS,
            "parameterCount": len(state["parameters"]), "mix": state["parameters"].get("mix"),
            "selected": state["parameters"].get("selected"), "trigger": state["parameters"].get("trigger"),
            "engineOn": [state["parameters"].get(f"on{i}") for i in range(6)],
            "engineSolo": [state["parameters"].get(f"solo{i}") for i in range(6)],
            "curveSlots": modes, "stateXmlSHA256": state["xmlSHA256"],
            "configuredParameters": parameters,
            "automatedLaneCount": sum(item["envelopeCount"] > 0 for item in parameters),
            "automatedFloatEventCount": sum(item["floatEventCount"] for item in parameters),
            "nonnegativeTimeEventCount": sum(item["nonnegativeTimeEventCount"] for item in parameters),
            "allRecordedBindingsMatchJuce": all(item["boundIdMatchesJuce"] for item in parameters if item["envelopeCount"]),
            "_state": state,
        })
    modern = [plugin for plugin in plugins if plugin["schema"] == "3" and plugin["revision3Mask"] == 63]
    comparison = {"available": len(modern) >= 2}
    if len(modern) >= 2:
        first, second = modern[-2:]
        a, b = first["_state"], second["_state"]
        differences = {name: [a["parameters"].get(name), b["parameters"].get(name)]
                       for name in sorted(set(a["parameters"]) | set(b["parameters"]))
                       if a["parameters"].get(name) != b["parameters"].get(name)}
        patterns = lambda plugin: {item["parameterName"]: [(event["time"], event["normalizedValue"]) for event in item["events"]]
                                   for item in plugin["configuredParameters"] if item["envelopeCount"]}
        caches = lambda plugin: {item["parameterName"]: {"vst3ParameterId": item["vst3ParameterId"], "manualNormalizedValue": item["manualNormalizedValue"]}
                                 for item in plugin["configuredParameters"]}
        ca, cb = caches(first), caches(second)
        comparison = {
            "available": True, "pluginOrdinals": [first["pluginOrdinal"], second["pluginOrdinal"]],
            "trackNames": [first["trackName"], second["trackName"]],
            "processorParameterDifferences": differences, "onlyMixDiffers": set(differences) == {"mix"},
            "otherParametersIdenticalExceptMix": set(differences) <= {"mix"} and len(a["parameters"]) == len(b["parameters"]) == len(REQUIRED_IDS),
            "comparedParameterCount": len(a["parameters"]),
            "sixCurveSlotsIdentical": a["curves"] == b["curves"],
            "bandCurvesIdentical": a["bands3"] == b["bands3"], "shapeMemoriesIdentical": a["shapeMemories3"] == b["shapeMemories3"],
            "rootAttributesIdentical": a["attributes"] == b["attributes"],
            "copiedAutomationPatternsIdentical": patterns(first) == patterns(second),
            "liveConfiguredCacheDifferences": {name: [ca.get(name), cb.get(name)] for name in sorted(set(ca) | set(cb)) if ca.get(name) != cb.get(name)},
            "cacheNote": "Live controls with ParameterId -1 have no recorded bound automation. Their cached Manual values are not the authoritative OpenDuck processor chunk.",
        }
    for plugin in plugins:
        del plugin["_state"]
    return {
        "gatheredAtUTC": datetime.now(timezone.utc).isoformat(), "set": str(path),
        "setSHA256": hashlib.sha256(original).hexdigest(), "setFileBytes": len(original),
        "setModifiedAtUTC": datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat(),
        "method": "Read-only gzip/XML inspection; exact AutomationEnvelope PointeeId -> PluginFloatParameter AutomationTarget join; independent JUCE binary XML chunk decode",
        "idReference": "JUCE utilities/juce_VST3ClientExtensions.cpp:43-50; String::hashCode() & 0x7fffffff",
        "timeNote": "Raw Ableton FloatEvent Time values. Negative initial sentinel -63072000 is counted separately from nonnegative recorded arrangement events.",
        "totalAutomationEnvelopeCount": len(envelopes), "totalFloatEventCount": len(list(xml.iter("FloatEvent"))),
        "openDuckPluginCount": len(plugins), "modernPluginCount": len(modern), "modernRevision3PluginCount": len(modern),
        "modernAutomatedLaneCount": sum(plugin["automatedLaneCount"] for plugin in modern),
        "modernFloatEventCount": sum(plugin["automatedFloatEventCount"] for plugin in modern),
        "modernNonnegativeTimeEventCount": sum(plugin["nonnegativeTimeEventCount"] for plugin in modern),
        "plugins": plugins, "lastTwoModernComparison": comparison,
        "scope": "Saved automation/state evidence only. Cloned matching automation is reported as copied, not as a second independently recorded performance. No ALS/UI/preference/system file changed.",
    }



VERSION = "0.3.1"
QA_RELATIVE = "work/Ableton-QA-v0.3.1"
GATES = {"legacy", "dsp", "quality", "audio", "ui", "ai", "bridge", "parity", "host",
         "lifecycle", "vst3_lifecycle", "state_commit", "assist_exception", "steinberg", "pluginval",
         "waveform_capture", "scope_freeze", "scope_edge", "waveform_ui"}
EXPECTED_COUNTS = {"legacy":76,"dsp":51,"quality":16,"audio":35,"ui":43,"ai":76,
                   "bridge":179,"parity":15,"host":11,"lifecycle":41,"vst3_lifecycle":82,
                   "state_commit":7,"assist_exception":6,"waveform_capture":108,
                   "scope_freeze":18,"scope_edge":21,"waveform_ui":109}
EXTRA_NAMES = {"lifecycle","vst3_lifecycle","state_commit","assist_exception",
               "waveform_capture","scope_freeze","scope_edge","waveform_ui"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def secure(path, boundary):
    path, boundary = Path(path), Path(boundary)
    require(path.is_absolute() and ".." not in path.parts and ":" not in str(path)[2:], "Explicit absolute local path required")
    for part in (path, *path.parents):
        try:
            attributes = part.lstat()
        except FileNotFoundError:
            continue
        require(not part.is_symlink() and not (getattr(attributes, "st_file_attributes", 0) & 0x400), "Reparse path forbidden")
    require(path.resolve().is_relative_to(boundary.resolve()), "Audit path escapes its named project boundary")
    return path


def amount_phase_proof(report, sector):
    require(type(sector) is int and 0 <= sector < 6, "Sector must be0..5")
    required_ids = [f"curveAmount{sector}", f"curvePhase{sector}"]
    modern = [plugin for plugin in report["plugins"] if plugin["schema"] == "3" and plugin["revision3Mask"] == 63]
    rows = []
    for plugin in modern[-2:]:
        bindings = []
        for name in required_ids:
            candidates = [parameter for parameter in plugin["configuredParameters"] if parameter["parameterName"] == name]
            parameter = candidates[0] if len(candidates) == 1 else {}
            recorded = [event for event in parameter.get("events", []) if event["time"] >= 0]
            values = [event["normalizedValue"] for event in recorded]
            correct = parameter.get("vst3ParameterId") == vst3_id(name) and parameter.get("boundIdMatchesJuce") is True
            varied = len(values) >= 2 and max(values) > min(values)
            bindings.append({"parameterId": name, "expectedJuceVst3Id": vst3_id(name),
                "boundIdCorrect": correct, "recordedEventCount": len(recorded), "recordedTimeRange": bounds([event["time"] for event in recorded]),
                "recordedNormalizedRange": bounds(values), "recordedRawRange": raw_range(name, bounds(values)) if values else None,
                "recordedValuesVary": varied, "passed": correct and varied})
        rows.append({"pluginOrdinal": plugin["pluginOrdinal"], "schema": plugin["schema"], "revision3Mask": plugin["revision3Mask"],
                     "parameterCount": plugin["parameterCount"], "complete413ParameterSet": plugin["schema3CompleteParameterSet"],
                     "bindings": bindings, "passed": plugin["schema3CompleteParameterSet"] and all(binding["passed"] for binding in bindings)})
    return {"sector": sector, "requiredIds": required_ids, "instances": rows,
            "passed": len(rows) == 2 and all(row["passed"] for row in rows),
            "copiedPatternsAreReportedAsCopied": True, "independentPerformancesClaimed": False,
            "scope": "Actual saved event/binding evidence on two revision3 instances. Duplicated matching envelopes are not claimed as two independently recorded UI performances."}


def expected_digest(value):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{64}", value),
            "An explicit expected release DLL SHA256 of 64 hex characters is required")
    return value.lower()


def validate_native(native, expected_sha256):
    require(all(type(count) is int and count > 0 for count in EXPECTED_COUNTS.values()),
            "Final waveform UI count has not been approved; this auditor cannot certify a release yet")
    require(native.get("version") == VERSION and native.get("complete") is True and
            native.get("releaseBinarySHA256") == expected_sha256 and
            set(native.get("checks", {})) == GATES,
            "Exactly nineteen completed native gates for the explicitly expected031 DLL required")
    require(native.get("expectedNativeCounts") == EXPECTED_COUNTS and
            native.get("expectedExtraCounts") == {name:EXPECTED_COUNTS[name] for name in EXTRA_NAMES} and
            all(type(value) is int for value in native["expectedNativeCounts"].values()) and
            all(type(value) is int for value in native["expectedExtraCounts"].values()),
            "Approved native/lifecycle/waveform count declarations differ")
    for name, row in native["checks"].items():
        require(row.get("passed") is True and row.get("exitCode") == 0,
                f"Native gate unresolved: {name}")
        if name in EXPECTED_COUNTS:
            require(type(row.get("count")) is int and row["count"] == EXPECTED_COUNTS[name],
                    f"Native gate count differs: {name}")
        elif name == "steinberg":
            require(type(row.get("count")) is int and row.get("count") == 47, "Base Steinberg47 count required")
    require(native.get("pairedAudioEquivalence", {}).get("passed") is True,
            "Actual paired audio equivalence gate is not complete")


def validate_release_evidence(root, path, expected_sha256):
    expected_sha256 = expected_digest(expected_sha256)
    root = Path(root)
    require(root.is_absolute() and root.is_dir(), "Explicit existing project root required")
    qa = secure(root / QA_RELATIVE, root)
    path = secure(path, root)
    require(path.is_file() and path.suffix.lower() == ".als" and path.stat().st_size <= 256 * 1024 * 1024, "Bounded explicit QA ALS required")
    baseline_path = secure(qa / "baseline.json", qa)
    baseline = json.loads(baseline_path.read_text(encoding="utf-8-sig"))
    require(baseline.get("version") == VERSION and baseline.get("baselineComplete") is True and
            Path(baseline.get("qaSet", "")).resolve() == path.resolve() and
            sha(baseline_path) == secure(qa / "baseline.json.sha256", qa).read_text(encoding="utf-8-sig").strip(), "Named031 baseline/set identity mismatch")
    require(baseline.get("fixtureOnly", False) is False, "Synthetic baseline cannot certify an actual DAW run")
    binary = secure(root / "build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3", root)
    native_path = secure(root / "build/non-daw-v0.3.1.json", root)
    native = json.loads(native_path.read_text(encoding="utf-8-sig"))
    validate_native(native, expected_sha256)
    require(baseline.get("targetBinarySHA256") == sha(binary) == expected_sha256 and
            Path(baseline.get("targetBinary", "")).resolve() == binary.resolve(),
            "Baseline/build do not match the explicit expected release hash and path")
    files = [baseline_path, secure(qa/"baseline.json.sha256",qa), binary, native_path]
    for name, row in native["checks"].items():
        log = secure(root/row.get("log", ""), root)
        executable = secure(Path(row.get("command", [""])[0]), root)
        require(log.is_file() and executable.is_file() and sha(log) == row.get("logSHA256") and
                sha(executable) == row.get("executableSHA256"), f"Native log/executable changed: {name}")
        files += [log, executable]
    loaded_path = secure(qa/"LOADED-MODULES.json",qa)
    loaded = json.loads(loaded_path.read_text(encoding="utf-8-sig"))
    checks = loaded.get("checks", {})
    require(loaded.get("schema") == "openduck-loaded-module031-v1" and
            loaded.get("version") == VERSION and loaded.get("proofPassed") is True and
            loaded.get("expectedSHA256") == expected_sha256 and
            loaded.get("nonDAWReportSHA256") == sha(native_path) and
            loaded.get("baselineSHA256") == sha(baseline_path) and
            checks.get("finalNineteenNativeGatesMatched") is True and
            checks.get("stableHostIdentityMatched") is True and
            checks.get("allLoadedOpenDuckModulesMatchFinalBinary") is True and
            checks.get("allLoadedOpenDuckModulesMatchExpectedSelectedPath") is True,
            "Actual loaded-module031 proof for the same expected release and nineteen gates required")
    auditor_path = secure(root/"tools/audit_loaded_openduck_v031.ps1",root)
    require(loaded.get("auditToolSHA256") == sha(auditor_path),
            "Loaded-module proof was not created by the current nineteen-gate auditor")
    require(type(loaded.get("processId")) is int and loaded["processId"] > 0 and
            re.fullmatch(r"\d{16,20}",str(loaded.get("processStartTimeUtcTicks",""))) and
            re.fullmatch(r"12\.4\.15(?:\.0)?",str(loaded.get("hostProductVersion",""))) and
            Path(loaded.get("expectedBinary", "")).resolve() == binary.resolve(),
            "Loaded-module stable host/build identity is incomplete")
    selected = Path(loaded.get("expectedLoadedBinary", ""))
    selected = secure(selected, selected.parent)
    require(selected.is_file() and sha(selected) == expected_sha256,
            "Selected loaded-module disk file differs from the explicit expected hash")
    records = loaded.get("loadedOpenDuckModules", [])
    require(isinstance(records,list) and len(records)>0, "No actual loaded OpenDuck modules recorded")
    for row in records:
        recorded_path = secure(Path(row.get("path", "")), selected.parent)
        require(recorded_path.resolve() == selected.resolve() and
                row.get("sha256") == expected_sha256 and row.get("productVersion") == VERSION and
                row.get("matchesTargetHash") is True and row.get("expectedSelectedPath") is True,
                "Loaded OpenDuck module record differs from the expected path/hash")
    files += [loaded_path, selected, auditor_path]
    return {"expectedSHA256":expected_sha256,"baselinePath":baseline_path,"nativePath":native_path,
            "loadedPath":loaded_path,"filesSHA256":{str(file):sha(file) for file in files}}


def final_audit(root, path, sector=0, *, expected_dll_sha256):
    root, path = Path(root), Path(path)
    evidence = validate_release_evidence(root, path, expected_dll_sha256)
    before = sha(path)
    report = audit(path)
    proof = amount_phase_proof(report, sector)
    report.update({"schema": "openduck-ableton-automation031-v1", "version": VERSION,
        "amountPhaseBindingProof": proof, "proofPassed": proof["passed"] and
            all(row["allRecordedBindingsMatchJuce"] for row in report["plugins"] if row["schema"] == "3" and row["revision3Mask"] == 63),
        "expectedReleaseBinarySHA256": evidence["expectedSHA256"],
        "nonDAWReportSHA256":sha(evidence["nativePath"]),"baselineSHA256":sha(evidence["baselinePath"]),
        "loadedModuleReportSHA256":sha(evidence["loadedPath"]),"finalNineteenNativeGatesMatched":True,
        "auditToolSHA256": sha(Path(__file__).resolve()),
        "parameterReferenceSHA256": sha(Path(__file__).resolve().with_name("gather_ableton_qa_v031.py")),
        "mappingSourceSHA256": {relative: sha(secure(root / relative, root)) for relative in (
            "Source/PluginProcessor.cpp", "CMakeLists.txt", "third_party/JUCE/modules/juce_audio_processors/utilities/juce_VST3ClientExtensions.cpp")}})
    require(before == sha(path) == report["setSHA256"], "Saved QA set changed during read-only inspection")
    require(all(sha(Path(file))==digest for file,digest in evidence["filesSHA256"].items()),
            "Release/baseline/native/loaded-module evidence changed during inspection")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--set", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-dll-sha256", required=True, type=expected_digest,
                        help="Explicit approved release SHA256, never inferred from the files being audited")
    parser.add_argument("--sector", type=int, choices=range(6), default=0,
                        help="Record curveAmountN and curvePhaseN in the two new R3 instances; default Ducking0")
    args = parser.parse_args()
    try:
        qa = secure(args.root / QA_RELATIVE, args.root)
        output = secure(args.output, qa)
        require(output.parent.resolve() == qa.resolve() and output.name == "AUTOMATION-AUDIT.json", "Use only a new031QA/AUTOMATION-AUDIT.json")
        report = final_audit(args.root, args.set, args.sector, expected_dll_sha256=args.expected_dll_sha256)
        with output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
        print(json.dumps({"audit": str(output), "proofPassed": report["proofPassed"], "bindingProof": report["amountPhaseBindingProof"]}, indent=2))
        return 0 if report["proofPassed"] else 2
    except Exception as error:
        print("Automation031 audit failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
