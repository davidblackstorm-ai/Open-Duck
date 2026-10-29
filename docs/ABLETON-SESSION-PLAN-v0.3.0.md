# Piano della singola sessione finale Ableton — OpenDuck Pro X 0.3.0

Piano preparato tramite lettura dei **soli due set del laboratorio dedicato** e dei due WAV LAB. Nessun avvio di Ableton, modifica dei set o accesso alle preferenze attive è stato effettuato per questa analisi. Le verifiche descritte sotto sono ancora da eseguire; i risultati precedenti 0.2.0 non costituiscono un collaudo del nuovo binario.

## Base consigliata e percorsi esatti

Usare il set originale, più semplice e a 145 BPM:

`E:\Codex\Lavori\OpenDuck Pro X\OpenDuck Pro X - TEST LAB Project\OpenDuck Pro X - TEST LAB.als`

Aprirlo **soltanto dopo** tutti i gate non-DAW e il baseline finale delle quattro preferenze. Come prima operazione di salvataggio, usare il comando ufficiale **Save Live Set As** per creare:

`E:\Codex\Lavori\OpenDuck Pro X\OpenDuck Pro X - TEST LAB Project\OpenDuck Pro X - TEST LAB - 0.3.0.als`

Il target non esisteva all'inventario e deve ancora essere assente quando si esegue Baseline. Se compare prima del baseline, non sovrascriverlo: riesaminare la provenienza e scegliere un target nuovo in modo esplicito. Il baseline `OriginalSet` deve indicare il file originale sopra, `QaSet` il nuovo file 0.3.0. Il set 0.2.0 resta un riferimento storico e non viene aperto nella sessione finale. Il comando Save As crea una nuova versione del set nello stesso progetto senza duplicare i campioni già presenti. [Manuale Ableton: gestione e salvataggio dei set](https://www.ableton.com/en/manual/managing-files-and-sets/).

| Set esaminato | Stato reale | Ragione della scelta |
|---|---|---|
| `OpenDuck Pro X - TEST LAB.als` | 145 BPM, 5 tracce principali + 2 return; **2** istanze OpenDuck schema1, 71 parametri ciascuna. | Base originale, meno stato e automazione di vecchi collaudi. Aggiungere due istanze nuove 0.3 per arrivare a quattro totali. |
| `OpenDuck Pro X - TEST LAB - 0.2.0.als` | 168 BPM, 7 tracce principali + 2 return; **4** istanze schema2, 131 parametri ciascuna; due legacy `modernMask=0`, due modern `modernMask=63`. | Contiene custom 9/193 punti, tutti i sei settori ON nella coppia moderna, MIX 80/49,444 e automazioni di collaudo; base più ambigua per testare INIT e l'AI nuova. |

SHA256 originali, da ricontrollare al baseline:

```text
TEST LAB.als
d9819a87d33c18e1e6b855919e803e670bb3932d4339ca2c39a1f3718b5186e4

TEST LAB - 0.2.0.als
cdc92a777f7980f036f72d01a51984ec5dece56a389d0a61e6b91f41bd5e24eb
```

## Inventario del set originale

Gli ID XML restano più affidabili dei prefissi numerici nei nomi visualizzati: inserire nuove tracce cambia la numerazione visibile. La proprietà `Speaker/Manual=true` significa traccia **attiva**, non muta.

| ID | Traccia originale | Clip e routing effettivi | OpenDuck |
|---:|---|---|---|
| 12 | `1-MIDI` | Clip Session `Track 0`, loop 0–16 beat; 16 note **60**, velocity 100, durata 0,25 beat, una nota per beat. MIDI To `Track.8 / DeviceIn.0.0`, cioè il plugin del primo bass. | Nessun plugin nella traccia MIDI. |
| 13 | `2-MIDI` | Vuota; MIDI To None. | Nessuno. |
| 8 | `3-LAB-Bass-145` | Clip Session Bass 0–16 beat; uscita Main; istanza A. Riceve il MIDI della traccia 12. | Schema1, MIX 100; Ducking e Spectral ON; trigger storico MIDI. Sidechain Live ON da `AudioIn/Track.14/PostFxOut`. |
| 15 | `4-LAB-Bass-145` | Clip Session Bass 0–16 beat; uscita Main; istanza B. Una envelope storica di parametro con 3 FloatEvent. | Schema1, MIX 100; Ducking e Spectral ON; trigger storico MIDI, **senza** routing MIDI dedicato dalla traccia12. Sidechain Live ON da Track14 Post FX. |
| 14 | `5-LAB-Kick-145` | Clip Session Kick 0–16 beat, uscita Main; sorgente sidechain. | Nessuno. |
| 2 / 3 | `A-Reverb` / `B-Delay` | Return del laboratorio; uscita Main. | Nessun OpenDuck. |

Le clip Session di Bass/Kick sono warped, loop attivo 0–16 beat e start relativo zero. L'Arrangement originale ha materiale su MIDI/Bass8/Kick14 circa da beat 238,979 a 275,019, mentre la posizione di trasporto salvata è circa 61,600 beat e Loop Arrangement è OFF. Premere soltanto Play alla posizione salvata può quindi produrre silenzio. Per la sessione finale usare i **clip Session già presenti**, lanciati insieme su confine di battuta; non scambiare silenzio di Arrangement per difetto del plugin.

L'originale ha due istanze, non quattro. Le quattro istanze presenti nel set 0.2.0 sono sulle tracce ID 8, 15, 16, 17; il Kick resta ID14, visualizzato come traccia7 dopo gli inserimenti. Le etichette salvate nella sidechain sono in parte obsolete (`4-LAB-Kick`, `5-LAB-Kick`, `6-LAB-Kick`): il target reale è sempre **Track.14/PostFxOut**. Nella UI scegliere la traccia Kick effettiva e Post FX, senza dedurre il routing dalla vecchia stringa.

Il set 0.2 ha 8 envelope/26 FloatEvent su ciascuna delle due nuove tracce Bass, oltre all'envelope della traccia15. Non copiare quei blocchi di automazione nell'istanza destinata alle capture AI: altererebbero il contesto durante l'analisi.

### Campioni disponibili, senza risorse esterne

Entrambi i file sono in `Samples/Imported/`, stereo PCM16, **44.100 Hz, 291.972 frame, 6,620680272 s**, cioè circa quattro battute a 145 BPM. La piccola differenza rispetto a 16 beat ideali viene gestita dai marker warp già nel set.

| File | SHA256 |
|---|---|
| `LAB-Kick-145.wav` | `b9c10ae0afbd45016cabb8535fc2f284caa6942d6f1d6ffac84f641e9359df28` |
| `LAB-Bass-145.wav` | `7c9924eaf10ea580d2a23a17313f4aa8e74a49d0d82f76448807962eab908cf2` |

Non occorre cambiare il sample rate del DAW né spostare campioni. Registrare il sample rate realmente usato nel verbale e il PDC corrispondente: 11.274 campioni a 44,1 kHz, 12.112 a 48 kHz, 22.432 a 96 kHz.

## Preparazione finale senza aprire prima il DAW

1. Finire integrazione e quality gate dei sei modelli, suite native/host/AI, Steinberg, pluginval strictness10 e QA installer sul DLL finale. Congelare l'hash finale, diverso eventualmente dall'hash preliminare pre-AI.
2. Con Live già chiuso, eseguire `prepare_ableton_qa_v030.ps1 -Mode Baseline` con OriginalSet e QaSet esatti sopra, PreferencesRoot esplicito della versione Live e bundle di produzione nominato. Il baseline non è riutilizzato dalla sessione 0.2.0.
3. Avviare Live una sola volta, usare la configurazione ufficiale della cartella VST3 custom verso `<ProjectRoot>/build/OpenDuckProX_artefacts/Release/VST3`, senza sostituire il bundle di sistema. Aprire il set originale e fare immediatamente Save As 0.3.0. Verificare il nome della copia nella UI prima di salvare altre modifiche.
4. Accertare quale DLL è realmente caricato. Il nuovo `audit_loaded_openduck_v030.ps1` legge soltanto filename dei moduli del PID Live esplicito e hash dei file su disco; richiede il baseline e lo stesso hash del target finale. Un mismatch di path/hash non viene presentato come test del build corretto. Non legge memoria del processo, non usa UI/Win32 di controllo e non modifica file del plugin.

Esempio da eseguire dal coordinatore **durante quella sola sessione**, dopo avere un'istanza caricata:

```powershell
& '<ProjectRoot>\tools\audit_loaded_openduck_v030.ps1' `
    -LiveProcessId <PID Live osservato> `
    -ProjectRoot 'E:\Codex\Lavori\OpenDuck Pro X' `
    -Output 'E:\Codex\Lavori\OpenDuck Pro X\work\Ableton-QA-v0.3.0\LOADED-MODULES.json'
```

Exit0 richiede un modulo OpenDuck trovato e tutti gli OpenDuck caricati dal percorso del DLL finale del progetto, con hash coerente col baseline; exit2 indica prova negativa, exit1 errore/accesso non disponibile. Si tratta di prova di metadati OS e file su disco, insieme al congelamento del binario prima/dopo la sessione, non di ispezione dei byte in memoria.

## Quattro istanze finali con ruoli espliciti

Conservare A/B come controlli di compatibilità. Creare **due nuove tracce audio pulite** nel set QA, copiarvi soltanto il clip Session Bass e inserire OpenDuck dal browser. Non duplicare una traccia contenente plugin e automazione se si vuole verificare un vero INIT.

| Ruolo | Destinazione | Uso |
|---|---|---|
| A | Bass originale ID8 | Stato storico caricato senza upgrade automatico; riapertura GUI, confronto di valori salvati. |
| B | Bass originale ID15 | Seconda istanza legacy indipendente; non usarla come misura di profondità o AI. |
| C | Nuova traccia `QA-0.3-MAIN` | Nuova istanza INIT; test dei sei settori, sei capture/Apply/Undo AI, curve e automazione rappresentativa. |
| D | Nuova traccia `QA-0.3-INDEPENDENT` | Nuovo INIT indipendente; copia/stato alternativo e verifica che C non modifichi D. Può ricevere il routing MIDI per la prova note. |

Le nuove tracce non hanno ID predeterminati: verificarli dalla UI e poi dal set salvato. Assegnare **a entrambe** la sidechain Live dal Kick14 Post FX e verificare il meter SC reale; la ricezione MIDI è una configurazione diversa e non è provata dalla sola sidechain audio. Il routing Post FX è prima del mixer: il fader della traccia Kick non deve modificare il segnale della sidechain. [Manuale Ableton: punti di routing](https://www.ableton.com/en/manual/routing-and-i-o/).

Durante le misure ascoltare/processare **un solo Bass per volta**: A/B/D non devono sommare percorsi dry paralleli al Bass C. Tenere il Kick attivo per la sidechain; per sentire solo il Bass si può abbassare il suo fader Main mantenendo il tap Post FX, controllando che SC continui a ricevere. Per il confronto musicale rialzarlo al valore annotato. Controllare anche send/return: code di riverbero/delay non sono la profondità residua del plugin.

## Sequenza unica di collaudo nella copia

### A. INIT, restore e controlli manuali

- Aprire C appena inserito: Ducking, primo shape in alto a sinistra, Amount/MIX100, soli Ducking ON, punti OFF. Il fatto che A/B aprano in stato storico diverso è corretto.
- Chiudere/riaprire l'editor di A/B: i valori storici e il ramo legacy restano. Non cliccare Upgrade prima di aver documentato questa compatibilità.
- Lanciare i clip Session Bass C e Kick insieme. Attendere almeno un ciclo completo dopo il warmup/PDC prima di giudicare il monitor. Verificare un kick per periodo, Main/SC/Out sullo stesso frame e Freeze reale.
- Su C confrontare Amount0/50/100, MIX0/100, Attack/Release, Tension, Phase FINE e Smoothing; riportare valori e differenza osservata, senza trattare ogni movimento grafico come prova audio.
- Provare freccia sinistra/destra e categoria/dropdown del catalogo nel settore: il totale 700 è già validato dalla suite, non servono 700 click manuali. Non devono variare MIX/trigger durante il browsing.
- Attivare Edit Points soltanto esplicitamente: doppio click anche tardi nel ciclo, Ctrl campana, Free Draw, quindi disattivare i punti senza cancellare la shape. Controllare che Attack/Release non si azzerino creando un nodo tardivo.

### B. Sei settori, uno alla volta

Selezionare il settore **non cambia gli ON**: prima di ogni caso isolare quello da ascoltare, con gli altri cinque OFF e SOLO coerente. MIX100. GroupOff. Ripristinare il baseline della prova prima del caso successivo. Non chiamare “inaudibile” un settore selezionato ma lasciato OFF.

| Settore | Caso osservabile nel DAW | Evidenza minima |
|---|---|---|
| Ducking | Shape di fabbrica e custom, profondità, Attack/Release/Phase; confronto bypass allineato. | Screenshot valori/monitor + A/B dello stesso Bass. |
| Spectral | SC Kick reale e intervallo di frequenze; differenza fra processo spettrale e full range. | SC attivo e variazione del contenuto, senza pretendere che una curva scalare descriva ogni frequenza. |
| Multiband | 2 bande Low/High e High Solo; poi 3 curve indipendenti con profondità e tempi differenti. | MID/CrossHigh disabilitati in 2 bande e ripristinati in3; controlli banda legati alla curva selezionata. |
| Morph | Variazione Character/morph conservando la chiusura degli endpoint. | Differenza del recupero; curva chiusa non riaperta da un blend nascosto. |
| Groove | Pattern step e Swing su clip stabile, Attack/Release separati dallo Speed. | Periodo invariato quando si modifica Release. |
| Transient | Trigger SC su kick breve e poi Input; meter e shape seguono il programma dopo PDC. | Nessun gate che termina prima dell'audio ritardato. |

Poi abilitare più settori in serie per un controllo combinato, annotando gli ON effettivi e il carico. Lasciare invariati i progetti originali. Un semplice ascolto su Bass/Kick non rimpiazza le misure sintetiche di selettività e −60 dB già nei 51 check DSP.

### C. Sei analisi AI senza contesto ambiguo

Tempo **145 BPM invariato**, host Play stabile, Sync musicale riproducibile, GroupOff. Trigger Repeat oppure Sidechain; non usare MIDI o Hz libero per attestare sei analisi riuscite, perché il controller si astiene intenzionalmente senza un contesto riproducibile.

Per ciascuno dei sei settori su C:

1. Isolare il settore ON, conservare il punto di partenza; verificare Bass e SC non silenziosi. Fissare finestra 5 o 10 secondi e non cambiare parametri, tempo, trasporto o routing durante capture/ottimizzazione.
2. Avviare Analyze e attendere lo stato effettivo. Registrare settore, finestra, stato, qualità/valutazione visibile e se una proposta è stata accettata, respinta o giudicata senza miglioramento. “Modello caricato” non è una misura del miglioramento.
3. Se c'è una proposta reale, Apply, forza proposta, confronto e Undo devono agire soltanto sul settore corrente. Verificare D e almeno un altro slot C invariati. Non forzare un cambiamento se la baseline viene mantenuta perché migliore.
4. Salvare un risultato significativo nello slot del settore; procedere al settore successivo. Fotografare i sei esiti senza inventare un successo in caso di insufficienza, stale context o assenza di miglioramento.

Il training e il renderer sono offline/local CPU; questa sessione osserva l'integrazione nel DAW. Il verbale finale deve riportare esiti reali per sei settori, non una sola capture replicata sei volte.

### D. MIDI, trasporto e automazione senza il vecchio crash tempo

- La clip MIDI disponibile invia **nota60**, non36. Per la prova D selezionare nel routing Live la nuova traccia D e il device OpenDuck effettivo; filtro note60 accetta e note61 esclude; nessun MIDI inventato da shell o API interne. Tornare al routing/caso desiderato prima del salvataggio finale.
- Se si preme Analyze in MIDI, lo stato previsto è astensione esplicita per mancanza di eventi registrati nella capture AI: non è un fallimento del trigger MIDI audio. Documentare separatamente i due comportamenti.
- Host Stop deve rilasciare; Run while stopped consente prosecuzione solo se attivato. Provare un seek tramite controlli UI/Arrangement nella copia, mantenendo i clip realmente in esecuzione per non confondere Session/Arrangement.
- **Non usare `set_value`/ValuePattern UIA sul campo tempo Ableton**: quel percorso è già stato associato a un crash precedente. La base originale è già145 BPM, quindi non è necessario modificarlo. Se una variazione è indispensabile, usare soltanto un gesto UI ordinario di focus e digitazione con verifica visiva; non scrivere memoria, preferenze o XML del set. Le variazioni BPM del wrapper sono già testate prima del DAW.
- Soltanto dopo le sei capture stabili, registrare una breve automazione rappresentativa su C, per esempio Amount o Phase di un settore, e verificare il playback. Fermare Recording e Automation Arm prima di altre capture/operazioni che potrebbero registrare gesti non voluti. Non usare le vecchie automazioni 0.2 per dimostrare il comportamento 0.3.

### E. Persistenza, salvataggio e uscita

- Lasciare sei slot distinguibili su C, una shape/banda custom e valori diversi su D; chiudere e riaprire i relativi editor senza INIT involontario.
- Salvare esclusivamente `TEST LAB - 0.3.0.als`. Riaprire **la medesima copia nel medesimo avvio di Live**, se serve provare il project restore, senza aprire altri progetti utente né una seconda sessione di collaudo. Verificare curve, punti, trigger e stati distinti.
- Se utile, esportare una breve evidenza audio dalla sola traccia QA in una cartella del progetto. Non descrivere un grafico/meters come sostituto dell'audio realmente esportato o ascoltato.
- Chiudere Live dalla UI; verificare processo terminato. Eseguire Restore delle sole quattro preferenze con lo script corretto e validato dal coordinatore. Non riavviare Live dopo il restore per “verificarlo”: riscriverebbe le preferenze.
- Eseguire `gather_ableton_qa_v030.py` sul nuovo set. Devono risultare veri tutti i gate di restore, original set/snapshot, bundle di produzione, target hash e schema3. Il parser deve trovare 413 ID,6 curve,18 band curve, segment/reversed e96 memorie per stato completo; schema3 con mask0 resta compatibilità legacy, non prova del motore nuovo.

## Compatibilità del decoder con i set reali

Il helper schema3 è stato applicato in sola lettura al contenitore dei due ALS: riconosce correttamente il formato JUCE `VC2!` dentro `Vst3Preset/ProcessorState` esadecimale di Ableton. L'originale produce **2 chunk legacy,0 schema3**; il set0.2 **4 chunk legacy,0 schema3**. Non vengono falsamente promossi a stati 0.3. Il fixture del processor nuovo ha già validato il contratto 413/282/6/18/96 e i campi dei segmenti; resta da acquisire l'ALS0.3 effettivamente salvato da Live.

## Verbale e prove da consegnare

Creare `docs/Ableton-QA-v0.3.0.md` soltanto dopo il test, con hash finale, sample rate e buffer osservati, routing reale, quattro ruoli delle istanze, esiti dei sei settori/sei AI, automazione, restore e problemi eventualmente risolti. Riportare esplicitamente ciò che non è stato verificato.

Prove selezionabili: `POST-RUN-REPORT.json`, `LOADED-MODULES.json`, screenshot nominati in manifest e audio QA scelto. Escludere sempre backup delle preferenze, restore snapshots, baseline privato, set/media utente completi, log Live e crash report. Una schermata nera, un About con sola versione o un file con nome simile non provano da soli che Live abbia caricato il DLL finale corretto.
