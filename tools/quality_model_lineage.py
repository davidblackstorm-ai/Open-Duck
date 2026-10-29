"""Verify the R2 teacher, guard and runtime share the exact audio implementation.

R1's post-training stereo-extension audit remains historical. R2 freezes stereo
and preservation guards before generating fresh labels: no equivalence to the
old, faulty gain law is asserted or required.
"""
from __future__ import annotations
import csv
import hashlib
import json
from pathlib import Path
import re

REQUIRED = {
    'Source/PluginProcessor.cpp', 'Source/PluginProcessor.h', 'Source/Engines.h',
    'Source/BehaviorLibrary.h', 'Source/AssistTrainingData.h', 'Source/PluginAssist.cpp',
    'Source/LocalAssist.cpp', 'Source/LocalAssist.h', 'Source/LocalAssistRender.cpp',
    'Source/LocalAssistRender.h', 'tools/AssistTeacher.cpp',
}

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def validate(root):
    root = Path(root).resolve()
    relative = 'docs/research-v0.3.0/teacher-final/manifest.json'
    corpus_dir = root / Path(relative).parent
    corpus = json.loads((root / relative).read_text(encoding='utf-8-sig'))
    training = json.loads((root / 'docs/research-v0.3.0/training.json').read_text(encoding='utf-8-sig'))
    require(corpus.get('pipeline_revision') == 'quality-r2' and corpus.get('seed') == 42030 and
            corpus.get('teacher_train_only') is False,
            'A fresh quality-r2 native corpus with seed42030 is required')
    require(corpus.get('scenes_per_mode') == 144 and corpus.get('candidates_per_scene') == 8 and
            all(corpus.get(k) is True for k in ('passed', 'teacher_unchanged', 'sources_unchanged')),
            'The intact 864-scene, eight-candidate R2 teacher run is required')
    sources = corpus.get('source_sha256', {})
    require(REQUIRED <= sources.keys(), 'Frozen teacher lacks DSP, stereo, guard or renderer source fingerprints')
    for name, expected in sources.items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts and
                isinstance(expected, str) and re.fullmatch('[0-9a-f]{64}', expected), 'Invalid source fingerprint')
        require(sha(corpus_dir / 'frozen-source' / path) == expected == sha(root / path),
                f'R2 runtime/teacher/frozen implementation differs: {name}')
    require(training.get('version') == '0.3.0-r2', 'Models were not retrained for R2')
    require(training.get('training_script_sha256') == sha(root / 'tools/train_assist.py') and
            training.get('corpus_manifest_sha256') == sha(root / relative), 'Training script or corpus provenance differs')
    require(sha(root / 'Source/NeuralModels.h') == training.get('header_sha256'), 'R2 weight export changed')
    header = (root / 'Source/NeuralModels.h').read_text(encoding='utf-8-sig')
    require('trained=true' in header and 'OpenDuck-native-teacher-0.3.0-r2' in header,
            'R1 weights or untrained placeholders cannot satisfy the quality gate')
    runs = {r['mode']: r for r in corpus.get('runs', [])}
    data = {r['mode']: r for r in training.get('sources', [])}
    models = {r['mode']: r for r in training.get('models', [])}
    require(set(runs) == set(data) == set(models) == set(range(6)), 'Six independent sector lineages required')
    require(len({r.get('model_sha256_float32') for r in models.values()}) == 6, 'Six distinct trained weight sets required')
    modes = []
    for mode in range(6):
        run, model = runs[mode], models[mode]
        csv_path = corpus_dir / f'mode-{mode}.csv'
        require(run.get('passed') is True and run.get('exit_code') == 0 and run.get('rows') == 144 and
                sha(csv_path) == run.get('csv_sha256') == data[mode].get('sha256'), f'R2 data mismatch for sector {mode}')
        require((model.get('train_scenes'), model.get('validation_scenes'), model.get('test_scenes')) == (96, 24, 24) and
                model.get('seed') == 43030 + mode * 1009, 'R2 grouped splits or fresh training seed changed')
        with csv_path.open(newline='', encoding='utf-8-sig') as stream:
            rows = list(csv.DictReader(stream))
        require(len(rows) == 144 and all(int(r['mode']) == mode for r in rows), 'Invalid R2 teacher records')
        require({split: sum(int(r['split']) == split for r in rows) for split in (0, 1, 2)} == {0:96, 1:24, 2:24},
                'Grouped R2 source split is incomplete')
        modes.append({'mode': mode, 'rows': 144, 'csvSHA256': sha(csv_path),
                      'modelSHA256': model['model_sha256_float32']})
    repro_path = root / 'build/training-reproducibility-v0.3.0-r2.json'
    repro = json.loads(repro_path.read_text(encoding='utf-8-sig'))
    require(repro.get('version') == '0.3.0-r2' and repro.get('pipeline_revision') == 'quality-r2' and
            repro.get('seed') == 43030 and repro.get('steps') == 1800 and
            all(repro.get(k) is True for k in ('passed', 'allSixModelStatisticsExactlyEqual', 'originalInputsUnchanged')) and
            repro.get('corpusManifestSHA256') == sha(root / relative) and
            repro.get('trainingScriptSHA256') == sha(root / 'tools/train_assist.py') and
            repro.get('originalTrainingReportSHA256') == sha(root / 'docs/research-v0.3.0/training.json'),
            'R2 deterministic repeated training has not passed for the current inputs')
    for label, original in (('NeuralModels.h', root / 'Source/NeuralModels.h'),
                            ('golden.csv', root / 'docs/research-v0.3.0/golden.csv')):
        row = repro.get('checks', {}).get(label, {})
        repeated = Path(row.get('repeatedPath', ''))
        require(repeated.is_absolute() and repeated.resolve().is_relative_to(root) and
                repeated.resolve() != original.resolve() and not repeated.samefile(original) and
                row.get('identical') is True and row.get('originalSHA256') == row.get('repeatedSHA256') == sha(original) and
                sha(repeated) == sha(original), f'Repeated R2 export does not match: {label}')
    return {'passed': True, 'method': 'quality-r2-exact-frozen-runtime-source',
            'corpusManifestSHA256': sha(root / relative), 'sourceSHA256': sources,
            'modelHeaderSHA256': training['header_sha256'], 'modes': modes,
            'trainingReproducibilitySHA256': sha(repro_path),
            'scope': 'Exact shared DSP/renderer/guard provenance; measured audio and stereo tests are separate gates. No R1 equivalence or perceptual certification is inferred.'}
