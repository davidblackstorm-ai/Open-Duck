"""Read only the named Live set and export OpenDuck state metadata as JSON.

No Live process access, project mutation, raw project XML export or plugin load.
Direct JUCE VC2! processor states and JUCE VST3PluginState/IComponent wrappers
are supported. The optional comparison matches instances by track/device ID.
"""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET

MAX_STATE = 64 * 1024 * 1024
MAX_PROJECT = 256 * 1024 * 1024
RELEVANT = (
    "profileModel4", "duckMix4", "position4", "duration4", "curveTension0", "moveCurve4", "slope4",
    "bandEnable4", "bandCutoff4", "bandListen4", "bypass", "curveShape0",
    "template4", "mix", "curveAmount0", "curveLength0", "v3Trigger0",
    "v3SpeedMode0", "v3Speed0", "v3Hz0", "v3Loop0", "v3End0", "antiClick4",
)
OLD_DEFAULTS = {
    "profileModel4": 0, "duckMix4": 100,
    "moveCurve4": 0, "slope4": 0, "bandEnable4": 0,
    "bandCutoff4": 160, "bandListen4": 0, "bypass": 0,
}
TRACK_TAGS = {"AudioTrack", "MidiTrack", "GroupTrack", "MasterTrack", "ReturnTrack"}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def local_tag(node: ET.Element) -> str:
    return node.tag.rsplit("}", 1)[-1]


def attribute_value(node: ET.Element, path: str, default=None):
    found = node.find(path)
    return found.get("Value", default) if found is not None else default


def parse_xml(data: bytes) -> ET.Element:
    if b"<!DOCTYPE" in data.upper():
        raise ValueError("DOCTYPE is not accepted in evidence XML")
    return ET.fromstring(data)


def decode_memory_block(text: str) -> bytes:
    """JUCE MemoryBlock base64 uses a length prefix and little-endian bits."""
    text = "".join(text.split())
    match = re.fullmatch(r"(\d+)\.([.A-Za-z0-9+]*)", text)
    if match:
        length = int(match[1])
        if not 0 < length <= MAX_STATE:
            raise ValueError("Invalid IComponent MemoryBlock length")
        encoded = match[2]
        if len(encoded) != (length * 8 + 5) // 6:
            raise ValueError("Truncated IComponent MemoryBlock")
        alphabet = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
        values = {character: index for index, character in enumerate(alphabet)}
        result = bytearray()
        accumulator = bits = 0
        for character in encoded:
            accumulator |= values[character] << bits
            bits += 6
            if bits >= 8:
                result.append(accumulator & 255)
                accumulator >>= 8
                bits -= 8
        if len(result) != length:
            raise ValueError("IComponent MemoryBlock size mismatch")
        return bytes(result)
    # Some hosts export ordinary base64 instead of JUCE's MemoryBlock text.
    try:
        result = base64.b64decode(text, validate=True)
    except (ValueError, base64.binascii.Error) as error:
        raise ValueError("Unsupported IComponent text encoding") from error
    if not 0 < len(result) <= MAX_STATE:
        raise ValueError("Invalid decoded IComponent size")
    return result


def decode_processor_blob(blob: bytes, depth: int = 0):
    if depth > 3 or len(blob) > MAX_STATE:
        raise ValueError("Processor state nesting/size exceeds evidence limit")
    if blob.startswith(b"VC2!"):
        if len(blob) < 8:
            raise ValueError("Truncated JUCE binary XML header")
        length = struct.unpack_from("<I", blob, 4)[0]
        if not 0 < length <= MAX_STATE or 8 + length > len(blob):
            raise ValueError("Invalid JUCE binary XML length")
        raw = blob[8:8 + length].rstrip(b"\0")
        state = parse_xml(raw)
        method = ["JUCE VC2! binary XML"]
    elif blob.lstrip().startswith(b"<"):
        raw = blob.rstrip(b"\0")
        state = parse_xml(raw)
        method = ["XML component wrapper"]
    else:
        raise ValueError("Unsupported processor-state format")
    if local_tag(state) == "OpenDuckState":
        return state, raw, method
    component = state if local_tag(state) == "IComponent" else next(
        (child for child in state.iter() if local_tag(child) == "IComponent"), None)
    if component is None:
        raise ValueError("No OpenDuckState or IComponent in processor state")
    nested = next((child for child in component.iter()
                   if local_tag(child) == "OpenDuckState"), None)
    if nested is not None:
        return nested, ET.tostring(nested), method + ["IComponent XML child"]
    inner_blob = decode_memory_block("".join(component.itertext()))
    nested, raw, inner_method = decode_processor_blob(inner_blob, depth + 1)
    return nested, raw, method + ["IComponent MemoryBlock"] + inner_method


def numeric(text: str) -> float:
    number = float(text)
    if not math.isfinite(number):
        raise ValueError("Non-finite OpenDuck parameter")
    return number


def inspect(path: Path):
    before = path.stat()
    original = path.read_bytes()
    after = path.stat()
    if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns:
        raise ValueError("Live set changed during the read; retry the snapshot")
    if len(original) > MAX_PROJECT:
        raise ValueError("Live set exceeds evidence size limit")
    raw_xml = gzip.decompress(original) if original.startswith(b"\x1f\x8b") else original
    if len(raw_xml) > MAX_PROJECT:
        raise ValueError("Uncompressed Live set exceeds evidence size limit")
    project = parse_xml(raw_xml)
    parents = {child: parent for parent in project.iter() for child in parent}
    instances = []
    skipped = 0
    for ordinal, info in enumerate(project.iter("Vst3PluginInfo"), 1):
        declared_name = attribute_value(info, "Name", "")
        declared_openduck = "openduck" in re.sub(r"\s+", "", declared_name.lower())
        processor_node = info.find("Preset/Vst3Preset/ProcessorState")
        if processor_node is None:
            if declared_openduck:
                raise ValueError("Named OpenDuck instance has no ProcessorState")
            skipped += 1
            continue
        # Other products may also use VC2!, with unrelated/namespaced XML.
        if not declared_openduck:
            skipped += 1
            continue
        blob = bytes.fromhex("".join((processor_node.text or "").split()))
        state, state_xml, method = decode_processor_blob(blob)
        parameter_nodes = [child for child in state if local_tag(child) == "PARAM"]
        parameters = {child.get("id"): numeric(child.get("value", "nan"))
                      for child in parameter_nodes}
        if None in parameters or len(parameters) != len(parameter_nodes):
            raise ValueError("Missing/duplicate OpenDuck parameter IDs")
        selected = {name: parameters.get(name) for name in RELEVANT}
        defaults = {name: default for name, default in OLD_DEFAULTS.items()
                    if name not in parameters}
        effective = {name: parameters.get(name, OLD_DEFAULTS.get(name)) for name in RELEVANT}
        properties = {key: value for key, value in state.attrib.items()
                      if key.startswith("template") or key in
                      {"schema", "modernMask", "revision3Mask", "audioRouting"}}
        curve_parent = state.find("Curves")
        curves = []
        if curve_parent is not None:
            for curve in curve_parent.findall("Curve"):
                points = [dict(point.attrib) for point in curve.findall("Point")]
                curves.append({"mode": curve.get("mode"), "attributes": dict(curve.attrib),
                               "pointCount": len(points), "pointsSHA256": sha(canonical(points))})
        memory_parent = state.find("ShapeMemories3")
        memories = [dict(memory.attrib) for memory in memory_parent.findall("Memory")] \
            if memory_parent is not None else []
        device = info
        while device in parents and local_tag(device) != "PluginDevice":
            device = parents[device]
        track = device
        while track in parents and local_tag(track) not in TRACK_TAGS:
            track = parents[track]
        preset = info.find("Preset/Vst3Preset")
        identity = f"{local_tag(track)}:{track.get('Id', '')}/PluginDevice:{device.get('Id', '')}"
        instances.append({
            "instanceKey": identity, "pluginOrdinal": ordinal,
            "trackId": track.get("Id"), "trackTag": local_tag(track),
            "trackName": attribute_value(track, "Name/EffectiveName",
                                         attribute_value(track, "Name/UserName", "")),
            "deviceId": device.get("Id"), "pluginName": declared_name,
            "presetId": preset.get("Id") if preset is not None else None,
            "schema": int(state.get("schema", "1")),
            "parameterCount": len(parameters), "parameters": selected,
            "effectiveParameters": effective, "missingParametersUsingLegacyDefaults": defaults,
            "shape": parameters.get("curveShape0"), "templateProperties": properties,
            "engineOn": [parameters.get(f"on{i}") for i in range(6)],
            "engineSolo": [parameters.get(f"solo{i}") for i in range(6)],
            "curveSlots": curves, "shapeMemories": memories,
            "allParametersSHA256": sha(canonical(parameters)),
            "curveSlotsSHA256": sha(canonical(curves)),
            "shapeMemoriesSHA256": sha(canonical(memories)),
            "stateXmlSHA256": sha(state_xml), "processorBlobSHA256": sha(blob),
            "decodeMethod": method,
        })
    if not instances:
        raise ValueError("The named Live set contains no declared OpenDuck VST3 instances")
    identity_keys = [row["instanceKey"] for row in instances]
    if len(set(identity_keys)) != len(identity_keys):
        raise ValueError("Ambiguous OpenDuck track/device identity")
    fingerprints = {sha(canonical({"parameters": row["effectiveParameters"],
                                   "properties": row["templateProperties"],
                                   "curves": row["curveSlots"],
                                   "memories": row["shapeMemories"]})) for row in instances}
    return {
        "evidenceSchema": "openduck-ableton-state-v041-1",
        "gatheredAtUTC": datetime.now(timezone.utc).isoformat(),
        "method": "Read-only explicit .als gzip/XML and JUCE processor-state inspection",
        "set": str(path), "setSHA256": sha(original), "setFileBytes": len(original),
        "setModifiedAtUTC": datetime.fromtimestamp(after.st_mtime, timezone.utc).isoformat(),
        "projectReadOnly": True, "rawProjectXmlExported": False,
        "openDuckInstanceCount": len(instances), "otherVst3InstancesSkipped": skipped,
        "distinctRelevantStateCount": len(fingerprints),
        "isolationNote": "Distinct saved states alone do not prove live audio/process isolation.",
        "instances": instances,
    }


def compare(before: dict, after: dict):
    prior = {row["instanceKey"]: row for row in before["instances"]}
    current = {row["instanceKey"]: row for row in after["instances"]}
    changes = []
    for key in sorted(prior.keys() & current.keys()):
        a, b = prior[key], current[key]
        parameter_diff = {name: [a["effectiveParameters"].get(name), b["effectiveParameters"].get(name)]
                          for name in sorted(set(a["effectiveParameters"]) | set(b["effectiveParameters"]))
                          if a["effectiveParameters"].get(name) != b["effectiveParameters"].get(name)}
        property_diff = {name: [a["templateProperties"].get(name), b["templateProperties"].get(name)]
                         for name in sorted(set(a["templateProperties"]) | set(b["templateProperties"]))
                         if a["templateProperties"].get(name) != b["templateProperties"].get(name)}
        changes.append({"instanceKey": key, "trackName": b["trackName"],
                        "parameterDifferences": parameter_diff, "propertyDifferences": property_diff,
                        "allParametersIdentical": a["allParametersSHA256"] == b["allParametersSHA256"],
                        "curveSlotsIdentical": a["curveSlotsSHA256"] == b["curveSlotsSHA256"],
                        "shapeMemoriesIdentical": a["shapeMemoriesSHA256"] == b["shapeMemoriesSHA256"],
                        "processorBlobIdentical": a["processorBlobSHA256"] == b["processorBlobSHA256"]})
    return {"baselineSetSHA256": before["setSHA256"],
            "afterSetSHA256": after["setSHA256"],
            "addedInstances": sorted(current.keys() - prior.keys()),
            "removedInstances": sorted(prior.keys() - current.keys()),
            "instances": changes,
            "allMatchedRelevantParametersIdentical": all(not row["parameterDifferences"] for row in changes),
            "allMatchedParametersIdentical": all(row["allParametersIdentical"] for row in changes),
            "allMatchedCurvesIdentical": all(row["curveSlotsIdentical"] for row in changes),
            "allMatchedShapeMemoriesIdentical": all(row["shapeMemoriesIdentical"] for row in changes),
            "comparisonNote": "A before/after file snapshot proves saved recall only; GUI/audio observations remain separate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("set", type=Path, help="Explicit .als file to inspect; never modified")
    parser.add_argument("--output", type=Path, required=True, help="Metadata-only .json output")
    parser.add_argument("--compare", type=Path, help="Optional earlier JSON snapshot")
    args = parser.parse_args()
    path = args.set.resolve(strict=True)
    output = args.output.resolve()
    if path.suffix.lower() != ".als" or output.suffix.lower() != ".json" or output == path:
        parser.error("Expected an explicit .als input and a separate .json output")
    result = inspect(path)
    if args.compare is not None:
        baseline = json.loads(args.compare.read_text(encoding="utf-8-sig"))
        if baseline.get("evidenceSchema") != result["evidenceSchema"]:
            raise ValueError("Comparison baseline has an incompatible evidence schema")
        result["comparison"] = compare(baseline, result)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "setSHA256": result["setSHA256"],
                      "openDuckInstances": result["openDuckInstanceCount"],
                      "distinctRelevantStates": result["distinctRelevantStateCount"]}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, ET.ParseError, struct.error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
