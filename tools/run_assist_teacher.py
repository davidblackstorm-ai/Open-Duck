"""Run the real native teacher with bounded parallelism; outputs stay in the specified work folder."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import argparse, hashlib, json, subprocess, time
from datetime import datetime, timezone


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--teacher', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--scenes', type=int, default=144)
    parser.add_argument('--candidates', type=int, default=8)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--seed', type=int, default=42030)
    parser.add_argument('--train-only', type=int, choices=(0, 1), default=0,
                        help='Calibration pilot uses only source families0..7; never a release train/val/test corpus')
    args = parser.parse_args()
    if not args.teacher.is_file() or not 1 <= args.jobs <= 3:
        raise ValueError('A real teacher binary and 1..3 jobs are required')
    args.output.mkdir(parents=True, exist_ok=True)
    if (args.output/'manifest.json').exists() or any(args.output.glob('mode-*.csv')):
        raise FileExistsError('Use a new experiment folder; frozen corpus evidence is never overwritten')
    native_files = ['Source/PluginProcessor.cpp', 'Source/PluginProcessor.h', 'Source/Engines.h',
                    'Source/BehaviorLibrary.h', 'Source/LocalAssist.cpp', 'Source/LocalAssist.h',
                    'Source/LocalAssistRender.cpp', 'Source/LocalAssistRender.h', 'Source/PluginAssist.cpp',
                    'Source/AssistTrainingData.h', 'tools/AssistTeacher.cpp']
    manifest = {'started_utc': datetime.now(timezone.utc).isoformat(), 'teacher': str(args.teacher),
                'teacher_sha256': sha(args.teacher), 'scenes_per_mode': args.scenes,
                'candidates_per_scene': args.candidates, 'parallel_processes': args.jobs, 'seed': args.seed,
                'sample_rate': 48000, 'pipeline_revision': 'quality-r2',
                'model_contract': 'OpenDuck-native-teacher-0.3.0-r2', 'guard_contract': 'hard-preservation-v1',
                'teacher_train_only': bool(args.train_only),
                'purpose': 'training-family-only calibration' if args.train_only else 'training-validation-test corpus',
                'source_sha256': {f: sha(args.source_root/f) for f in native_files},
                'shape_bank': '(trial*5+mode*3+8*((sceneId/12)%2))%16', 'runs': []}
    manifest_path = args.output/'manifest.json'
    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    save()
    def run(mode):
        csv_path = args.output/f'mode-{mode}.csv'
        log_path = args.output/f'mode-{mode}.log'
        command = [str(args.teacher), '--mode', str(mode), '--scenes', str(args.scenes),
                   '--candidates', str(args.candidates), '--seed', str(args.seed),
                   '--train-only', str(args.train_only), '--output', str(csv_path)]
        start = time.perf_counter()
        with log_path.open('w', encoding='utf-8') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        rows = sum(1 for _ in csv_path.open(encoding='utf-8'))-1 if csv_path.exists() else 0
        contract_seen='CONTRACT OpenDuck-native-teacher-0.3.0-r2 hard-preservation-v1' in log_path.read_text(encoding='utf-8')
        return {'mode': mode, 'exit_code': result.returncode, 'rows': rows,
                'elapsed_seconds': time.perf_counter()-start,
                'csv_sha256': sha(csv_path) if csv_path.exists() else None,
                'log': str(log_path), 'matching_contract': contract_seen,
                'passed': result.returncode == 0 and rows == args.scenes and contract_seen}
    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(run, mode) for mode in range(6)]
        for future in as_completed(futures):
            row = future.result(); manifest['runs'].append(row); save(); print(json.dumps(row), flush=True)
    manifest['elapsed_seconds'] = time.perf_counter()-start
    manifest['finished_utc'] = datetime.now(timezone.utc).isoformat()
    # A changed executable/source invalidates the frozen experiment even if all child processes succeeded.
    manifest['teacher_unchanged'] = sha(args.teacher) == manifest['teacher_sha256']
    manifest['sources_unchanged'] = all(sha(args.source_root/f) == h for f,h in manifest['source_sha256'].items())
    manifest['passed'] = (len(manifest['runs']) == 6 and all(row['passed'] for row in manifest['runs'])
                          and manifest['teacher_unchanged'] and manifest['sources_unchanged'])
    save()
    print('TEACHER CORPUS', 'PASS' if manifest['passed'] else 'FAIL', flush=True)
    return 0 if manifest['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
