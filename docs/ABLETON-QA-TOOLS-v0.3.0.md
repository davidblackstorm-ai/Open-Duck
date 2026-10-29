# Strumenti per il collaudo finale Ableton — 0.3.0

Questi strumenti preparano e verificano **una sola sessione QA finale**, dopo i gate non-DAW e l'integrazione dei modelli. Non avviano né chiudono Live e non certificano da soli ascolto, gesti UI, routing o qualità AI. Alla preparazione di questo documento nessuna baseline reale è stata catturata da questi nuovi script e nessuna preferenza attiva è stata letta o modificata.

## File e responsabilità

- `tools/prepare_ableton_qa_v030.ps1`: modalità `Baseline` e `Restore`, solo operazioni file. Richiede Live già chiuso; non invoca Stop-Process, Start-Process, registro, controller di impostazioni o installazioni.
- `tools/gather_ableton_qa_v030.py`: lettura post-run delle sole evidenze indicate esplicitamente; report JSON pubblico privo del contenuto delle preferenze. Derivato dal precedente `gather_ableton_qa.py`, con contratto schema3 e controlli di percorso più stretti.
- Directory fissa: `<ProjectRoot>/work/Ableton-QA-v0.3.0/`. Il baseline non viene mai sovrascritto. I tool non modificano il nucleo DSP congelato.

I parametri di percorso devono essere assoluti su drive locale, senza alternate data stream o componenti relativi. Ogni componente esistente viene controllato contro link/reparse point. Progetto, preferenze e bundle di produzione hanno confini separati. Non si seguono directory reparse durante l'hash del bundle.

## 1. Baseline, soltanto alla fine degli altri gate

Il coordinatore chiude Live tramite UI e ne verifica la chiusura. Indica esplicitamente la cartella Preferences della versione Live usata, senza scansioni di altre versioni. `OriginalSet` è il set QA originale nel progetto, da conservare; `QaSet` è il percorso distinto del set che verrà creato con Save As. Quest'ultimo non deve ancora esistere. Il bundle di produzione resta intatto e non viene usato per sostituire il DLL finale del progetto.

Esempio di struttura dei comandi; i valori fra parentesi angolari sono segnaposto da risolvere dal coordinatore, non istruzioni da eseguire letteralmente:

```powershell
$qaParameters = @{
    ProjectRoot = 'E:\Codex\Lavori\OpenDuck Pro X'
    PreferencesRoot = '<cartella Preferences esatta della versione Live>'
    OriginalSet = '<set QA originale .als dentro il progetto>'
    QaSet = '<nuovo set QA 0.3.0 .als dentro il progetto>'
    ProductionBundle = 'C:\Program Files\Common Files\VST3\OpenDuck Pro X.vst3'
}
& '<ProjectRoot>\tools\prepare_ableton_qa_v030.ps1' -Mode Baseline @qaParameters
```

Il target finale è fisso:

`<ProjectRoot>/build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents/x86_64-win/OpenDuck Pro X.vst3`.

Il baseline conserva esclusivamente i quattro file autorizzati, se esistenti:

1. `Preferences.cfg`
2. `Library.cfg`
3. `Options.txt`
4. `UsageData.cfg`

Per ciascuno registra esistenza, SHA256, lunghezza, attributi e timestamp. Salva copie verificate in `Preferences-before/`; conserva il set originale in `TEST-LAB-before.als`, con hash; registra hash del target finale, esistenza e lista hash del bundle di produzione. `baseline.json.sha256` verifica l'integrità accidentale del manifest: non è una firma digitale contro modifiche deliberate. Live deve essere chiuso e il target invariato durante la capture. Se il QA root esiste già, il comando si ferma senza sostituire il baseline, anche quando quel baseline è incompleto.

La configurazione temporanea di Live viene eseguita dal coordinatore tramite l'interfaccia ufficiale e usa la cartella VST3 custom del build. Questo script non imposta la cartella, non lancia il DAW e non modifica il bundle di sistema.

## 2. Sessione e restore

Il coordinatore svolge la singola sessione finale, salva il set modificato **esattamente nel `QaSet` pianificato**, raccoglie screenshot selezionati e osservazioni audio/UI, poi chiude Live tramite UI. Non cambia `OriginalSet` e non modifica il DLL finale durante il collaudo.

Con Live chiuso:

```powershell
& '<ProjectRoot>\tools\prepare_ableton_qa_v030.ps1' -Mode Restore @qaParameters
```

Prima di toccare una preferenza, Restore controlla:

- identità dei percorsi e integrità del baseline;
- lista di esattamente quattro nomi autorizzati e snapshot originali con hash corretti;
- hash del set originale e della sua copia;
- hash del binario finale e integrità del bundle di produzione;
- assenza di Live e assenza di link/reparse nel percorso.

Quindi copia **tutti** i file autorizzati attualmente presenti in una directory privata e univoca `Restore-snapshots/<timestamp-guid>/`, con `before-restore.json`, prima della prima sostituzione/rimozione. Un errore conserva questi backup e il baseline. Nessuna rimozione ricorsiva viene eseguita. Se un file inizialmente assente è stato generato da Live, viene prima salvato e poi rimosso dal solo percorso autorizzato. I file inizialmente presenti vengono ripristinati dal loro snapshot verificato, inclusi timestamp e attributi.

Ogni modifica ricontrolla Live chiuso, percorso sicuro e assenza di cambiamenti rispetto al backup appena eseguito. Il report privato della singola operazione è `Restore-snapshots/<timestamp-guid>/restore-report.json`. Ripetere Restore genera un backup nuovo; non cancella le prove del tentativo precedente. Non è un comando di recovery dei progetti o del bundle: questi file non vengono mai sovrascritti dal tool. Una deviazione inattesa dei loro hash ferma la procedura per revisione.

## 3. Report post-run pubblico

Solo dopo il restore verificato:

```powershell
& '<python-runtime>\python.exe' '<ProjectRoot>\tools\gather_ableton_qa_v030.py' `
    --root 'E:\Codex\Lavori\OpenDuck Pro X' `
    --qa-set '<QaSet pianificato nel baseline>' `
    --original-set '<OriginalSet pianificato nel baseline>' `
    --output 'E:\Codex\Lavori\OpenDuck Pro X\work\Ableton-QA-v0.3.0\POST-RUN-REPORT.json'
```

`--original-set` è opzionale: in sua assenza viene comunque verificato il percorso originale del baseline. `--output` è opzionale e senza di esso il JSON va su stdout. Un file di report esistente non viene sovrascritto; per un controllo provvisorio si usa stdout o un nome JSON distinto. Exit code: **0** se tutti i check sono veri, **2** se il report è valido ma un check è falso, **1** per errore di parsing, confine o preflight. Le copie private del baseline non sono considerate prove da distribuire.

Il report verifica i nomi di gate usati dal packaging:

```text
fourPreferencesRestoredExactly
baselinePreferenceSnapshotsIntact
productionBundleUnchanged
abletonClosed
initialProcessStateRestored
targetReleaseBinaryUnchanged
schema3StateCaptured
schema3StatesComplete
```

Aggiunge inoltre `originalQASetUnchanged`, `originalQASetSnapshotIntact` e `schema3ModernEngineCaptured`. `postRunChecksPassed` è vero soltanto se tutti i check, inclusi quelli aggiuntivi, sono veri.

### Contratto schema3 osservato

Il file di stato reale del build `build/visual-qa-v0.3.0/QA-StateRoundtrip.odpx` contiene **413 parametri APVTS unici**: 131 pubblicati precedentemente e 282 nuovi (`47 × 6`). Il helper richiede tutti i 413 ID effettivi, valori finiti, assenza di duplicati e permette eventuali ulteriori ID distinti. Non usa il conteggio approssimativo 425.

Per ogni chunk JUCE `VC2!` contenuto nello stato esadecimale del set `.als` salvato, il report estrae esclusivamente lo stato `OpenDuckState`:

- schema3, `modernMask`, `revision3Mask`, `audioRouting` e le altre proprietà root;
- sei `Curves/Curve`, con mode 0–5 unici;
- diciotto `Bands3/BandCurve`, con tutte le coppie settore 0–5 / banda 0–2;
- geometria di ogni punto: `x`, `y`, `segment`, `curvature` e `reversed`, con limiti, ordine e massimo 256;
- 96 memorie `ShapeMemories3`, una per ciascuna coppia settore/profilo;
- shape dei gruppi presenti, dati dei parametri e XML originale del solo plugin, con SHA256.

Gli stati schema1/2 sono contati separatamente e non spacciati per prove del nuovo motore. La presenza di almeno uno stato schema3 con `revision3Mask > 0` è richiesta per il gate aggiuntivo del motore moderno. Il parser rifiuta XML con DOCTYPE, dimensioni oltre il limite o strutture incomplete. Non legge nomi di clip, media, cronologia browser, log Live o crash report nel report pubblico.

## Packaging e privacy

Possono entrare nel pacchetto esclusivamente:

- `docs/Ableton-QA-v0.3.0.md`, redatto sulla sessione osservata;
- `work/Ableton-QA-v0.3.0/POST-RUN-REPORT.json` verificato;
- screenshot scelti esplicitamente tramite manifest e relativo SHA256;
- i sorgenti dei due helper e questo documento.

**Escludere sempre** `Preferences-before/`, `Restore-snapshots/`, `TEST-LAB-before.als`, baseline privato, preferenze attive, intero set/media utente, log Live e crash report. Il report pubblico contiene soltanto nomi/hash dei quattro file, non il loro contenuto. La selezione dei file di release deve essere positiva e non una copia ricorsiva della cartella QA.

## Verifiche degli strumenti effettuate nello staging

Python `py_compile`: superato. PowerShell AST ParseFile: superato; controllo AST senza comandi di avvio/arresto processi o registro. Lo script Baseline/Restore **non è stato eseguito**, nemmeno contro preferenze attive.

Il parser puro ha verificato il fixture reale del plugin e un `.als` sintetico contenente soltanto quel chunk. Ha accettato il contratto 413/282/6/18/96 e rifiutato parametro mancante, banda mancante, campo `reversed` mancante e valore NaN; ha accettato un punto con `reversed=1`. Prova locale: `qa-tools-proof/parser-checks.json`. Non è un collaudo del restore su preferenze reali: tale operazione resta parte della sessione finale coordinata.
