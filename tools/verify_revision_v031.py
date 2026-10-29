"""Run 0.3.1 non-DAW and exact-host-hotfix checks against one fingerprinted Release build.

This tool never launches a DAW or installs a system plugin. A partial development
run is marked incomplete and cannot satisfy the release packaging gate.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
from verify_quality_audio031 import validate as validate_quality_audio
from quality_host_hotfix_lineage031 import validate as validate_model_lineage
from verify_host_hotfix_audio_equivalence031 import validate as validate_audio_equivalence, requested as request_audio_equivalence

VERSION = '0.3.1'
NATIVE = {'legacy': 'OpenDuckTests', 'dsp': 'Revision030DspTests',
          'quality': 'Revision030QualityTests', 'audio': 'Revision030QualityAudit',
          'ui': 'Revision030UiTests', 'ai': 'Revision030AiTests',
          'bridge': 'Revision030AssistBridgeTests', 'parity': 'AssistModelParity',
          'host': 'OpenDuckHostContract', 'lifecycle': 'Revision031LifecycleTests',
          'vst3_lifecycle': 'Revision031Vst3LifecycleTests', 'state_commit': 'Revision031StateCommitTests',
          'assist_exception': 'Revision031AssistExceptionTests',
          'waveform_capture': 'Revision031WaveformCaptureTests', 'scope_freeze': 'Revision031ScopeFreezeTests',
          'scope_edge': 'Revision031ScopeEdgeTests', 'waveform_ui': 'Revision031WaveformUiTests'}
WAVEFORM_COUNTS = {'waveform_capture': 108, 'scope_freeze': 18, 'scope_edge': 21, 'waveform_ui': 109}
EXPECTED_EXTRA_COUNTS = {'lifecycle': 41, 'vst3_lifecycle': 82, 'state_commit': 7, 'assist_exception': 6, **WAVEFORM_COUNTS}
EXPECTED_NATIVE_COUNTS = {'legacy': 76, 'dsp': 51, 'quality': 16, 'audio': 35, 'ui': 43,
                          'ai': 76, 'bridge': 179, 'parity': 15, 'host': 11, **EXPECTED_EXTRA_COUNTS}
CONFIGURATION_FILES = ('CMakeLists.txt', 'cmake/OpenDuckWindowsResources.cmake')
HOTFIX_FIRST = ('lifecycle', 'state_commit', 'assist_exception', 'vst3_lifecycle')
WAVEFORM_FIRST = tuple(WAVEFORM_COUNTS)
ALL = HOTFIX_FIRST + WAVEFORM_FIRST + tuple(name for name in NATIVE if name not in HOTFIX_FIRST + WAVEFORM_FIRST) + ('steinberg', 'pluginval')

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def all_result_records_clean(text):
    records = re.findall(r'RESULT\s+(\d+)\s+passed,\s*(\d+)\s+failed', text)
    return all(int(passed) > 0 and int(failed) == 0 for passed, failed in records)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--checks', nargs='+', choices=ALL, default=list(ALL))
    parser.add_argument('--timeout', type=int, default=1200)
    args = parser.parse_args()
    root = args.root.resolve(); build = root / 'build'
    bundle = build / 'OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3'
    binary = bundle / 'Contents/x86_64-win/OpenDuck Pro X.vst3'
    if not binary.is_file(): raise RuntimeError('Build the Release VST3 first')
    moduleinfo = bundle / 'Contents/Resources/moduleinfo.json'
    versions = re.findall(r'"Version"\s*:\s*"([^"]+)"', moduleinfo.read_text(encoding='utf-8-sig'))
    if not versions or set(versions) != {VERSION}: raise RuntimeError('Bundle metadata is not the requested hotfix version')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    attempt = root / 'work/v0.3.1/verification-attempts' / stamp
    attempt.mkdir(parents=True, exist_ok=False)
    preview = build / f'visual-qa-v{VERSION}'; preview.mkdir(exist_ok=True)
    proof = {'version': VERSION, 'startedUTC': stamp, 'releaseBinarySHA256': sha(binary),
             'complete': False, 'abletonRun': False, 'productionInstallPerformed': False,
             'checks': {}, 'sourceSHA256': {p.relative_to(root).as_posix(): sha(p)
                 for directory in ('Source', 'tests')
                 for p in sorted((root / directory).rglob('*')) if p.is_file()}}
    proof['inputEvidenceSHA256'] = {name: sha(root / name) for name in (
        'docs/research-v0.3.0/golden.csv', 'docs/research-v0.3.0/training.json',
        'docs/research-v0.3.0/teacher-final/manifest.json',
        'work/hotfix031/waveform/SCOPE-EXACT-TRANSFORMS.json') if (root / name).is_file()}
    proof['sourcePolicy'] = 'Exact frozen R2 data/weights and eight unchanged sources plus reviewed byte-exact host/worker/scope exceptions; independent paired audio required'
    proof['configurationSHA256'] = {name: sha(root / name) for name in CONFIGURATION_FILES}
    proof['bundleMetadataSHA256'] = sha(moduleinfo)
    proof['expectedExtraCounts'] = EXPECTED_EXTRA_COUNTS
    proof['expectedNativeCounts'] = EXPECTED_NATIVE_COUNTS
    proof['verifierToolSHA256'] = {f'tools/{name}': sha(Path(__file__).resolve().with_name(name)) for name in (
        'verify_revision_v031.py', 'quality_host_hotfix_lineage031.py', 'verify_quality_audio031.py',
        'verify_host_hotfix_audio_equivalence031.py')}
    paired_path = build / f'host-hotfix-audio-equivalence-v{VERSION}.json'
    if paired_path.is_file(): (attempt / paired_path.name).write_bytes(paired_path.read_bytes())
    proof['pairedAudioEquivalence'] = request_audio_equivalence(build / 'quality-audio-v0.3.0', build / f'quality-audio-v{VERSION}')
    paired_path.write_text(json.dumps(proof['pairedAudioEquivalence'], ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if set(args.checks) == set(ALL):
        lineage = validate_model_lineage(root)
        lineage_path = build / f'ai-quality-lineage-v{VERSION}.json'
        lineage_path.write_text(json.dumps(lineage, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        proof.setdefault('generatedEvidenceSHA256', {})[lineage_path.relative_to(root).as_posix()] = sha(lineage_path)
    commands = {}
    for key, target in NATIVE.items():
        executable = build / f'{target}_artefacts/Release/{target}.exe'
        commands[key] = [str(executable), str(preview)]
    commands['host'] = [commands['host'][0], str(bundle)]
    commands['lifecycle'] = [commands['lifecycle'][0], str(root / 'tests/fixtures')]
    commands['vst3_lifecycle'] = [commands['vst3_lifecycle'][0], str(bundle), str(root / 'tests/fixtures'), VERSION]
    commands['state_commit'] = [commands['state_commit'][0]]
    commands['assist_exception'] = [commands['assist_exception'][0]]
    commands['waveform_capture'] = [commands['waveform_capture'][0], str(preview / 'scope-capture031-measures.json')]
    commands['scope_freeze'] = [commands['scope_freeze'][0]]
    commands['scope_edge'] = [commands['scope_edge'][0]]
    commands['waveform_ui'] = [commands['waveform_ui'][0], str(root / 'tests/fixtures/audio/LAB-Kick-145.wav'), str(preview)]
    audio_directory = build / f'quality-audio-v{VERSION}'
    commands['audio'] = [commands['audio'][0], str(root / 'tests/fixtures/audio'), str(audio_directory)]
    commands['parity'] = [commands['parity'][0], '--golden', str(root / 'docs/research-v0.3.0/golden.csv'),
        '--audio-report', str(build / f'ai-heldout-v{VERSION}.json'), '--heldout-scenes', '12']
    commands['steinberg'] = [str(build / 'validator/bin/Release/validator.exe'), str(bundle)]
    commands['pluginval'] = [str(root / 'tools/pluginval/pluginval.exe'), '--strictness-level', '10', '--validate', str(bundle)]
    names = {'legacy': f'tests-v{VERSION}.log', 'host': f'host-contract-v{VERSION}.log'}
    failure = None
    for name in args.checks:
        command = commands[name]
        if not Path(command[0]).is_file(): failure = f'Missing executable: {command[0]}'; break
        # A successful log must not certify a leftover waveform attachment if
        # the current run could not write it. Only these owned outputs are reset.
        fresh_waveform_outputs = {'waveform_capture': preview / 'scope-capture031-measures.json',
                                  'waveform_ui': preview / 'native-waveform-width1010.png'}
        if name in fresh_waveform_outputs:
            previous = fresh_waveform_outputs[name]
            if previous.is_file():
                (attempt / previous.name).write_bytes(previous.read_bytes())
                previous.unlink()
        destination = build / names.get(name, f'{name}-v{VERSION}.log')
        if destination.is_file(): (attempt / destination.name).write_bytes(destination.read_bytes())
        started = time.monotonic()
        check_timeout = min(args.timeout, 60) if name in {'lifecycle', 'vst3_lifecycle', 'state_commit', *WAVEFORM_FIRST} else min(args.timeout, 180) if name == 'assist_exception' else args.timeout
        print(f'RUN {name}', flush=True)
        with destination.open('wb') as output:
            try:
                job = subprocess.run(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                     timeout=check_timeout, creationflags=subprocess.CREATE_NO_WINDOW)
                code = job.returncode
            except subprocess.TimeoutExpired:
                code = -1
        text = destination.read_text(encoding='utf-8-sig', errors='replace')
        # Some Windows validators write a UTF-16 log via their CRT.
        if destination.read_bytes().startswith((b'\xff\xfe', b'\xfe\xff')):
            text = destination.read_text(encoding='utf-16')
        count = re.findall(r'RESULT\s+(\d+)\s+passed,\s*(\d+)\s+failed', text)
        if name == 'steinberg':
            ok = code == 0 and re.search(r'Result:\s*47\s+tests passed,\s*0\s+tests failed', text)
            passed = 47 if ok else 0
        elif name == 'pluginval':
            ok = code == 0 and text.rstrip().endswith('SUCCESS') and re.search(r'Strictness level:\s*10\b', text)
            passed = None
        elif name == 'audio':
            ok = code == 0 and re.search(r'RENDERED\s+35\s+cases;', text)
            passed = 35 if ok else 0
            if ok:
                try:
                    audio_proof = validate_quality_audio(root, audio_directory)
                    ok = audio_proof.get('passed') is True
                    audio_proof_path = build / f'quality-audio-v{VERSION}.json'
                    audio_proof_path.write_text(json.dumps(audio_proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
                except (ValueError, RuntimeError, OSError) as error:
                    ok = False
                    proof['audioValidationError'] = str(error)
        else:
            ok = code == 0 and count and int(count[-1][0]) > 0 and int(count[-1][1]) == 0
            passed = int(count[-1][0]) if count else 0
            if name in EXPECTED_NATIVE_COUNTS and EXPECTED_NATIVE_COUNTS[name] is not None:
                ok = bool(ok) and passed == EXPECTED_NATIVE_COUNTS[name]
        ok = bool(ok) and all_result_records_clean(text) and not re.search(r'^FAIL\s', text, re.MULTILINE)
        proof['checks'][name] = {'passed': ok, 'count': passed, 'exitCode': code,
            'seconds': round(time.monotonic()-started, 3), 'command': command,
            'log': destination.relative_to(root).as_posix(), 'logSHA256': sha(destination),
            'executableSHA256': sha(command[0])}
        if name == 'audio' and ok:
            try:
                paired = validate_audio_equivalence(root, build / 'quality-audio-v0.3.0', audio_directory,
                    new_binary_sha256=proof['releaseBinarySHA256'], new_renderer_sha256=sha(command[0]),
                    renderer_log_sha256=sha(destination))
                paired_path.write_text(json.dumps(paired, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
                proof['pairedAudioEquivalence'] = paired
                proof['checks'][name]['pairedAudioEquivalencePassed'] = paired.get('passed') is True
                ok = paired.get('passed') is True
            except (ValueError, RuntimeError, OSError) as error:
                ok = False
                proof['pairedAudioEquivalence']['status'] = 'failed'
                proof['pairedAudioEquivalence']['error'] = str(error)
                proof['checks'][name]['pairedAudioEquivalencePassed'] = False
                paired_path.write_text(json.dumps(proof['pairedAudioEquivalence'], ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
            proof['checks'][name]['passed'] = bool(ok)
        if name == 'steinberg' and ok:
            # SDK3.8.1 registers537 tests with -e, versus47 in its basic suite.
            # Exercise both supported lifetimes against the exact same DLL.
            variants = proof['checks'][name]['variants'] = {}
            for label, flags in (('extensive-global', ['-e']), ('extensive-local', ['-e', '-l'])):
                variant_command = [command[0], *flags, str(bundle)]
                variant_log = build / f'steinberg-{label}-v{VERSION}.log'
                if variant_log.is_file(): (attempt / variant_log.name).write_bytes(variant_log.read_bytes())
                variant_started = time.monotonic()
                with variant_log.open('wb') as output:
                    try:
                        job = subprocess.run(variant_command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                             timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW)
                        variant_code = job.returncode
                    except subprocess.TimeoutExpired:
                        variant_code = -1
                raw = variant_log.read_bytes()
                variant_text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig', errors='replace')
                variant_ok = variant_code == 0 and bool(re.search(r'Result:\s*537\s+tests passed,\s*0\s+tests failed', variant_text))
                variants[label] = {'passed': variant_ok, 'count': 537 if variant_ok else 0,
                    'exitCode': variant_code, 'seconds': round(time.monotonic()-variant_started, 3),
                    'command': variant_command, 'log': variant_log.relative_to(root).as_posix(),
                    'logSHA256': sha(variant_log), 'executableSHA256': sha(command[0])}
                print(f'{"PASS" if variant_ok else "FAIL"} Steinberg {label}: {variants[label]["count"]} checks', flush=True)
                if not variant_ok or sha(binary) != proof['releaseBinarySHA256']:
                    ok = False
                    proof['checks'][name]['passed'] = False
                    break
        generated = []
        if name == 'parity': generated.append(build / f'ai-heldout-v{VERSION}.json')
        if name == 'dsp': generated.append(preview / 'dsp030-measures.json')
        if name == 'bridge': generated.append(preview / 'assist-bridge-summary.json')
        if name == 'waveform_capture': generated.append(preview / 'scope-capture031-measures.json')
        if name == 'waveform_ui': generated.append(preview / 'native-waveform-width1010.png')
        if name == 'audio': generated.extend([build / f'quality-audio-v{VERSION}.json', paired_path])
        for path in generated:
            if not path.is_file():
                ok = False
                proof['checks'][name]['passed'] = False
                proof['checks'][name]['missingGeneratedEvidence'] = str(path)
                break
            proof.setdefault('generatedEvidenceSHA256', {})[path.relative_to(root).as_posix()] = sha(path)
        print(f'{"PASS" if ok else "FAIL"} {name}: {passed} checks, exit {code}', flush=True)
        if not ok: failure = f'{name} failed; see {destination}'; break
        if sha(binary) != proof['releaseBinarySHA256']: failure = 'Release binary changed during verification'; break
    # A single coherent build/source/evidence snapshot is mandatory across the run.
    current_sources = {p.relative_to(root).as_posix(): sha(p)
        for directory in ('Source', 'tests') for p in sorted((root / directory).rglob('*')) if p.is_file()}
    if current_sources != proof['sourceSHA256']: failure = failure or 'Source/tests changed during verification'
    if {name: sha(root / name) for name in CONFIGURATION_FILES} != proof['configurationSHA256']:
        failure = failure or 'CMake configuration/helper changed during verification'
    if any(not (root/name).is_file() or sha(root/name) != expected for name, expected in proof['inputEvidenceSHA256'].items()):
        failure = failure or 'R2 input evidence changed during verification'
    if any(sha(Path(__file__).resolve().with_name(Path(name).name)) != expected for name, expected in proof['verifierToolSHA256'].items()):
        failure = failure or 'Verifier tools changed during verification'
    if sha(moduleinfo) != proof['bundleMetadataSHA256']: failure = failure or 'Bundle metadata changed during verification'
    if set(args.checks) == set(ALL) and any(value is None for value in EXPECTED_EXTRA_COUNTS.values()):
        failure = failure or 'Final counts for all eight extra hotfix/waveform regressions must be explicitly approved/pinned; current run remains development only'
    if set(args.checks) == set(ALL) and not failure:
        try:
            final_lineage = validate_model_lineage(root)
            if final_lineage != lineage: failure = 'Host hotfix lineage changed during verification'
        except (ValueError, RuntimeError, OSError) as error:
            failure = f'Final host hotfix lineage failed: {error}'
    proof['complete'] = (not failure and set(args.checks) == set(ALL) and
                         proof['pairedAudioEquivalence'].get('passed') is True)
    proof['finishedUTC'] = datetime.now(timezone.utc).isoformat()
    if failure: proof['failure'] = failure
    destination = build / f'non-daw-v{VERSION}.json'
    destination.write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (attempt / destination.name).write_bytes(destination.read_bytes())
    if failure: raise RuntimeError(failure)
    print('Complete non-DAW evidence' if proof['complete'] else 'Development subset only; release gate remains open', flush=True)

if __name__ == '__main__': main()
