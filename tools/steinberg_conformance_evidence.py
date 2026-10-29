"""Check actual SDK3.8.1 extended/global and extended/local result provenance."""
import hashlib
from pathlib import Path
import re


def validate(root, proof):
    root = Path(root).resolve()
    validator = root / 'build/validator/bin/Release/validator.exe'
    bundle = root / 'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3'
    def sha(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    def require(ok, message):
        if not ok:
            raise RuntimeError(message)
    variants = proof.get('checks', {}).get('steinberg', {}).get('variants', {})
    require(set(variants) == {'extensive-global', 'extensive-local'}, 'Both extended Steinberg instance lifetimes are required')
    for label, flags in (('extensive-global', ['-e']), ('extensive-local', ['-e', '-l'])):
        row = variants[label]
        relative = f'build/steinberg-{label}-v0.3.0.log'
        path = root / relative
        require(row.get('passed') is True and row.get('exitCode') == 0 and row.get('count') == 537,
                f'Unresolved extended Steinberg suite: {label}')
        require(row.get('log') == relative and row.get('logSHA256') == sha(path) and
                row.get('executableSHA256') == sha(validator) and
                row.get('command') == [str(validator), *flags, str(bundle)],
                f'Extended Steinberg inputs/log changed: {label}')
        raw = path.read_bytes()
        text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig', errors='replace')
        require(bool(re.search(r'Result:\s*537\s+tests passed,\s*0\s+tests failed', text)),
                f'Extended Steinberg summary missing: {label}')
    return variants
