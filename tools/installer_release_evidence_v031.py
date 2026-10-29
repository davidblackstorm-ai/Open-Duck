"""Bind isolated installer QA and compile-only production Setup to current inputs."""
import hashlib
import json
from pathlib import Path
import re

VERSION = '0.3.1'
BUNDLE = 'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents'
INPUTS = ('installer/OpenDuck.iss', 'installer/LICENSE.txt', 'installer/RELEASE-NOTES.txt',
          'installer/WizardPanel.bmp', 'installer/WizardSmall.bmp', 'installer/OriginalTechno.wav')
QA_FLAGS = ('installPrevious', 'upgradeFrom030', 'reinstall', 'uninstall', 'legacyPresetPreserved',
            'modernPresetPreserved', 'binaryMatches', 'allPayloadFilesMatch', 'reinstallPayloadMatches',
            'managedDocumentsMatch', 'allManagedPayloadRemoved', 'productionUntouched')


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def canonical_sha(value):
    # PowerShell's ordered/compact JSON and this serialization are cross-checked
    # by the fixture tests, including registry text and byte encodings.
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf-8')).hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def validate(root):
    root = Path(root).resolve()
    def path(relative):
        name = Path(relative)
        require(not name.is_absolute() and '..' not in name.parts and ':' not in relative,
                'Invalid installer evidence relative path')
        target = root / name
        require(target.resolve().is_relative_to(root), 'Installer evidence escapes the project')
        for item in (target, *target.parents):
            if item.exists():
                require(not (getattr(item.stat(follow_symlinks=False), 'st_file_attributes', 0) & 0x400),
                        f'Installer evidence contains a reparse point: {item}')
            if item == root:
                break
        return target
    qa_path = path('build/installer-qa-results-v0.3.1.json')
    production_path = path('build/installer-production-v0.3.1.json')
    qa = json.loads(qa_path.read_text(encoding='utf-8-sig'))
    production = json.loads(production_path.read_text(encoding='utf-8-sig'))
    require(qa.get('evidenceSchema') == 'openduck-installer-qa-v2' and qa.get('version') == VERSION and
            all(qa.get(flag) is True for flag in QA_FLAGS), 'Complete current isolated installer QA required')
    require(production.get('schema') == 'openduck-installer-production-v1' and production.get('version') == VERSION and
            production.get('complete') is True and production.get('compileOnly') is True and
            production.get('productionInstallPerformed') is False, 'Compile-only production installer manifest required')
    manifest = qa.get('inputManifest', {})
    require(manifest == production.get('inputManifest') and manifest.get('schema') == 'openduck-installer-inputs-v1' and
            manifest.get('version') == VERSION, 'QA and production Setup did not use identical inputs')
    contents = path(BUNDLE)
    payload_files, directories = [], []
    for file in sorted(contents.rglob('*'), key=lambda p: p.relative_to(contents).as_posix().lower()):
        file = path(file.relative_to(root).as_posix())
        relative = file.relative_to(contents).as_posix()
        if file.is_dir():
            directories.append(relative)
        else:
            payload_files.append({'relative': relative, 'length': file.stat().st_size, 'sha256': sha(file)})
    require(payload_files, 'Current managed VST3 payload is empty')
    require(not any(Path(row['relative']).suffix.lower() in {'.pdb','.ilk','.ipdb','.iobj'} for row in payload_files), 'Private debug files are forbidden in the distributable payload')
    files = []
    for relative in INPUTS:
        file = path(relative)
        files.append({'relative': relative, 'length': file.stat().st_size, 'sha256': sha(file)})
    files.extend({'relative': BUNDLE + '/' + row['relative'], 'length': row['length'], 'sha256': row['sha256']}
                 for row in payload_files)
    files.sort(key=lambda row: row['relative'].lower())
    inventory = {'files': files, 'payloadDirectories': directories}
    expected_input_sha = canonical_sha(inventory)
    require(manifest.get('files') == files and manifest.get('payloadDirectories') == directories and
            manifest.get('inputSHA256') == qa.get('inputSHA256') == production.get('inputSHA256') == expected_input_sha,
            'Installer source, resources, documents or payload changed after QA/compile')
    payload = {'directories': directories, 'files': payload_files}
    require(qa.get('payloadManifest') == payload and qa.get('payloadFileCount') == len(payload_files) and
            qa.get('upgradePayloadSHA256') == qa.get('reinstallPayloadSHA256') == canonical_sha(payload),
            'Upgrade/reinstall did not verify the complete current payload')
    binary = path(BUNDLE + '/x86_64-win/OpenDuck Pro X.vst3')
    require(qa.get('builtProductVersion') == production.get('builtProductVersion') == VERSION, 'Wrong final VST3 product version')
    require(qa.get('previousVersion') == '0.3.0' and str(qa.get('previousInstalledSHA256', '')).lower() == 'a22be99e689d64304fcc525c9c4eeae9b793022836bcaca96cf125ccc636f271', 'Authentic 0.3.0 previous upgrade source required')
    require(str(qa.get('builtSHA256', '')).lower() == sha(binary), 'QA tested another VST3 binary')
    require(qa.get('productionSnapshotSchema') == 1 and isinstance(qa.get('productionBefore'), dict) and
            set(qa['productionBefore']) == {'bundle', 'auxiliary', 'uninstallRegistry'} and
            qa['productionBefore'] == qa.get('productionAfter') and
            qa.get('productionBeforeSHA256') == qa.get('productionAfterSHA256') and
            qa.get('productionBeforeSHA256') == canonical_sha(qa['productionBefore']),
            'Production bundle/resources/documents/uninstall metadata were not preserved')
    expected_target = path('work/installer-qa-v0.2.0/OpenDuck Pro X.vst3')
    require(Path(qa.get('target', '')).resolve() == expected_target.resolve() and
            not any((expected_target / 'Contents' / row['relative']).exists() for row in payload_files) and
            not any((expected_target / 'Documentation' / name).exists() for name in ('LICENSE.txt', 'RELEASE-NOTES.txt')),
            'Isolated QA target still contains managed payload/documents')
    require(str(qa.get('legacyInstalledSHA256', '')).lower() ==
            'a22be99e689d64304fcc525c9c4eeae9b793022836bcaca96cf125ccc636f271', 'Authentic previous-version installed binary required')
    require(qa.get('qaSetupProductVersion') == VERSION, 'Wrong QA Setup product version')
    qa_setup = 'work/installer-qa-build-v0.3.1/OpenDuck-Installer-QA.exe'
    require(qa.get('qaSetupRelative') == qa_setup and qa.get('setupSHA256') == sha(path(qa_setup)), 'QA Setup changed')
    previous = path('work/installer-qa-build-v0.3.0/OpenDuck-Installer-QA.exe')
    require(str(qa.get('previousSetupSHA256', '')).lower() == sha(previous) ==
            '1660adc9e23fea2ff044a2695b6de8bc5093dcdcf20f4cedb02d8930773aeca5', 'Authentic previous-version QA setup required')
    setup_relative = 'releases/OpenDuck-Pro-X-0.3.1-Windows-x64-Setup.exe'
    setup = path(setup_relative)
    require(production.get('setup') == {'relative': setup_relative, 'sha256': sha(setup),
            'length': setup.stat().st_size, 'productVersion': VERSION}, 'Production Setup differs from its compile manifest')
    for report, field, names in ((qa, 'verificationToolsSHA256', ('verify_installer_v031.ps1', 'installer_evidence_v031.ps1')),
                                (production, 'compilationToolsSHA256', ('compile_installer_v031.ps1', 'installer_evidence_v031.ps1'))):
        require(report.get(field) == {'tools/' + name: sha(path('tools/' + name)) for name in names},
                'Installer verification/compile tools changed since this attempt')
        compiler = report.get('compiler', {})
        compiler_path = Path(compiler.get('path', ''))
        require(compiler_path.is_absolute() and compiler.get('sha256') == sha(compiler_path), 'Installer compiler changed')
    require(qa['compiler']['path'] == production['compiler']['path'] and
            qa['compiler']['sha256'] == production['compiler']['sha256'], 'QA and production compiler differ')
    require(qa['compiler'].get('arguments') == ['/Qp', '/DProductVersion=0.3.1', '/DQA', '/DQAUpgradeFrom030', '/FOpenDuck-Installer-QA',
            '/O' + str(root / 'work/installer-qa-build-v0.3.1'), str(root / 'installer/OpenDuck.iss')], 'QA compiler arguments changed')
    require(production['compiler'].get('arguments') == ['/Qp', '/DProductVersion=0.3.1', '/O' + str(root / 'releases'),
            '/FOpenDuck-Pro-X-0.3.1-Windows-x64-Setup', str(root / 'installer/OpenDuck.iss')], 'Production compiler arguments changed')
    return {'passed': True, 'inputSHA256': expected_input_sha, 'payloadFileCount': len(payload_files),
            'qaReportSHA256': sha(qa_path), 'productionManifestSHA256': sha(production_path),
            'productionSetupSHA256': sha(setup), 'productionInstallPerformed': False}
