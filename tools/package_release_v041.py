"""Package 0.4.1 only from matching, current verification evidence.

No builds, installation, process control, deletions or external publishing.
Run after native QA and the isolated installer tests. Prior 0.4.0 evidence is
retained as history and never counted as a new result.
"""
from pathlib import Path
from datetime import datetime
import argparse
import hashlib
import json
import os
import re
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
version = '0.4.1'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--outputs', type=Path, default=Path(r'C:\Users\PC\Documents\Codex\2026-10-29\recupera-quello-che-stavamo-facendo-per\outputs'))
args = parser.parse_args()
args.outputs.mkdir(parents=True, exist_ok=True)
work = root / 'work/revision041'
verification = root / f'build/verification-v{version}'
release = root / f'releases/{version}'
setup = root / f'releases/OpenDuck-Pro-X-{version}-Windows-x64-Setup.exe'
bundle = root / 'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3'
binary = bundle / 'Contents/x86_64-win/OpenDuck Pro X.vst3'

def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()

def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def require(value, message):
    if not value:
        raise RuntimeError(message)

evidence = read_json(verification / 'results.json')
built_hash = sha(binary)
require(evidence['version'] == version and evidence.get('complete') is True and evidence['builtSHA256'].lower() == built_hash,
        'Current verification does not match the 0.4.1 Release binary')
require(bool(evidence.get('sourceSHA256')), 'Build evidence has no source hashes; run build_v041.ps1')
for relative, expected in evidence['sourceSHA256'].items():
    require(sha(root / relative) == expected.lower(), f'Source changed after verification: {relative}')
checks = {}
required_checks = {'core', 'ui', 'ai5', 'ai10', 'stress-4x120s',
                   'actual-vst3-lifecycle-and-automation', 'actual-vst3-host', 'real-waveform-capture',
                   'curve-tools'}
for check in evidence['checks']:
    log = Path(check['log'])
    require(sha(log) == check['sha256'].lower(), f'Test log changed: {log}')
    body = log.read_text(encoding='utf-8-sig')
    match = re.search(r'(?:RESULT|Result:)[^\r\n]*?(\d+) passed, 0 failed', body)
    require(match and int(match[1]) > 0 and not re.search(r'^FAIL ', body, re.M),
            f'No successful positive test record: {log}')
    require(check.get('exitCode') == 0 and check.get('failed') == 0 and
            check.get('passed') == int(match[1]), f'Inconsistent test count: {log}')
    checks[check['name']] = check
require(required_checks.issubset(checks), f'Missing current checks: {required_checks - checks.keys()}')

compiler = read_json(root / f'build/installer-production-v{version}.json')
require(compiler['version'] == version and compiler['complete'] and compiler['compileOnly'] and
        compiler['setup']['sha256'].lower() == sha(setup), 'Production installer evidence does not match setup')
qa = read_json(root / f'build/installer-qa-results-v{version}.json')
require(qa['version'] == version and qa['builtSHA256'].lower() == built_hash and
        all(qa.get(key) is True for key in ['upgradeFrom040', 'reinstall', 'uninstall',
            'legacyPresetPreserved', 'modernPresetPreserved', 'productionUntouched', 'allPayloadFilesMatch']),
        'Current isolated 0.4.0 -> 0.4.1 installer QA is incomplete')
require(compiler['inputSHA256'] == qa['inputSHA256'], 'Installer QA and production compilation used different inputs')
for entry in compiler['inputManifest']['files']:
    require(sha(root / entry['relative']) == entry['sha256'].lower(),
            f"Installer input changed after QA: {entry['relative']}")

native = read_json(work / 'ableton-verification.json')
require(native.get('version') == version and native.get('complete') is True and
        native.get('builtSHA256', '').lower() == built_hash and
        isinstance(native.get('observations'), list) and native['observations'],
        'Native Ableton verification is missing or does not match this binary')
validators = read_json(work / 'validator-verification.json')
require(validators.get('version') == version and validators.get('complete') is True and
        validators.get('builtSHA256', '').lower() == built_hash,
        'Validator evidence does not match the final VST3')
require({row['name'] for row in validators['checks']} == {'steinberg', 'pluginval'},
        'Both final validators must have current evidence')
for row in validators['checks']:
    require(row['exitCode'] == 0 and sha(row['log']) == row['logSHA256'].lower() and
            sha(row['stderr']) == row['stderrSHA256'].lower() and
            sha(row['tool']) == row['toolSHA256'].lower(),
            f"Validator output/tool changed: {row['name']}")
pluginval = work / 'release-pluginval.stdout.log'
pluginval_body = pluginval.read_text(encoding='utf-8-sig')
require('Strictness level: 10' in pluginval_body and pluginval_body.rstrip().endswith('SUCCESS'),
        'Current pluginval strictness10 validation is incomplete')
steinberg = work / 'release-steinberg.log'
steinberg_body = steinberg.read_text(encoding='utf-8-sig')
require(re.search(r'0 tests failed', steinberg_body) is not None, 'Current Steinberg validation is incomplete')
steinberg_match = re.search(r'(\d+) tests passed', steinberg_body)
require(steinberg_match and int(steinberg_match[1]) > 0, 'Steinberg log has no positive result count')

release.mkdir(parents=True, exist_ok=True)
args.outputs.mkdir(parents=True, exist_ok=True)
for category in ['Preview', 'Evidence']:
    (release / category).mkdir(exist_ok=True)
for item in checks.values():
    shutil.copy2(item['log'], release / 'Evidence' / Path(item['log']).name)
for file in [verification / 'results.json', root / f'build/installer-production-v{version}.json',
             root / f'build/installer-qa-results-v{version}.json', work / 'ableton-verification.json',
             work / 'validator-verification.json', pluginval, steinberg]:
    shutil.copy2(file, release / 'Evidence' / file.name)
for name in ['concept-MIX65-single-1728.png', 'concept-MIX65-loop-1728.png',
             'concept-MIX65-single-hover.png', 'concept-MIX65-loop-hover.png',
             'concept-mini-MIDI-one-shot-1728.png']:
    file = verification / 'Preview' / name
    require(file.is_file(), f'Missing final native preview: {name}')
    shutil.copy2(file, release / 'Preview' / name)
require(isinstance(native.get('screenshots'), list) and native['screenshots'],
        'Final native Ableton screenshots are missing')
for entry in native['screenshots']:
    file = Path(entry['path']).resolve(strict=True)
    require(file.parent == work.resolve() and file.name.startswith('Ableton-')
            and file.suffix.lower() in {'.png', '.jpg', '.jpeg'}
            and sha(file) == entry['sha256'].lower(),
            f'Native Ableton screenshot evidence changed: {file}')
    shutil.copy2(file, release / 'Evidence' / file.name)
for name in ['FUNCTIONAL-COMPARISON-v0.4.1.json', 'REFERENCE-FUNCTIONAL-RESULTS-v0.4.1.txt']:
    file = root / 'docs' / name
    require(file.is_file(), f'Missing measured functional comparison: {name}')
    shutil.copy2(file, release / 'Evidence' / name)

count_lines = '\n'.join(f"{name}: {item['passed']} pass, 0 fail." for name, item in checks.items())
observations = '\n'.join('- ' + str(item) for item in native['observations'])
report = f'''BLACK CIRCUIT — OPEN DUCK {version}
Report locale di consegna — {datetime.now().astimezone().isoformat()}

Installer: {setup}
Dimensione: {setup.stat().st_size:,} byte
SHA256 installer: {sha(setup)}
SHA256 VST3 finale: {built_hash}
Progetto e sorgenti: {root}

CORREZIONE
Waveform grigia/biancastra del MAIN reale per default, anche senza sidechain.
Finestra iniziale di quattro beat; mini scheda per MAIN/SIDECHAIN/OUTPUT,
1/2/4/8/16 beat, singolo colpo e zoom di una porzione.
La cattura grafica non dipende dal trigger e funziona anche in bypass.
Nessuna waveform sintetica e nessuna acquisizione da Windows Media Player.
Nuova istanza: MIX 0,0%, POWER spento, main non attenuato. POWER abilita
il ducking; MIX regola soltanto la profondita. MOVE sposta la curva intera,
PHASE regola la durata e CURVE la curvatura. Le sessioni richiamano i
parametri salvati e POWER; gli stati precedenti mantengono il loro modello DSP.
Template cromato e 700 preset conservati. Le manopole non sono state ridisegnate.
Curva blu continua, con collegamenti ai riavvii; una sola zona SLOPE ciano
delimitata da tratteggi, visibile solo con il mouse nel grafico e durante il drag.
Drag interno: pendenza; drag esterno: curva intera. L'hover non cambia l'audio.
BLACK CIRCUIT e il marchio del produttore, retroilluminato e non interattivo.
Quattro frecce MOVE, coarse 1/16 e fine 1/256 di ciclo. Freccia nel grafico:
Undo per gesto, con cronologia distinta per istanza. Reset MOVE/SLOPE nel menu.
AUTO / 1 CURVA / LOOP: classificazione del segnale reale e scelta manuale.
In AUDIO/MIDI il blu GAIN REALE segue i coefficienti effettivamente applicati,
con la medesima compensazione del ritardo della waveform. I retrigger fuori
griglia e il mantenimento ONE SHOT non vengono sostituiti da ripetizioni.
1 CURVA mostra l'ultima cattura del trigger su asse0..durata fisica; LOOP
mostra la cattura corrente anche parziale. SYNC conserva la preview periodica.
Le miniature mostrano forme complete con i loro SHIFT/SLOPE; il grafico
applica anche MIX. BAND esteso: crossover 20..5120 Hz e cuffie per ascoltare
la banda bassa processata; cuffie disabilitate con BAND spento.
Il nuovo editor non contiene accessi all'analisi locale/AI o editing dei punti.
Backend e stati precedenti restano per compatibilita; non viene pubblicizzata
una funzione AI nella nuova interfaccia.

RIFERIMENTO FUNZIONALE
Manuale ufficiale Kickstart 2 per SHIFT/SLOPE/reset/BAND:
https://downloads.cableguys.com/Nicky-Romero-Kickstart-2-Manual.pdf?v=2.0.8
Il DSP di Open Duck e implementato indipendentemente. Le prove usano il
plugin installato come riferimento funzionale, con ingressi audio controllati
e confronto numerico; i risultati valgono per i casi documentati nelle prove.

VERIFICHE AUTOMATICHE SU QUESTA BUILD
{count_lines}
Validator Steinberg: {int(steinberg_match[1])} pass, 0 fail.
Pluginval strictness10: SUCCESS.
I conteggi sono estratti dai log correnti, confrontati per hash.
I risultati storici 0.4.0 non vengono presentati come prove della 0.4.1.
Le suite AI esercitano il backend mantenuto per compatibilita, non controlli
AI esposti dal nuovo editor. curve-tools verifica le nuove funzioni native.

COLLAUDO NATIVO ABLETON 12
{observations}
L'hash della DLL collaudata coincide con la build e il payload del setup.
Nessuna modifica diretta alle preferenze globali di Ableton.

INSTALLER
Publisher e testi Black Circuit. Upgrade autentico QA 0.4.0 -> 0.4.1,
reinstallazione e disinstallazione isolati; preset personali conservati.
Il payload installato dalla QA coincide con quello compilato.
Il setup di produzione e compilato e non e stato eseguito elevato da questa QA.
La distribuzione non e firmata Authenticode.

PACCHETTI
Archivio sorgenti con Source/tests/tools/docs/Assets/installer/cmake/third_party.
Archivio VST3 con il payload Release verificato. Hash in SHA256SUMS-v{version}.txt.
Ricompilazione: tools/build_v041.ps1 -WithStress.
Grafica setup: tools/create_installer_assets_v041.py --preview <render-nativo.png>.
Setup: tools/compile_installer_v041.ps1 -InnoCompiler <ISCC.exe>.
QA: tools/verify_installer_v041.ps1 -InnoCompiler <ISCC.exe>.

PULIZIA STORICA
La rimozione degli intermedi 0.4.0 era stata respinta dal controllo automatico
con motivo "blocked by policy". Non e stata aggirata e resta non completata.
Questi script non cancellano oggetti di compilazione o archivi di recupero.
'''
for destination in [root / f'docs/VERIFICATION-v{version}.txt', release / 'Report-verifica.txt',
                    args.outputs / f'Report-OpenDuck-Pro-X-{version}.txt', root / 'dashboard/delivery041.txt']:
    destination.write_text(report, encoding='utf-8-sig')

source_zip = root / f'releases/OpenDuck-Pro-X-{version}-Sorgenti.zip'
vst_zip = root / f'releases/OpenDuck-Pro-X-{version}-VST3.zip'
ignore_dirs = {'.git', '__pycache__', 'node_modules', '.vs', 'build', 'work', 'releases', 'outputs'}
ignore_ext = {'.exe', '.dll', '.lib', '.obj', '.pdb', '.ilk', '.ipdb', '.iobj', '.zip', '.7z', '.pyc', '.log'}
source_count = 0
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for folder in ['Source', 'tests', 'tools', 'docs', 'Assets', 'installer', 'cmake', 'third_party']:
        for current, directories, files in os.walk(root / folder):
            directories[:] = sorted(name for name in directories if name not in ignore_dirs)
            for name in sorted(files):
                file = Path(current) / name
                if file.suffix.lower() in ignore_ext or file.name == '.git' or file.is_symlink():
                    continue
                archive.write(file, file.relative_to(root).as_posix())
                source_count += 1
    for name in ['CMakeLists.txt', 'README.md', 'LICENSE.txt', '.clang-format']:
        archive.write(root / name, name)
        source_count += 1
with zipfile.ZipFile(vst_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for file in sorted(bundle.rglob('*')):
        if file.is_file():
            archive.write(file, (Path('OpenDuck Pro X.vst3') / file.relative_to(bundle)).as_posix())
for file in [source_zip, vst_zip]:
    with zipfile.ZipFile(file) as archive:
        require(archive.testzip() is None, f'ZIP CRC failed: {file}')
manifest = {'version': version, 'publisher': 'Black Circuit', 'builtSHA256': built_hash,
            'installerSHA256': sha(setup), 'sourceZipSHA256': sha(source_zip),
            'vstZipSHA256': sha(vst_zip), 'sourceFileCount': source_count,
            'automatedChecks': checks, 'steinbergPassed': int(steinberg_match[1]),
            'pluginvalStrictness': 10, 'sourceSHA256': evidence['sourceSHA256'],
            'nativeAbleton': native, 'installerQAComplete': True,
            'productionSetupExecuted': False, 'historicalCleanupComplete': False}
(release / 'release-evidence.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf-8')
files = [setup, source_zip, vst_zip, release / 'Report-verifica.txt', release / 'release-evidence.json']
(root / f'releases/SHA256SUMS-v{version}.txt').write_text(''.join(
    f'{sha(file)}  {file.relative_to(root / "releases").as_posix()}\n' for file in files), encoding='utf-8')
dashboard_setup = root / f'dashboard/Black-Circuit-OpenDuck-PRO-X-{version}-Setup.exe'
shutil.copy2(setup, dashboard_setup)
require(sha(dashboard_setup) == sha(setup), 'Dashboard installer copy differs')
user_setup = args.outputs / f'Black-Circuit-Open-Duck-{version}-Setup.exe'
shutil.copy2(setup, user_setup)
require(sha(user_setup) == sha(setup), 'User-facing installer copy differs')
status_path = root / 'dashboard/revision041-status.json'
status = read_json(status_path)
status['delivery'] = {'complete': True, 'installer': './' + dashboard_setup.name, 'report': './delivery041.txt'}
status['updated'] = datetime.now().astimezone().isoformat()
status_path.write_text(json.dumps(status, indent=2, ensure_ascii=False), encoding='utf-8')
print(json.dumps({'installerBytes': setup.stat().st_size, 'sourceZipBytes': source_zip.stat().st_size,
                  'vstZipBytes': vst_zip.stat().st_size, 'sourceFileCount': source_count,
                  'suiteChecksPassed': sum(item['passed'] for item in checks.values()),
                  'historicalCleanupComplete': False}, indent=2))
