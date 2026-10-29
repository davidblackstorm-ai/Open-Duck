#!/usr/bin/env python3
"""Compare explicitly supplied dry/wet DAW exports without alignment or resampling.

The default gates require a relative difference RMS > 0.03, a 20 ms wet/dry RMS
ratio < 0.10, recovery > 0.75, at least four low-to-recovery episodes, and a
coefficient of variation <= 0.25 between low episode starts. These are objective
checks for a repetitive default pumping envelope, not a listening assessment or
a measurement of host PDC. Supply --bpm to check the expected cycle period too.

Only --output is written, using exclusive creation in an existing directory.
WAV inputs are never changed. PCM16/24/32 and IEEE float32/64 RIFF WAVE are read.
"""

from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct
import sys


class WaveError(ValueError):
    pass


def read_wave(path: Path) -> tuple[dict, array]:
    """Decode little-endian RIFF WAVE, including PCM/float extensible subtypes."""
    raw = path.read_bytes()
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise WaveError(f"{path.name}: expected a little-endian RIFF WAVE file")
    riff_end = struct.unpack_from("<I", raw, 4)[0] + 8
    if riff_end > len(raw):
        raise WaveError(f"{path.name}: truncated RIFF container")
    fmt = None
    payloads = []
    offset = 12
    while offset + 8 <= riff_end:
        kind, length = struct.unpack_from("<4sI", raw, offset)
        begin, end = offset + 8, offset + 8 + length
        if end > riff_end:
            raise WaveError(f"{path.name}: truncated {kind!r} chunk")
        if kind == b"fmt ":
            if fmt is not None:
                raise WaveError(f"{path.name}: multiple format chunks are unsupported")
            fmt = raw[begin:end]
        elif kind == b"data":
            payloads.append(raw[begin:end])
        offset = end + (length & 1)
    if fmt is None or len(fmt) < 16 or not payloads:
        raise WaveError(f"{path.name}: missing format or audio data")
    encoding, channels, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", fmt)
    valid_bits = bits
    if encoding == 0xFFFE:
        if len(fmt) < 40 or struct.unpack_from("<H", fmt, 16)[0] < 22:
            raise WaveError(f"{path.name}: incomplete extensible format")
        valid_bits = struct.unpack_from("<H", fmt, 18)[0]
        guid = fmt[24:40]
        if guid[4:] != bytes.fromhex("00001000800000aa00389b71"):
            raise WaveError(f"{path.name}: unsupported extensible subtype GUID")
        encoding = struct.unpack_from("<I", guid)[0]
    supported = (encoding == 1 and bits in (16, 24, 32)) or (encoding == 3 and bits in (32, 64))
    if not supported or not 1 <= channels <= 32 or not 1 <= rate <= 768000:
        raise WaveError(f"{path.name}: unsupported encoding, channel count or sample rate")
    if not 1 <= valid_bits <= bits or align != channels * (bits // 8) or byte_rate != rate * align:
        raise WaveError(f"{path.name}: inconsistent PCM format fields")
    payload = b"".join(payloads)
    if not payload or len(payload) % align:
        raise WaveError(f"{path.name}: empty or incomplete audio frames")
    values = array("d")
    if encoding == 1 and bits == 24:
        for n in range(0, len(payload), 3):
            value = payload[n] | payload[n + 1] << 8 | payload[n + 2] << 16
            if value & 0x800000:
                value -= 0x1000000
            values.append(value / 8388608.0)
    else:
        code = {16: "h", 32: "i"}[bits] if encoding == 1 else {32: "f", 64: "d"}[bits]
        scale = float(1 << (bits - 1)) if encoding == 1 else 1.0
        values.extend(value[0] / scale for value in struct.iter_unpack("<" + code, payload))
    if any(not math.isfinite(value) for value in values):
        raise WaveError(f"{path.name}: non-finite audio samples")
    metadata = {
        "path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
        "encoding": "PCM integer" if encoding == 1 else "IEEE float", "bits": bits,
        "validBits": valid_bits, "sampleRate": rate, "channels": channels,
        "frames": len(payload) // align, "durationSeconds": len(payload) / align / rate,
    }
    return metadata, values


def rms(values) -> float:
    return math.sqrt(math.fsum(value * value for value in values) / len(values))


def db(value: float):
    return 20 * math.log10(value) if value > 0 else None


def quantile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = fraction * (len(ordered) - 1)
    low = int(position)
    return ordered[low] + (ordered[min(low + 1, len(ordered) - 1)] - ordered[low]) * (position - low)


def compare(dry_meta: dict, dry: array, wet_meta: dict, wet: array, criteria: dict) -> dict:
    for field in ("frames", "sampleRate", "channels"):
        if dry_meta[field] != wet_meta[field]:
            raise WaveError(f"Dry/wet {field} differ: {dry_meta[field]} versus {wet_meta[field]}")
    rate, channels = dry_meta["sampleRate"], dry_meta["channels"]
    dry_rms, wet_rms = rms(dry), rms(wet)
    difference = array("d", (b - a for a, b in zip(dry, wet)))
    diff_rms = rms(difference)
    relative_difference = diff_rms / dry_rms if dry_rms > 0 else None
    frames_per_window = max(1, round(rate * criteria["windowMs"] / 1000))
    samples_per_window = frames_per_window * channels
    floor = max(criteria["minDryWindowRms"], dry_rms * criteria["minDryWindowRelative"])
    windows, ratios = [], []
    for begin in range(0, len(dry), samples_per_window):
        end = min(begin + samples_per_window, len(dry))
        a, b = rms(dry[begin:end]), rms(wet[begin:end])
        full = end - begin == samples_per_window
        eligible = full and a >= floor and a > 0
        ratio = b / a if eligible else None
        if ratio is not None:
            ratios.append(ratio)
        windows.append({
            "startSeconds": begin / channels / rate, "frames": (end - begin) // channels,
            "dryRms": a, "wetRms": b, "wetDryRmsRatio": ratio,
            "eligible": eligible,
        })
    low_runs = []
    for n, window in enumerate(windows):
        ratio = window["wetDryRmsRatio"]
        if ratio is not None and ratio < criteria["lowRatio"]:
            if low_runs and low_runs[-1][1] == n - 1:
                low_runs[-1][1] = n
            else:
                low_runs.append([n, n])
    # Suppress multiple threshold crossings within a fraction of one kick cycle.
    runs = []
    for run in low_runs:
        if runs and (windows[run[0]]["startSeconds"] - windows[runs[-1][0]]["startSeconds"]) * 1000 < criteria["minCycleMs"]:
            runs[-1][1] = run[1]
        else:
            runs.append(run)
    episodes = []
    for n, (begin, end) in enumerate(runs):
        until = runs[n + 1][0] if n + 1 < len(runs) else len(windows)
        recovery = next((i for i in range(end + 1, until)
                         if windows[i]["wetDryRmsRatio"] is not None
                         and windows[i]["wetDryRmsRatio"] > criteria["recoveryRatio"]), None)
        episodes.append({
            "lowStartSeconds": windows[begin]["startSeconds"],
            "lowEndSeconds": windows[end]["startSeconds"] + windows[end]["frames"] / rate,
            "minRatio": min(windows[i]["wetDryRmsRatio"] for i in range(begin, end + 1)
                            if windows[i]["wetDryRmsRatio"] is not None),
            "recoverySeconds": windows[recovery]["startSeconds"] if recovery is not None else None,
            "recoveryRatio": windows[recovery]["wetDryRmsRatio"] if recovery is not None else None,
            "complete": recovery is not None,
        })
    periods = [(b["lowStartSeconds"] - a["lowStartSeconds"]) * 1000 for a, b in zip(episodes, episodes[1:])]
    period_mean = statistics.fmean(periods) if periods else None
    period_cv = statistics.pstdev(periods) / period_mean if periods and period_mean > 0 else None
    period_median = statistics.median(periods) if periods else None
    expected = 60000 / criteria["bpm"] * criteria["beatsPerCycle"] if criteria["bpm"] is not None else None
    checks = {
        "matchingFramesRateChannels": True,
        "finiteSamples": True,
        "nonSilentDry": dry_rms > criteria["minDryRms"],
        "audibleDifferenceProxy": relative_difference is not None and relative_difference > criteria["minDifferenceRatio"],
        "lowGainPresent": bool(ratios) and min(ratios) < criteria["lowRatio"],
        "recoveryPresent": bool(ratios) and max(ratios) > criteria["recoveryRatio"],
        "enoughCompleteCycles": sum(episode["complete"] for episode in episodes) >= criteria["minCycles"],
        "periodicLowGain": period_cv is not None and period_cv <= criteria["maxPeriodCv"],
    }
    if expected is not None:
        checks["expectedCyclePeriod"] = period_median is not None and abs(period_median - expected) / expected <= criteria["expectedPeriodTolerance"]
    return {
        "passed": all(checks.values()), "checks": checks,
        "metrics": {
            "dryRms": dry_rms, "dryRmsDbfs": db(dry_rms), "dryPeak": max(map(abs, dry)),
            "wetRms": wet_rms, "wetRmsDbfs": db(wet_rms), "wetPeak": max(map(abs, wet)),
            "differenceRms": diff_rms, "differenceRmsDbfs": db(diff_rms),
            "differencePeak": max(map(abs, difference)), "differenceToDryRms": relative_difference,
            "wetToDryRms": wet_rms / dry_rms if dry_rms > 0 else None,
            "eligibleWindows": len(ratios), "ignoredWindows": len(windows) - len(ratios),
            "dryWindowRmsFloor": floor, "actualWindowMs": frames_per_window / rate * 1000,
            "windowRatioQuantiles": {name: quantile(ratios, fraction) for name, fraction in
                                     (("min", 0), ("p05", .05), ("p25", .25), ("p50", .5), ("p75", .75), ("p95", .95), ("max", 1))},
            "completeCycles": sum(episode["complete"] for episode in episodes),
            "periodsMs": periods, "medianPeriodMs": period_median,
            "periodCoefficientOfVariation": period_cv, "expectedPeriodMs": expected,
        },
        "episodes": episodes, "windows": windows,
    }


def positive(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dry", type=Path, required=True, help="Unmodified MIX0 export")
    p.add_argument("--wet", type=Path, required=True, help="MIX100/Amount100 export of the same source/range")
    p.add_argument("--output", type=Path, required=True, help="New JSON in an existing QA directory; refuses overwrite")
    p.add_argument("--window-ms", type=positive, default=20)
    p.add_argument("--min-difference-ratio", type=positive, default=.03)
    p.add_argument("--low-ratio", type=positive, default=.1)
    p.add_argument("--recovery-ratio", type=positive, default=.75)
    p.add_argument("--min-cycles", type=int, default=4)
    p.add_argument("--max-period-cv", type=positive, default=.25)
    p.add_argument("--min-cycle-ms", type=positive, default=100)
    p.add_argument("--min-dry-rms", type=positive, default=1e-7)
    p.add_argument("--min-dry-window-rms", type=positive, default=1e-5)
    p.add_argument("--min-dry-window-relative", type=positive, default=.01)
    p.add_argument("--bpm", type=positive, help="Optional known export tempo for an additional cycle-period gate")
    p.add_argument("--beats-per-cycle", type=positive, default=1, help="Quarter-note beats per envelope cycle;1=1/4")
    p.add_argument("--expected-period-tolerance", type=positive, default=.2)
    return p


def main(argv=None) -> int:
    p = parser()
    args = p.parse_args(argv)
    if args.min_cycles < 4:
        p.error("--min-cycles must be at least4")
    if args.low_ratio >= args.recovery_ratio:
        p.error("--low-ratio must be lower than --recovery-ratio")
    if args.output.suffix.lower() != ".json" or not args.output.parent.is_dir():
        p.error("--output must name a JSON file in an existing QA directory")
    if args.output.exists() or args.output.resolve() in (args.dry.resolve(), args.wet.resolve()):
        p.error("--output must be a new file, separate from both WAV inputs")
    criteria = {
        "windowMs": args.window_ms, "minDifferenceRatio": args.min_difference_ratio,
        "lowRatio": args.low_ratio, "recoveryRatio": args.recovery_ratio,
        "minCycles": args.min_cycles, "maxPeriodCv": args.max_period_cv,
        "minCycleMs": args.min_cycle_ms, "minDryRms": args.min_dry_rms,
        "minDryWindowRms": args.min_dry_window_rms, "minDryWindowRelative": args.min_dry_window_relative,
        "bpm": args.bpm, "beatsPerCycle": args.beats_per_cycle,
        "expectedPeriodTolerance": args.expected_period_tolerance,
    }
    report = {
        "schema": "openduck-ableton-export-comparison-v1", "criteria": criteria,
        "limitations": ["No alignment, shifting, resampling or gain normalization was performed.",
                        "This comparison does not establish PDC accuracy or sample alignment.",
                        "The difference gate is a signal metric, not a claim of human listening or perceptual quality.",
                        "Export range, track/sidechain routing and DAW settings require separate recorded evidence."],
    }
    code = 2
    try:
        dry_meta, dry = read_wave(args.dry)
        wet_meta, wet = read_wave(args.wet)
        report.update({"dry": dry_meta, "wet": wet_meta})
        report.update(compare(dry_meta, dry, wet_meta, wet, criteria))
        code = 0 if report["passed"] else 1
    except (OSError, ValueError, struct.error) as error:
        report.update({"passed": False, "error": str(error)})
    # Exclusive creation also protects against a file appearing after validation.
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(report, output, indent=2, ensure_ascii=False, allow_nan=False)
        output.write("\n")
    print(("PASS" if report["passed"] else "FAIL") + " " + str(args.output.resolve()))
    if "checks" in report:
        for name, passed in report["checks"].items():
            print(("PASS " if passed else "FAIL ") + name)
    if "error" in report:
        print(report["error"], file=sys.stderr)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
