"""Validate only the named secondary-profile rollback metadata.

This read-only gate neither opens preference contents nor attests a DAW test.
Only the exact public copy of the helper's restore report may be distributed.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import stat

VERSION = "0.3.0"
SETTINGS = {"Preferences.cfg", "Library.cfg", "UsageData.cfg", "Options.txt"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def local_metadata_file(path, boundary):
    path, boundary = Path(path), Path(boundary)
    require(path.is_absolute() and re.match(r"^[A-Za-z]:[\\/]", str(path)) and
            ":" not in str(path)[2:] and ".." not in path.parts,
            "Secondary evidence requires an explicit local metadata path")
    for part in (path, *path.parents):
        try:
            item = part.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(item.st_mode) and not (getattr(item, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)), "Secondary metadata reparse path forbidden")
    path = path.resolve()
    require(path.is_relative_to(boundary.resolve()) and path.is_file(), "Secondary metadata is missing or outside its named boundary")
    require(path.stat().st_size <= 1024 * 1024, "Secondary metadata is oversized")
    return path


def read_json(path, boundary):
    path = local_metadata_file(path, boundary)
    raw = path.read_bytes()
    value = json.loads(raw.decode("utf-8-sig"))
    require(isinstance(value, dict), "Secondary metadata must be a JSON object")
    return value, raw


def same_path(value, expected):
    require(isinstance(value, str) and value, "Missing secondary metadata path")
    supplied = PureWindowsPath(value)
    require(supplied.is_absolute() and re.match(r"^[A-Za-z]:[\\/]", value) and
            ":" not in value[2:] and not any(p in (".", "..") for p in supplied.parts),
            "Invalid secondary metadata path")
    return str(supplied).casefold() == str(PureWindowsPath(str(expected))).casefold()


def identity_keys(records):
    require(isinstance(records, list), "Original process identities must be a list")
    keys, pids = set(), set()
    for row in records:
        require(isinstance(row, dict) and type(row.get("id")) is int and row["id"] > 0 and
                isinstance(row.get("processName"), str) and row["processName"].startswith("Ableton Live") and
                "beta" not in row["processName"].casefold(), "Invalid original Live process identity")
        ticks, stamp = row.get("startTimeUtcTicks"), row.get("startTimeUtc")
        require(isinstance(ticks, str) and re.fullmatch(r"\d{16,20}", ticks) and isinstance(stamp, str),
                "Invalid original Live start timestamp")
        match = re.fullmatch(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,7}))?(?:Z|\+00:00)", stamp)
        require(match is not None, "Original Live timestamp must be UTC")
        at = datetime.fromisoformat(match[1]).replace(tzinfo=timezone.utc)
        delta = at - datetime(1, 1, 1, tzinfo=timezone.utc)
        expected = (delta.days * 86400 + delta.seconds) * 10_000_000 + int((match[2] or "").ljust(7, "0"))
        require(ticks == str(expected) and row["id"] not in pids, "Original Live ticks/PID identity is inconsistent")
        pids.add(row["id"])
        keys.add((row["id"], ticks, row["processName"]))
    require(len(keys) == len(records), "Duplicate original Live identity")
    return keys


def settings_by_name(rows):
    require(isinstance(rows, list) and len(rows) == 4 and all(isinstance(row, dict) for row in rows),
            "Exactly four secondary preference metadata records required")
    require({row.get("name") for row in rows} == SETTINGS, "Secondary setting names must be unique and whitelisted")
    return {row["name"]: row for row in rows}


def validate(root, module, post):
    """Return public report identity, or None for stable Live without a report."""
    root = Path(root)
    require(root.is_absolute() and root.is_dir(), "Explicit project root required")
    qa = root / "work/Ableton-QA-v0.3.0"
    public = qa / "SECONDARY-PROFILE-RESTORE.json"
    require(isinstance(module, dict) and isinstance(post, dict), "Module/post metadata objects required")
    process_name = module.get("processName", "")
    require(isinstance(process_name, str), "Invalid QA process name")
    required = "beta" in process_name.casefold()
    if not public.exists():
        require(not required, "Live Beta QA requires the secondary-profile rollback report")
        return None
    report, public_raw = read_json(public, qa)
    primary_path = qa / "baseline.json"
    primary, primary_raw = read_json(primary_path, qa)
    primary_hash = sha(primary_raw)
    require(primary.get("version") == VERSION and primary.get("baselineComplete") is True and
            primary.get("fixtureOnly", False) is False and same_path(primary.get("projectRoot"), root),
            "The genuine complete primary baseline is required")
    require(primary_hash == post.get("baselineSHA256") == module.get("baselineSHA256"),
            "Secondary rollback and QA module/post identify different primary baselines")
    primary_digest = local_metadata_file(qa / "baseline.json.sha256", qa).read_bytes()
    require(primary_digest.decode("utf-8-sig").strip() == primary_hash, "Primary baseline digest changed")
    initial = identity_keys(primary.get("initialLiveProcesses"))
    require(primary.get("abletonInitiallyRunning") is bool(initial) and
            (primary.get("allowRunningLiveBaseline") is True or not initial), "Primary process baseline is inconsistent")

    secondary_root = qa / "secondary-profile"
    secondary_path = secondary_root / "baseline.json"
    secondary, secondary_raw = read_json(secondary_path, secondary_root)
    secondary_hash = sha(secondary_raw)
    secondary_digest = local_metadata_file(secondary_root / "baseline.json.sha256", secondary_root).read_bytes()
    require(secondary_digest.decode("utf-8-sig").strip() == secondary_hash, "Secondary baseline digest changed")
    script = local_metadata_file(root / "tools/prepare_ableton_secondary_qa_v030.ps1", root / "tools")
    script_hash = sha(script.read_bytes())
    for record in (report, secondary):
        require(record.get("version") == VERSION and type(record.get("schema")) is int and record["schema"] == 1 and
                record.get("fixtureOnly") is False and record.get("eligibleForReleaseEvidence") is True and
                record.get("qaValidationPerformed") is False and record.get("profileVersion") == "12.3b10",
                "Secondary metadata is synthetic, wrong-version or claims validation beyond rollback")
        require(record.get("primaryBaselineSHA256") == primary_hash and
                same_path(record.get("primaryBaseline"), primary_path) and record.get("scriptSHA256") == script_hash,
                "Secondary baseline/helper provenance changed")
        require(identity_keys(record.get("initialLiveProcesses")) == initial, "Original process identities differ from primary baseline")
        preference_path = PureWindowsPath(record.get("preferencesRoot", ""))
        require(preference_path.is_absolute() and re.match(r"^[A-Za-z]:[\\/]", str(preference_path)) and
                ":" not in str(preference_path)[2:] and ".." not in preference_path.parts and
                tuple(p.casefold() for p in preference_path.parts[-2:]) == ("live 12.3b10", "preferences"),
                "Secondary metadata does not name the exact Live12.3b10 Preferences profile")
    require(same_path(report["preferencesRoot"], secondary["preferencesRoot"]) and
            same_path(secondary.get("projectRoot"), root) and secondary.get("secondaryBaselineComplete") is True and
            secondary.get("primaryBaselineDigestSHA256") == sha(primary_digest), "Secondary baseline linkage is incomplete")
    require(report.get("secondaryBaselineSHA256") == secondary_hash, "Restoration references another secondary baseline")
    for key in ("restoreComplete", "secondaryPreferencesRestoredExactly", "primaryBaselineUnchanged", "betaClosed",
                "initialProcessStatePreserved", "noAdditionalLiveProcesses"):
        require(report.get(key) is True, f"Secondary rollback check unresolved: {key}")
    saved, restored = settings_by_name(secondary.get("settings")), settings_by_name(report.get("settings"))
    for name in SETTINGS:
        before, after = saved[name], restored[name]
        require(type(before.get("exists")) is bool and after.get("initiallyExisted") is before["exists"] and
                after.get("restoredExactly") is True and after.get("metadataRestored") is True,
                f"Secondary setting did not restore bytes/metadata/absence: {name}")
        if before["exists"]:
            require(isinstance(before.get("sha256"), str) and re.fullmatch(r"[0-9a-f]{64}", before["sha256"]) and
                    type(before.get("length")) is int and before["length"] >= 0 and
                    after.get("sha256") == before["sha256"], f"Restored secondary setting fingerprint differs: {name}")
        else:
            require(all(before.get(key) is None for key in ("sha256", "length", "attributes", "creationTimeUtc", "lastWriteTimeUtc")) and
                    after.get("sha256") is None, f"Absent secondary setting has unexpected metadata: {name}")

    backup = report.get("generatedFilesBackup")
    require(isinstance(backup, str), "Missing private restore report location")
    source_report = local_metadata_file(Path(backup) / "restore-report.json", secondary_root / "Restore-snapshots")
    require(len(source_report.relative_to((secondary_root / "Restore-snapshots").resolve()).parts) == 2,
            "Restore report must be directly inside its named restore attempt")
    require(source_report.read_bytes() == public_raw, "Public secondary report is not the exact helper restore-report copy")
    return {"path": public.relative_to(root).as_posix(), "sha256": sha(public_raw), "profileVersion": "12.3b10",
            "scopeRollbackOnly": True, "qaValidationPerformed": False,
            "scope": "Four secondary beta preference files restored; metadata-only evidence, no DAW or host validation inferred."}
