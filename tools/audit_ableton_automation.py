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
    parameters = {node.get("id"): node.get("value") for node in xml.findall("PARAM")}
    curves = []
    parent = xml.find("Curves")
    if parent is not None:
        curves = [{"attributes": dict(node.attrib),
                   "points": [dict(point.attrib) for point in node.findall("Point")]}
                  for node in parent.findall("Curve")]
    return {"attributes": dict(xml.attrib), "parameters": parameters, "curves": curves,
            "xmlSHA256": hashlib.sha256(raw).hexdigest(), "blobSHA256": hashlib.sha256(blob).hexdigest()}


def raw_range(name, normalized):
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
        state = processor_state(info)
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
            parameters.append({
                "liveSlot": int(parameter.get("Id")), "parameterName": name,
                "vst3ParameterId": host_id, "expectedJuceVst3Id": vst3_id(name),
                "boundIdMatchesJuce": host_id == vst3_id(name),
                "liveAutomationTargetId": int(target_id) if target_id is not None else None,
                "manualNormalizedValue": float(value(parameter, "ParameterValue/Manual", "nan")),
                "envelopeCount": len(matched_envelopes), "floatEventCount": len(events),
                "initialSentinelEventCount": len(events) - len(arranged),
                "nonnegativeTimeEventCount": len(arranged),
                "allTimeRange": bounds([event["time"] for event in events]),
                "recordedTimeRange": bounds([event["time"] for event in arranged]),
                "normalizedValueRange": bounds(normalized), "rawParameterValueRange": raw_range(name, bounds(normalized)) if normalized else None,
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
    modern = [plugin for plugin in plugins if plugin["modernMask"] == 63]
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
            "other130ParametersIdentical": set(differences) == {"mix"} and len(a["parameters"]) == len(b["parameters"]) == 131,
            "sixCurveSlotsIdentical": a["curves"] == b["curves"],
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
        "idReference": "JUCE juce_VST3ClientExtensions.cpp:43-50; String::hashCode() & 0x7fffffff",
        "timeNote": "Raw Ableton FloatEvent Time values. Negative initial sentinel -63072000 is counted separately from nonnegative recorded arrangement events.",
        "totalAutomationEnvelopeCount": len(envelopes), "totalFloatEventCount": len(list(xml.iter("FloatEvent"))),
        "openDuckPluginCount": len(plugins), "modernPluginCount": len(modern),
        "modernAutomatedLaneCount": sum(plugin["automatedLaneCount"] for plugin in modern),
        "modernFloatEventCount": sum(plugin["automatedFloatEventCount"] for plugin in modern),
        "modernNonnegativeTimeEventCount": sum(plugin["nonnegativeTimeEventCount"] for plugin in modern),
        "plugins": plugins, "lastTwoModernComparison": comparison,
        "scope": "Saved automation/state evidence only. Cloned matching automation is reported as copied, not as a second independently recorded performance. No ALS/UI/preference/system file changed.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--set", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() == args.set.resolve() or args.output.suffix.lower() != ".json":
        raise ValueError("Audit output must be a separate JSON file")
    report = audit(args.set)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"audit": str(args.output), "setSHA256": report["setSHA256"],
                      "modernLaneCount": report["modernAutomatedLaneCount"],
                      "modernFloatEvents": report["modernFloatEventCount"],
                      "comparison": report["lastTwoModernComparison"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
