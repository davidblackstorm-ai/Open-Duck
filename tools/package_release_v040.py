from pathlib import Path
import json,re,hashlib,shutil,zipfile,os
from datetime import datetime
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X');work=p/'work/revision040';release=p/'releases/0.4.0';release.mkdir(exist_ok=True)
outputs=Path(r'C:\Users\PC\Documents\Codex\2026-10-29\recupera-quello-che-stavamo-facendo-per\outputs');outputs.mkdir(exist_ok=True)
def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
setup=p/'releases/OpenDuck-Pro-X-0.4.0-Windows-x64-Setup.exe';binary=p/'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3'
repro=json.loads((p/'build/verification-v0.4.0/results.json').read_text(encoding='utf-8-sig'));assert repro['builtSHA256'].lower()==sha(binary)
installed=json.loads((work/'ableton-final-module-and-restore.json').read_text(encoding='utf-8-sig'));assert installed['modules'][0]['sha256'].lower()==sha(binary)
compiler=json.loads((p/'build/installer-production-v0.4.0.json').read_text(encoding='utf-8-sig'));assert compiler['setup']['sha256'].lower()==sha(setup)
qa=json.loads((p/'build/installer-qa-results-v0.4.0.json').read_text(encoding='utf-8-sig'));assert qa['builtSHA256'].lower()==sha(binary) and qa['uninstall'] and qa['legacyPresetPreserved'] and qa['modernPresetPreserved']
checks={};expected={'core':20,'ui':4,'ai5':5,'ai10':5,'actual-vst3-lifecycle-and-automation':85,'actual-vst3-host':11,'real-waveform-capture':108}
for name,count in expected.items():
    file=p/'build/verification-v0.4.0'/f'{name}.log';body=file.read_text(encoding='utf-8-sig');assert not re.search(r'^FAIL ',body,re.M);assert re.search(rf'RESULT[^\n]*\b{count} passed, 0 failed',body);checks[name]={'passed':count,'failed':0,'path':str(file),'sha256':sha(file)}
for name,file,count in [('stress',work/'release-stress.log',2),('steinberg',work/'release-steinberg.log',47)]:
    body=file.read_text(encoding='utf-8-sig');assert '0 failed' in body or '0 tests failed' in body;assert not re.search(r'^FAIL ',body,re.M);checks[name]={'passed':count,'failed':0,'path':str(file),'sha256':sha(file)}
pluginval=work/'release-pluginval.stdout.log';assert 'Strictness level: 10' in pluginval.read_text(encoding='utf-8-sig') and pluginval.read_text(encoding='utf-8-sig').rstrip().endswith('SUCCESS')
plan=json.loads((work/'ableton-plan.json').read_text());assert sha(plan['source'])==plan['originalSHA256']
objects=[]
for directory in ['Revision040Tests.dir','Revision040RegressionTests.dir','Revision040Vst3LifecycleTests.dir']:
    for file in (p/'build'/directory/'Release').glob('*.obj'):objects.append({'path':str(file),'bytes':file.stat().st_size})
cleanup={'completed':False,'blockedByAutomaticApprovalReview':True,'statedReason':'blocked by policy','attempts':['recursive removal of task-created intermediate directories','individual verified object files','one exact literal Revision040Tests.obj path'],'filesDeleted':0,'identifiedObjectFiles':len(objects),'identifiedBytes':sum(x['bytes'] for x in objects),'candidates':objects}
(work/'cleanup.json').write_text(json.dumps(cleanup,indent=2))
report=f'''BLACK CIRCUIT — OPENDUCK PRO X 0.4.0
Report locale di consegna — 29 ottobre 2026

SOFTWARE E INSTALLER VERIFICATI. PULIZIA INTERMEDI BLOCCATA.

Installer: {setup}
Dimensione: {setup.stat().st_size:,} byte
SHA256 installer: {sha(setup)}
SHA256 VST3 finale: {sha(binary)}
Progetto e sorgenti: {p}
Set di collaudo: {plan['qa']}

IMPLEMENTAZIONE
Template cromato conservato; editor JUCE nativo e controlli interattivi.
MIX e la posizione orizzontale lineare (-50..+50% del ciclo, neutro50%).
PHASE e la durata attorno al centro (0.5..1.5 volte la durata del preset).
CURVE cambia la curvatura. Nessun dry/wet o autogain nascosto sul nuovo MIX.
La traslazione e tagliata ai bordi senza wrap. Arco LED, lineetta e numero
condividono la stessa sorgente parametrica. Corretti residui luminosi del
riferimento statico, stati esclusivi, pannelli opachi e nome preset modificato.
Corretto separatamente l'estremo numerico della shape Arch prima delle potenze
frazionarie, con regressione dedicata. I sorgenti DSP/AI e lo stato non sono
stati alterati dalle sole correzioni grafiche successive, salvo questa
correzione numerica esplicitamente registrata.

700 preset: verificati 700 inviluppi DSP distinti, con catalogo e hash.
Nuova istanza INIT Soft Duck; ripristino delle sessioni separato da INIT.
Schema4 e parametri aggiunti in coda. I vecchi stati schema1-3 conservano
interpretazione MIX, audio ed editor precedenti. Conservare il set originale
se si vuole tornare a una versione precedente del plugin.

Sidechain esterna VST3: in Ableton, Sidechain > Audio From > traccia kick >
punto di prelievo. Main: basso o materiale da attenuare. Clic destro sul grafico
per soglia, isteresi, protezione retrigger e smoothing.
La waveform e assente senza audio acquisito; i dati visualizzati provengono
dalla sidechain reale, con isolamento di un kick e aggiornamento successivo.
Windows Media Player e rimasto escluso dalle sorgenti e non e stato fermato.

AI locale: catture5/10 secondi, analisi e render sul worker, nessuna applicazione
automatica. APPLY esplicito su miglioramento misurato; UNDO verificato anche
dopo un successivo edit manuale. Astensione quando mancano copertura o benefici.

VERIFICHE AUTOMATICHE FINALI
Core, geometria, stato, preset, preview/DSP: 20 pass, 0 fail.
UI, pulsanti, attachment e pixel LED: 4 pass, 0 fail.
VST3 effettivo, lifecycle, legacy e automazione nuovi parametri: 85 pass, 0 fail.
Host contract VST3: 11 pass, 0 fail.
Cattura waveform reale: 108 pass, 0 fail.
AI5s e AI10s: 5+5 pass, 0 fail.
Stress: quattro istanze, 120s audio ciascuna con automazione; 2 pass, 0 fail.
Validator Steinberg: 47 pass, 0 fail.
Pluginval strictness10: SUCCESS, uscita0.
Totale controlli riportati dalle suite: {sum(x['passed'] for x in checks.values())}, nessun fallimento finale.
Le prove fallite durante lo sviluppo restano nei log diagnostici e non sono
promosse a verifiche superate.

Misura DC dopo l'avvio, con smoothing: scarto massimo preview/DSP0.0041233;
salto massimo tra campioni0.00412361. Queste sono misure sui segnali di prova,
non una garanzia percettiva su qualunque sorgente.
Automazione wrapper:512 blocchi, valori MIX riflessi correttamente e differenza
RMS audio0.0519813 rispetto a una seconda istanza indipendente.
Render nativi a864/1296/1728/2592 pixel. Verificati10001 valori angolari e53579
pixel ciano di anello nelle prove. Il DPI di sistema non e stato modificato;
non viene dichiarato un collaudo fisico su altri monitor o computer.

COLLAUDO ABLETON LIVE12
Unico progetto QA0.4.0, riaperto dopo la rifinitura per verificare il richiamo.
Cinque istanze salvate indipendenti; nell'istanza modificata sono tornati
MIX59%, PHASE-17%, CURVE-4% e la shape scelta, senza INIT indesiderato.
168BPM /44.1kHz, latenza dichiarata11274 campioni (255.6ms).
A48kHz:12112 campioni (252.3ms); il prodotto e orientato al mix.
La latenza esposta e stata osservata e il ritardo DSP misurato: non viene
attribuito un null test di export DAW che non e stato eseguito.
Waveform vuota a startup; kick reale Track14/PostFX visibile dopo il Play.
Drag reali, indipendenza dei tre controlli, selezione shape, editing custom,
mini impostazioni, salvataggio e richiamo verificati.
Proposta AI reale su5 secondi: obiettivo1.01314 ->0.40973; APPLY esplicito,
UNDO e conservazione dell'edit manuale MIX verificati.
Nessuna modifica diretta a driver, buffer, volume o preferenze globali audio.
Il set originale0.3.1 e rimasto invariato (hash confrontato).
La DLL caricata nell'ultimo collaudo ha lo stesso SHA256 della build finale.

INSTALLER
Black Circuit compare nel publisher, nel testo del wizard e nella grafica.
Arte grafica ottenuta dal render nativo finale, non da un mockup.
QA isolata: installazione autentica0.3.0, upgrade0.4.0, reinstallazione e
rimozione superati; payload identico alla build e preset personali conservati.
Il setup di produzione e compilato; non e stato eseguito in modalita elevata.
Per il collaudo e stata copiata e verificata la DLL nella posizione standard.
Firma Authenticode non presente. Nessuna pubblicazione o invio esterno.

PACCHETTI
OpenDuck-Pro-X-0.4.0-Sorgenti.zip: sorgenti, test, tool, documentazione, asset
ed effettivi sorgenti JUCE/VST3 SDK; esclusi build, log privati e cache.
OpenDuck-Pro-X-0.4.0-VST3.zip: payload VST3 della build verificata.
SHA256SUMS-v0.4.0.txt contiene gli hash dei pacchetti.
Per ricompilare: tools/build_v040.ps1 [-CMakePath percorso] [-WithStress].
Per rigenerare gli asset: tools/create_installer_assets_v040.py dopo il build.
Per compilare il setup: tools/compile_installer_v040.ps1 -InnoCompiler ISCC.exe.
Per ripetere la QA di upgrade serve anche l'installer autentico0.3.0 archiviato;
non vengono distribuite copie dell'ambiente privato dentro il pacchetto sorgenti.

PULIZIA NON COMPLETATA
Il controllo automatico ha respinto la rimozione ricorsiva, la rimozione dei
soli oggetti verificati e anche un singolo percorso .obj letterale:
"blocked by policy". Non sono stati aggirati i controlli o cancellati file.
Restano {len(objects)} file .obj individuati, circa {sum(x['bytes'] for x in objects)/1024**2:.1f}MiB,
oltre agli archivi di recupero conservati. Questa attivita resta rossa nel
pannello e non e dichiarata terminata. Sorgenti, setup finale e recovery intatti.
'''
(p/'docs/VERIFICATION-v0.4.0.txt').write_text(report,encoding='utf-8-sig');(release/'Report-verifica.txt').write_text(report,encoding='utf-8-sig');(outputs/'Report-OpenDuck-Pro-X-0.4.0.txt').write_text(report,encoding='utf-8-sig');(p/'dashboard/delivery040.txt').write_text(report,encoding='utf-8-sig')
shutil.copy2(p/'build/verification-v0.4.0/Preview/Factory-Presets-700.csv',release/'Factory-Presets-700.csv')
for category in ['Preview','Evidence']:(release/category).mkdir(exist_ok=True)
for file in (p/'build/verification-v0.4.0/Preview').glob('*.png'):shutil.copy2(file,release/'Preview'/file.name)
for file in work.glob('Ableton-*.png'):shutil.copy2(file,release/'Evidence'/file.name)
for item in checks.values():shutil.copy2(item['path'],release/'Evidence'/Path(item['path']).name)
shutil.copy2(pluginval,release/'Evidence'/pluginval.name)
for file in ['installer-production-v0.4.0.json','installer-qa-results-v0.4.0.json']:shutil.copy2(p/'build'/file,release/'Evidence'/file)
for file in ['ableton-observations.json','ableton-saved-states.json','ableton-final-module-and-restore.json','numeric-endpoint-fix.json','cleanup.json']:shutil.copy2(work/file,release/'Evidence'/file)
sourceZip=p/'releases/OpenDuck-Pro-X-0.4.0-Sorgenti.zip';vstZip=p/'releases/OpenDuck-Pro-X-0.4.0-VST3.zip'
ignoreDirs={'.git','__pycache__','node_modules','.vs','build','work','releases','outputs'};ignoreExt={'.exe','.dll','.lib','.obj','.pdb','.ilk','.ipdb','.iobj','.zip','.7z','.pyc','.log'}
count=0
with zipfile.ZipFile(sourceZip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for folder in ['Source','tests','tools','docs','Assets','installer','cmake','third_party']:
        for current,dirs,files in os.walk(p/folder):
            dirs[:]=sorted(x for x in dirs if x not in ignoreDirs)
            for name in sorted(files):
                file=Path(current)/name
                if file.suffix.lower() in ignoreExt or file.name=='.git' or file.is_symlink():continue
                z.write(file,file.relative_to(p).as_posix());count+=1
    for name in ['CMakeLists.txt','README.md','LICENSE.txt','.clang-format']:z.write(p/name,name)
with zipfile.ZipFile(vstZip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    bundle=binary.parents[2]
    for file in sorted(bundle.rglob('*')):
        if file.is_file():z.write(file,(Path('OpenDuck Pro X.vst3')/file.relative_to(bundle)).as_posix())
codeFiles=[p/'CMakeLists.txt',*sorted((p/'Source').glob('*.*')),*sorted((p/'tests').glob('Revision040*.cpp'))]
manifest={'version':'0.4.0','publisher':'Black Circuit','created':datetime.now().astimezone().isoformat(),'builtSHA256':sha(binary),'installerSHA256':sha(setup),'sourceZipSHA256':sha(sourceZip),'sourceFileCount':count+4,'vstZipSHA256':sha(vstZip),'automatedChecks':checks,'pluginval':{'strictness':10,'success':True,'exitCode':0,'sha256':sha(pluginval)},'installerQAComplete':True,'productionSetupExecuted':False,'ableton':installed,'cleanup':cleanup,'sourceSHA256':{f.relative_to(p).as_posix():sha(f) for f in codeFiles}}
(release/'release-evidence.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
files=[setup,sourceZip,vstZip,release/'Report-verifica.txt',release/'Factory-Presets-700.csv',release/'release-evidence.json']
(p/'releases/SHA256SUMS-v0.4.0.txt').write_text(''.join(f'{sha(f)}  {f.relative_to(p/"releases").as_posix()}\n' for f in files),encoding='utf-8')
shutil.copy2(setup,p/'dashboard/Black-Circuit-OpenDuck-PRO-X-0.4.0-Setup.exe')
assert sha(p/'dashboard/Black-Circuit-OpenDuck-PRO-X-0.4.0-Setup.exe')==sha(setup)
print(json.dumps({'installerBytes':setup.stat().st_size,'sourceZipBytes':sourceZip.stat().st_size,'vstZipBytes':vstZip.stat().st_size,'sourceFiles':count+4,'checks':sum(x['passed'] for x in checks.values()),'cleanupComplete':False},indent=2))
