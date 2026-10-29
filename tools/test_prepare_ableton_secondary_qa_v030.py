"""Isolated synthetic rollback tests. Never accesses real Ableton profiles/processes."""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

SCRIPT = Path(__file__).with_name("prepare_ableton_secondary_qa_v030.ps1")
PWSH = Path(r"C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe")
NAMES = ("Preferences.cfg", "Library.cfg", "UsageData.cfg", "Options.txt")
WORKSPACE = SCRIPT.parents[3]
PROOF = WORKSPACE / "work/v030-native/qa-tools-proof" / ("secondary-profile-" + uuid.uuid4().hex)
RESULTS: list[str] = []


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, document: dict) -> None:
    path.write_text(json.dumps(document, indent=2), encoding="utf-8")


def check(condition: bool, name: str) -> None:
    if not condition:
        raise AssertionError(name)
    RESULTS.append(name)


def process(pid: int = 40001, name: str = "Ableton Live 12 Suite") -> dict:
    return {"id": pid, "processName": name, "startTimeUtc": "2026-10-28T15:07:12.9162773Z",
            "startTimeUtcTicks": "639287968329162773"}


class Case:
    def __init__(self, name: str, present: tuple[str, ...] = NAMES[:2]):
        # Each case is its own explicitly named fixture boundary.
        self.root = PROOF.parent / (PROOF.name + "-" + name)
        self.project = self.root / "project"
        self.prefs = self.root / "fake-profile/Live 12.3b10/Preferences"
        self.primary = self.project / "work/Ableton-QA-v0.3.0/baseline.json"
        self.records = self.root / "fixture-processes.json"
        self.secondary = self.primary.parent / "secondary-profile"
        self.prefs.mkdir(parents=True)
        self.primary.parent.mkdir(parents=True)
        self.identities = [process(), process(40002, "Ableton Live 11 Suite")]
        self.primary_data = {"version": "0.3.0", "baselineComplete": True,
            "fixtureOnly": True, "projectRoot": str(self.project),
            "abletonInitiallyRunning": True, "allowRunningLiveBaseline": True,
            "initialLiveProcesses": copy.deepcopy(self.identities)}
        self.save_primary()
        self.save_processes(self.identities)
        for i, name in enumerate(present):
            (self.prefs / name).write_bytes(bytes((0, 255, i, 10, 13)) + (name + " initial").encode())
        for name in ("Undo.lock", "Log.txt", "UNTOUCHED-SENTINEL.txt"):
            (self.prefs / name).write_bytes(b"SYNTHETIC NON-WHITELIST SENTINEL " + name.encode())
        self.sentinels = {n: digest(self.prefs / n) for n in ("Undo.lock", "Log.txt", "UNTOUCHED-SENTINEL.txt")}

    def save_primary(self) -> None:
        write_json(self.primary, self.primary_data)
        self.primary.with_name("baseline.json.sha256").write_text(digest(self.primary) + "\n", encoding="ascii")

    def save_processes(self, processes: list[dict]) -> None:
        write_json(self.records, {"fixtureOnly": True, "synthetic": True, "processes": processes})

    def preference_hashes(self) -> dict:
        return {p.name: digest(p) for p in self.prefs.iterdir() if p.is_file()}

    def call(self, mode: str, success: bool, extra: list[str] | None = None, fixture: bool = True) -> dict | None:
        args = [str(PWSH), "-NoProfile", "-NonInteractive", "-File", str(SCRIPT), "-Mode", mode,
            "-ProjectRoot", str(self.project), "-PreferencesRoot", str(self.prefs),
            "-PrimaryBaseline", str(self.primary)]
        if fixture:
            args += ["-Fixture", "-FixtureRoot", str(self.root), "-FixtureProcessRecords", str(self.records)]
        if extra:
            args += extra
        completed = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
        (PROOF / (f"{len(RESULTS):02d}-{self.root.name.rsplit('-', 1)[-1]}-{mode}.stdout.txt")).write_text(completed.stdout, encoding="utf-8")
        (PROOF / (f"{len(RESULTS):02d}-{self.root.name.rsplit('-', 1)[-1]}-{mode}.stderr.txt")).write_text(completed.stderr, encoding="utf-8")
        if (completed.returncode == 0) != success:
            raise AssertionError(f"{self.root.name} {mode}: exit={completed.returncode}\n{completed.stdout}\n{completed.stderr}")
        check(True, f"{self.root.name.rsplit('-', 1)[-1]} {mode}: expected {'success' if success else 'refusal'}")
        return json.loads(completed.stdout) if success else None

    def baseline(self) -> dict:
        self.call("Baseline", True)
        return json.loads((self.secondary / "FIXTURE-baseline.json").read_text(encoding="utf-8"))

    def change_saved(self, mutator) -> None:
        path = self.secondary / "FIXTURE-baseline.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        mutator(data)
        write_json(path, data)
        path.with_name(path.name + ".sha256").write_text(digest(path) + "\n", encoding="ascii")

    def restore_refused_without_writes(self) -> None:
        before = self.preference_hashes()
        self.call("Restore", False)
        check(before == self.preference_hashes(), "refused restore leaves all synthetic preference bytes unchanged")
        check(not (self.secondary / "Restore-snapshots").exists(), "failed preflight creates no reversible-backup/restore side effects")


def main() -> None:
    PROOF.mkdir(parents=True)
    # Distinct timestamps + hidden/readonly attributes, encoded binary data,
    # two initially absent files, and one initially present file removed by QA.
    good = Case("roundtrip")
    meta_code = "$p=$args[0]; foreach($n in @('Preferences.cfg','Library.cfg')) { $f=Join-Path $p $n; [IO.File]::SetCreationTimeUtc($f,[DateTime]::Parse('2025-04-03T02:01:00.1234567Z').ToUniversalTime()); [IO.File]::SetLastWriteTimeUtc($f,[DateTime]::Parse('2025-08-07T06:05:04.7654321Z').ToUniversalTime()); [IO.File]::SetAttributes($f,([IO.FileAttributes]::ReadOnly -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Archive)) }"
    meta_script = PROOF / "set-synthetic-metadata.ps1"
    meta_script.write_text(meta_code, encoding="utf-8")
    subprocess.run([str(PWSH), "-NoProfile", "-File", str(meta_script), str(good.prefs)], check=True, capture_output=True)
    baseline = good.baseline()
    primary_hash = digest(good.primary)
    check(baseline["fixtureOnly"] and not baseline["eligibleForReleaseEvidence"] and not baseline["qaValidationPerformed"], "fixture baseline is explicitly ineligible as release/DAW evidence")
    good.call("Baseline", False)
    for name in NAMES:
        path = good.prefs / name
        if path.exists():
            os.chmod(path, 0o666)
            # OPEN_EXISTING permits the deliberately hidden fixture file;
            # Python 'wb'/CREATE_ALWAYS rejects a pre-existing hidden file.
            with path.open("r+b") as stream:
                stream.write(b"SYNTHETIC GENERATED CURRENT " + name.encode())
                stream.truncate()
        else:
            path.write_bytes(b"SYNTHETIC GENERATED CURRENT " + name.encode())
    (good.prefs / "Library.cfg").unlink()
    generated = good.preference_hashes()
    report = good.call("Restore", True)
    check(report["restoreComplete"] and report["secondaryPreferencesRestoredExactly"], "restore verifies four exact bytes/metadata/absence results")
    check(report["fixtureOnly"] and not report["eligibleForReleaseEvidence"] and not report["qaValidationPerformed"], "fixture restore cannot claim real QA or release evidence")
    check(report["primaryBaselineSHA256"] == primary_hash == digest(good.primary), "primary baseline remains byte-identical and hash-bound")
    check(report["initialLiveProcesses"] == good.identities and report["betaClosed"] and report["noAdditionalLiveProcesses"], "all synthetic original PID/start identities preserved")
    check(all(x["metadataRestored"] and x["restoredExactly"] for x in report["settings"]), "all four metadata results verified by PowerShell including readonly hidden precise timestamps")
    check(all((good.prefs / n).exists() == next(x for x in baseline["settings"] if x["name"] == n)["exists"] for n in NAMES), "initial absence preserved for both generated files")
    backup = Path(report["generatedFilesBackup"])
    check(all(digest(backup / n) == generated[n] for n in NAMES if n in generated), "every generated current whitelist file backed up before restoration")
    check(all(digest(good.prefs / n) == h for n, h in good.sentinels.items()), "Undo/log/nonwhitelist sentinel bytes never changed")
    check(not (good.secondary / "baseline.json").exists() and not list(good.secondary.rglob("restore-report.json")), "fixture metadata filenames never impersonate production baseline/report")
    good.call("Restore", True)
    check(len(list((good.secondary / "Restore-snapshots").iterdir())) == 2, "repeat restore is reversible with a separate backup transaction")

    all_present = Case("allpresent", NAMES)
    all_initial = {n: digest(all_present.prefs / n) for n in NAMES}
    all_present.baseline()
    for n in NAMES:
        (all_present.prefs / n).write_bytes(b"CHANGED " + n.encode())
    all_present.call("Restore", True)
    check(all(digest(all_present.prefs / n) == h for n, h in all_initial.items()), "all four initially present binary files restored")

    absent = Case("allabsent", ())
    absent.baseline()
    for n in NAMES:
        (absent.prefs / n).write_bytes(b"NEW")
    absent.call("Restore", True)
    check(all(not (absent.prefs / n).exists() for n in NAMES), "all four initially absent files removed only after backup")

    for kind in ("extra", "missing", "reusedpid", "renamed", "beta", "badstamp", "duplicate", "badtype", "digest", "primaryfixture", "unsafeprofile", "nonsynthetic"):
        case = Case("baseline" + kind)
        actual = copy.deepcopy(case.identities)
        if kind == "extra":
            actual.append(process(40003))
        elif kind == "missing":
            actual.pop()
        elif kind == "reusedpid":
            actual[0]["startTimeUtc"] = "2026-10-28T15:07:13.9162773Z"
            actual[0]["startTimeUtcTicks"] = str(int(actual[0]["startTimeUtcTicks"]) + 10_000_000)
        elif kind == "renamed":
            actual[0]["processName"] = "Ableton Live 12 Beta"
        elif kind == "beta":
            actual.append(process(40004, "Ableton Live 12 Beta"))
            case.primary_data["initialLiveProcesses"] = copy.deepcopy(actual)
            case.save_primary()
        elif kind == "badstamp":
            actual[0]["startTimeUtcTicks"] = str(int(actual[0]["startTimeUtcTicks"]) + 1)
        elif kind == "duplicate":
            case.primary_data["initialLiveProcesses"] *= 2
            case.save_primary()
        elif kind == "badtype":
            actual[0]["id"] = "40001"
        elif kind == "digest":
            case.primary.write_text("{}", encoding="utf-8")
        elif kind == "primaryfixture":
            case.primary_data["fixtureOnly"] = False
            case.save_primary()
        elif kind == "unsafeprofile":
            case.prefs = case.root / "fake-profile/Live 12.3b9/Preferences"
            case.prefs.mkdir(parents=True)
        case.save_processes(actual)
        if kind == "nonsynthetic":
            write_json(case.records, {"fixtureOnly": True, "synthetic": False, "processes": actual})
        before = case.preference_hashes()
        case.call("Baseline", False)
        check(not case.secondary.exists() and before == case.preference_hashes(), "rejected baseline creates no snapshot folder and changes no preference bytes")

    for kind in ("primarychanged", "digest", "snapshot", "whitelist", "duplicate", "existtype", "path", "fixtureflag", "absentsnapshot", "directory", "processchanged"):
        case = Case("restore" + kind)
        case.baseline()
        for n in NAMES:
            (case.prefs / n).write_bytes(b"CURRENT " + n.encode())
        if kind == "primarychanged":
            case.primary_data["changedAfterSecondaryBaseline"] = True
            case.save_primary()
        elif kind == "digest":
            path = case.secondary / "FIXTURE-baseline.json"
            path.write_text(path.read_text(encoding="utf-8") + " ", encoding="utf-8")
        elif kind == "snapshot":
            (case.secondary / "Preferences-before/Preferences.cfg").write_bytes(b"CORRUPTED")
        elif kind == "whitelist":
            case.change_saved(lambda d: d["settings"][0].update(name="Undo.lock"))
        elif kind == "duplicate":
            case.change_saved(lambda d: d["settings"][1].update(name=d["settings"][0]["name"]))
        elif kind == "existtype":
            case.change_saved(lambda d: d["settings"][0].update(exists="true"))
        elif kind == "path":
            case.change_saved(lambda d: d.update(preferencesRoot=str(case.root / "outside")))
        elif kind == "fixtureflag":
            case.change_saved(lambda d: d.update(fixtureOnly=False, eligibleForReleaseEvidence=True))
        elif kind == "absentsnapshot":
            (case.secondary / "Preferences-before/UsageData.cfg").write_bytes(b"SHOULD NOT EXIST")
        elif kind == "directory":
            (case.prefs / "UsageData.cfg").unlink()
            (case.prefs / "UsageData.cfg").mkdir()
        elif kind == "processchanged":
            case.save_processes(case.identities + [process(40005)])
        case.restore_refused_without_writes()

    isolation = Case("nofixture")
    isolation.call("Baseline", False, fixture=False)
    isolation.call("Baseline", False, extra=["-FixtureRoot", str(isolation.root)], fixture=False)
    check(not isolation.secondary.exists(), "production mode rejects fake project/synthetic switches before real profile access")
    summary = {"fixtureOnly": True, "eligibleForReleaseEvidence": False, "realPreferencesReadOrWritten": False,
        "realLiveProcessInspectionPerformed": False, "qaValidationPerformed": False,
        "passed": len(RESULTS), "failed": 0, "scriptSHA256": digest(SCRIPT), "checks": RESULTS,
        "proofRoot": str(PROOF), "scope": "Isolated C: fake files and declared fake PID/start JSON only; no Ableton launched or real preferences accessed."}
    write_json(PROOF / "FIXTURE-RESULTS.json", summary)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
