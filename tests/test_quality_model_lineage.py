"""Negative tests of the release provenance gate, without invoking DSP or a DAW."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

GATE = Path(__file__).resolve().parents[2] / 'v030-delivery/tools/quality_model_lineage.py'
if not GATE.exists():
    GATE = Path(__file__).resolve().parents[1] / 'tools/quality_model_lineage.py'
spec = importlib.util.spec_from_file_location('quality_model_lineage', GATE)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class LineageTests(unittest.TestCase):
    def setUp(self):
        self.fixture_parent = (Path(__file__).resolve().parents[1] / 'work/qa-gate-fixtures').resolve()
        self.fixture_parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='openduck-lineage-', dir=self.fixture_parent)
        self.root = Path(self.temporary.name)
        self.corpus = self.root / 'docs/research-v0.3.0/teacher-final'
        self.corpus.mkdir(parents=True)
        hashes = {}
        for name in gate.REQUIRED:
            for path in (self.root / name, self.corpus / 'frozen-source' / name):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test fixture: ' + name, encoding='utf-8')
            hashes[name] = gate.sha(self.root / name)
        self.manifest = dict(pipeline_revision='quality-r2', seed=42030, teacher_train_only=False,
                             scenes_per_mode=144, candidates_per_scene=8, passed=True,
                             teacher_unchanged=True, sources_unchanged=True, source_sha256=hashes, runs=[])
        header = self.root / 'Source/NeuralModels.h'
        header.write_text('trained=true; OpenDuck-native-teacher-0.3.0-r2', encoding='utf-8')
        (self.root / 'tools/train_assist.py').write_text('test fixture trainer', encoding='utf-8')
        (self.corpus.parent / 'golden.csv').write_text('test fixture golden', encoding='utf-8')
        self.training = dict(version='0.3.0-r2', header_sha256=gate.sha(header), sources=[], models=[])
        for mode in range(6):
            file = self.corpus / f'mode-{mode}.csv'
            with file.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.writer(stream); writer.writerow(['mode', 'split'])
                writer.writerows((mode, 0 if i < 96 else 1 if i < 120 else 2) for i in range(144))
            self.manifest['runs'].append(dict(mode=mode, passed=True, exit_code=0, rows=144, csv_sha256=gate.sha(file)))
            self.training['sources'].append(dict(mode=mode, sha256=gate.sha(file)))
            self.training['models'].append(dict(mode=mode, train_scenes=96, validation_scenes=24, test_scenes=24,
                    seed=43030+mode*1009, model_sha256_float32=hashlib.sha256(str(mode).encode()).hexdigest()))
        self.save()

    def save(self):
        (self.corpus / 'manifest.json').write_text(json.dumps(self.manifest), encoding='utf-8')
        self.training['training_script_sha256'] = gate.sha(self.root / 'tools/train_assist.py')
        self.training['corpus_manifest_sha256'] = gate.sha(self.corpus / 'manifest.json')
        (self.corpus.parent / 'training.json').write_text(json.dumps(self.training), encoding='utf-8')
        repro = dict(passed=True, version='0.3.0-r2', pipeline_revision='quality-r2', seed=43030, steps=1800,
                allSixModelStatisticsExactlyEqual=True, originalInputsUnchanged=True, checks={},
                corpusManifestSHA256=gate.sha(self.corpus / 'manifest.json'),
                trainingScriptSHA256=gate.sha(self.root / 'tools/train_assist.py'),
                originalTrainingReportSHA256=gate.sha(self.corpus.parent / 'training.json'))
        for label, path in (('NeuralModels.h', self.root / 'Source/NeuralModels.h'), ('golden.csv', self.corpus.parent / 'golden.csv')):
            repeated = self.root / 'work/repro' / label
            repeated.parent.mkdir(parents=True, exist_ok=True); repeated.write_bytes(path.read_bytes())
            repro['checks'][label] = dict(identical=True, originalSHA256=gate.sha(path), repeatedSHA256=gate.sha(repeated), repeatedPath=str(repeated))
        (self.root / 'build').mkdir(exist_ok=True)
        (self.root / 'build/training-reproducibility-v0.3.0-r2.json').write_text(json.dumps(repro), encoding='utf-8')

    def tearDown(self):
        self.assertTrue(self.root.resolve().is_relative_to(self.fixture_parent))
        self.temporary.cleanup()
    def test_valid_fixture(self): self.assertTrue(gate.validate(self.root)['passed'])
    def test_old_corpus_rejected(self):
        self.manifest['pipeline_revision'] = 'r1'; self.save()
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_pilot_rejected(self):
        self.manifest['teacher_train_only'] = True; self.save()
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_runtime_guard_changed(self):
        (self.root / 'Source/LocalAssist.cpp').write_text('changed', encoding='utf-8')
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_frozen_renderer_changed(self):
        (self.corpus / 'frozen-source/Source/LocalAssistRender.h').write_text('changed', encoding='utf-8')
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_old_weights_rejected(self):
        self.training['version'] = '0.3.0'; self.save()
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_wrong_seed_rejected(self):
        self.training['models'][2]['seed'] = 33030; self.save()
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_mismatched_data_rejected(self):
        (self.corpus / 'mode-3.csv').write_text('changed', encoding='utf-8')
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_old_repro_rejected(self):
        file = self.root / 'build/training-reproducibility-v0.3.0-r2.json'
        record = json.loads(file.read_text(encoding='utf-8')); record['version'] = '0.3.0'
        file.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaises(RuntimeError): gate.validate(self.root)
    def test_self_comparison_rejected(self):
        file = self.root / 'build/training-reproducibility-v0.3.0-r2.json'
        record = json.loads(file.read_text(encoding='utf-8'))
        record['checks']['NeuralModels.h']['repeatedPath'] = str(self.root / 'Source/NeuralModels.h')
        file.write_text(json.dumps(record), encoding='utf-8')
        with self.assertRaises(RuntimeError): gate.validate(self.root)

if __name__ == '__main__': unittest.main()
