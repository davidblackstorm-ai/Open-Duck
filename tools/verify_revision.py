"""Run 0.3.0 non-DAW checks against one fingerprinted Release build.

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
from verify_quality_audio import validate as validate_quality_audio
from quality_model_lineage import validate as validate_model_lineage

VERSION = '0.3.0'
NATIVE = {'legacy': 'OpenDuckTests', 'dsp': 'Revision030DspTests',
          'quality': 'Revision030QualityTests', 'audio': 'Revision030QualityAudit',
          'ui': 'Revision030UiTests', 'ai': 'Revision030AiTests',
          'bridge': 'Revision030AssistBridgeTests', 'parity': 'AssistModelParity',
          'host': 'OpenDuckHostContract'}
ALL = tuple(NATIVE) + ('steinberg', 'pluginval')

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

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
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    attempt = root / 'work/v0.3.0/verification-attempts' / stamp
    attempt.mkdir(parents=True, exist_ok=False)
    preview = build / f'visual-qa-v{VERSION}'; preview.mkdir(exist_ok=True)
    proof = {'version': VERSION, 'startedUTC': stamp, 'releaseBinarySHA256': sha(binary),
             'complete': False, 'abletonRun': False, 'productionInstallPerformed': False,
             'checks': {}, 'sourceSHA256': {p.relative_to(root).as_posix(): sha(p)
                 for directory in ('Source', 'tests')
                 for p in sorted((root / directory).rglob('*')) if p.is_file()}}
    proof['inputEvidenceSHA256'] = {name: sha(root / name) for name in (
        'docs/research-v0.3.0/golden.csv', 'docs/research-v0.3.0/training.json',
        'docs/research-v0.3.0/teacher-final/manifest.json') if (root / name).is_file()}
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
        destination = build / names.get(name, f'{name}-v{VERSION}.log')
        if destination.is_file(): (attempt / destination.name).write_bytes(destination.read_bytes())
        started = time.monotonic()
        print(f'RUN {name}', flush=True)
        with destination.open('wb') as output:
            try:
                job = subprocess.run(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                     timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW)
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
        ok = bool(ok) and not re.search(r'^FAIL\s', text, re.MULTILINE)
        proof['checks'][name] = {'passed': ok, 'count': passed, 'exitCode': code,
            'seconds': round(time.monotonic()-started, 3), 'command': command,
            'log': destination.relative_to(root).as_posix(), 'logSHA256': sha(destination),
            'executableSHA256': sha(command[0])}
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
        if name == 'audio': generated.append(build / f'quality-audio-v{VERSION}.json')
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
    proof['complete'] = not failure and set(args.checks) == set(ALL)
    proof['finishedUTC'] = datetime.now(timezone.utc).isoformat()
    if failure: proof['failure'] = failure
    destination = build / f'non-daw-v{VERSION}.json'
    destination.write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (attempt / destination.name).write_bytes(destination.read_bytes())
    if failure: raise RuntimeError(failure)
    print('Complete non-DAW evidence' if proof['complete'] else 'Development subset only; release gate remains open', flush=True)

if __name__ == '__main__': main()
