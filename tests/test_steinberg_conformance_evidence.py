"""File-only negative fixtures; no validator or plug-in is launched here."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import steinberg_conformance_evidence as gate


class ConformanceEvidenceTests(unittest.TestCase):
    def setUp(self):
        parent = Path(__file__).resolve().parents[1] / 'work/conformance-fixtures'
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.root = Path(self.temporary.name)
        self.validator = self.root / 'build/validator/bin/Release/validator.exe'
        self.validator.parent.mkdir(parents=True)
        self.validator.write_bytes(b'NONEXECUTABLE fixture, not a Steinberg result')
        bundle = self.root / 'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3'
        self.rows = {}
        for label, flags in (('extensive-global', ['-e']), ('extensive-local', ['-e', '-l'])):
            name = f'build/steinberg-{label}-v0.3.0.log'
            path = self.root / name
            path.write_text('Fixture only\nResult: 537 tests passed, 0 tests failed\n')
            self.rows[label] = {'passed': True, 'count': 537, 'exitCode': 0,
                'command': [str(self.validator), *flags, str(bundle)], 'log': name,
                'logSHA256': self.sha(path), 'executableSHA256': self.sha(self.validator)}
        self.proof = {'checks': {'steinberg': {'variants': self.rows}}}

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_both_complete_variants_are_accepted(self):
        self.assertEqual(gate.validate(self.root, self.proof), self.rows)

    def test_missing_lifetime_is_rejected(self):
        del self.rows['extensive-local']
        with self.assertRaises(RuntimeError): gate.validate(self.root, self.proof)

    def test_basic_suite_cannot_replace_extended_suite(self):
        self.rows['extensive-global']['count'] = 47
        with self.assertRaises(RuntimeError): gate.validate(self.root, self.proof)

    def test_failed_process_or_summary_is_rejected(self):
        for field, value in (('exitCode', 1), ('passed', False)):
            proof = copy.deepcopy(self.proof)
            proof['checks']['steinberg']['variants']['extensive-global'][field] = value
            with self.assertRaises(RuntimeError): gate.validate(self.root, proof)
        path = self.root / self.rows['extensive-global']['log']
        path.write_text('Result: 536 tests passed, 1 tests failed')
        self.rows['extensive-global']['logSHA256'] = self.sha(path)
        with self.assertRaises(RuntimeError): gate.validate(self.root, self.proof)

    def test_stale_log_or_validator_is_rejected(self):
        self.validator.write_bytes(b'changed fixture')
        with self.assertRaises(RuntimeError): gate.validate(self.root, self.proof)

    def test_local_flag_is_mandatory(self):
        self.rows['extensive-local']['command'].remove('-l')
        with self.assertRaises(RuntimeError): gate.validate(self.root, self.proof)


if __name__ == '__main__': unittest.main(verbosity=2)
