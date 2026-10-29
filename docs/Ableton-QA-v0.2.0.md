# Ableton Live 12.4.7 — collaudo OpenDuck Pro X 0.2.0

**Stato: prove funzionali moderne eseguite nel TEST LAB; chiusura regolare e ripristino finale verificati, con tutti i nove controlli POST superati.** Il collaudo è ripreso dopo il recupero del set. Le limitazioni della selezione da sottomenu e dei gesti con modificatori restano dichiarate; non viene presentato un esito positivo indiscriminato dell’intera matrice.

## Ambito e preparazione

È stato usato esclusivamente il progetto dedicato `OpenDuck Pro X - TEST LAB Project`. Il set di lavoro della revisione è `OpenDuck Pro X - TEST LAB - 0.2.0.als`; il precedente set è conservato localmente in `work/Ableton-QA-v0.2.0/TEST-LAB-before.als`. Non sono coinvolti altri progetti musicali.

Il baseline locale registra Live inizialmente chiuso, file di preferenze, hash del bundle di sistema precedente e hash della build di destinazione. Una copia delle preferenze iniziali è in `work/Ableton-QA-v0.2.0/Preferences-before`. Il report POST riporta i confronti e gli hash; i backup privati non sono inclusi nel pacchetto di consegna.

La build 0.2.0 è stata caricata dalla cartella di build su E:, tramite selezione temporanea della sorgente plugin. Non è stata installata o sovrascritta la copia in Common Files/VST3. La selezione temporanea è stata rimossa al termine e la configurazione iniziale ripristinata, prima nell’interfaccia e poi mediante le copie esatte dei file dopo la chiusura.

## Identità del modulo effettivamente caricato

[loaded-module-after-recovery.json](../work/Ableton-QA-v0.2.0/loaded-module-after-recovery.json) registra il processo Live 4856 dopo il recupero e il modulo:

```text
E:\Codex\Lavori\OpenDuck Pro X\build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3
SHA256: 4790D534B47CCF579A884A73D0884386F22CB146AB18FB7795FF523741A76EFF
```

Il log di Live alle 14:19:23 del 28 ottobre 2026 riporta due caricamenti riusciti di `OpenDuck Pro X v0.2.0`. Non si deduce la versione dal solo CID, che è mantenuto uguale per compatibilità.

La lettura del bundle di sistema al checkpoint e nel POST finale conferma l’hash precedente:

```text
C:\Program Files\Common Files\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3
SHA256: C9903A33C4C175D4B8EBB8190A48E585FBABBCB103A0888A95980FE09A362C35
```

Questo controllo conferma che il collaudo ha usato la build su E: lasciando intatta la copia di produzione precedente.

## Risultati osservati

| Verifica | Stato attuale | Limite dell’osservazione |
|---|---|---|
| Caricamento reale 0.2.0 in Live | Superato | Versione nel log e hash del modulo caricato verificati |
| Richiamo dei due dispositivi legacy del TEST LAB | Osservato in Live | Stati preesistenti richiamati; la prova dei sei slot moderni è descritta separatamente sotto |
| Riproduzione del LAB con clip MIDI e routing sidechain esistenti | Osservata in Live | Non dichiara completati tutti i confronti AUDIO/MIDI/silenzio della matrice |
| HOST SYNC e Free BPM | Osservati in Live | Host a 120 e 168 BPM seguito dal plugin; Free a 120 BPM con host a 168, poi ritorno a Host Sync. Il setter UIA interrotto resta un episodio distinto; seek non viene dichiarato collaudato in questa sessione |
| Recupero del set dopo l’interruzione | Confermato: l’utente ha selezionato Sì e Live ha recuperato lo stesso set QA | Al rientro host a 168 BPM e trasporto fermo; non è il completamento della prova di setter automatizzato |
| Nuova istanza: primo shape, Classic ON, Amount 100%, MIX 80%, editing OFF | Superato | INIT con primo Soft Duck, solo Classic ON, Host Sync indicante i 168 BPM correnti dell’host |
| 700 preset e sette categorie nel menu Live | Categorie visualizzate; selezione da sottomenu non completata | L’attivazione della finestra proprietaria da parte dello strumento chiudeva il popup; il conteggio e i 700 comportamenti rimangono verificati dalla suite automatica |
| Frecce precedente/successivo | Superato | Da INIT, precedente raggiunge 700 / Hard Gate / Late 20 / Full; successivo ritorna a INIT |
| Shape e PHASE indipendenti | Superato sui profili esercitati | Classic custom con PHASE +16,1%; Spectral shape 2, Multiband 3, Morph 16 e Transient 8 conservati nei rispettivi slot. Non è una prova manuale di ogni preset |
| EDIT POINTS, doppio clic e punti X/Y | Superato | Classic: editing abilitato, punto creato con doppio clic e trascinato; Undo e Redo riusciti |
| Ctrl drag/rotella e altri gesti con modificatori | Non completato in Live | Lo strumento non permetteva di mantenere Ctrl durante il gesto; i test nativi della suite rimangono separati |
| FREE DRAW | Superato | Disegno Groove di 193 punti, poi richiamato dal set |
| Sei slot, DETAILS, ON/SOLO | Superato sui controlli esercitati | DETAILS visibile per Spectral/Multiband/Morph/Groove/Transient; sei motori ON e SOLO Spectral acceso/spento senza alterare gli ON |
| BAND FOCUS | Superato sul settore Transient | BAND ON, frequenza da 160 Hz al valore mostrato di 3,4 kHz, conservato al richiamo |
| Trigger AUDIO e routing sidechain | Osservati durante la riproduzione | Scena basso/kick, sidechain `LAB-Kick-145` Post FX (traccia 6 al momento della prova), waveform e meter attivi; non dichiara un export o un ascolto umano certificato |
| Automazione del nuovo `curveAmount5` | Registrazione F9 e riscontro nel file ALS confermati | Due eventi non negativi da 100% a circa 64,4444% all’istante PPQ 122,164825; binding VST3 corretto. Confermate anche le lane trigger/ON/SOLO |
| Indipendenza delle istanze | Superato | Tracce moderne 5 e 6 con MIX rispettivamente 80% e 49,4% |
| Nuovo salvataggio e riapertura del set moderno | Superato nel medesimo processo Live | Ctrl+S e Ctrl+O sullo stesso set QA: copia con MIX 49,4%, Amount 64,4%, Transient shape 8, BAND 3,4 kHz, AUDIO e tutti ON; richiamati anche Groove FREE DRAW e Classic custom/PHASE +16,1% |
| Ripristino esatto della configurazione iniziale e chiusura regolare | Superato | Tutti i nove controlli del POST sono veri; Live chiuso, preferenze esatte, set originale e bundle di sistema invariati |

I risultati già completati fuori da Ableton restano **73 test interni, 11 contratti dell’host VST3 indipendente, 47 controlli Steinberg, pluginval livello 10 e QA installer isolata**. Non vengono conteggiati come ulteriori prove Live.

## Persistenza osservata nel set moderno

Le due istanze moderne sono state duplicate e rese diverse mantenendo il MIX a 80% nell’originale e portandolo a 49,4% nella copia. Le curve dei diversi settori sono state selezionate e modificate senza ritorno a INIT. L’editor è stato chiuso/aperto e il set salvato, quindi riaperto con Ctrl+O nello stesso processo Live.

Al richiamo sono stati osservati nella copia MIX 49,4%, Amount Transient 64,4%, shape Transient 8, BAND attivo con FOCUS mostrato a 3,4 kHz, trigger AUDIO e sei motori ON. Passando ai relativi settori sono stati ritrovati il disegno FREE DRAW Groove di 193 punti e i punti personalizzati Classic con PHASE +16,1%.

Questa prova attesta richiamo del set e separazione delle istanze nel processo Live usato. Non viene descritta come un riavvio completo di Windows o come una nuova sessione Live avviata da zero dopo l’uscita regolare; il precedente recupero dopo l’asserzione resta un evento distinto.

Durante la registrazione F9 è stato modificato il nuovo parametro `curveAmount5` da 100% a 64,4%. L’[audit strutturato del file ALS](../work/Ableton-QA-v0.2.0/AUTOMATION-AUDIT.json), prodotto dallo [script in sola lettura](../tools/audit_ableton_automation.py), collega esattamente l’AutomationTarget della lane al parametro VST3 JUCE `1360475886` del nuovo `curveAmount5`. Sono presenti un evento iniziale sentinella e due eventi non negativi allo stesso istante PPQ 122,164825, con valori 100% e circa 64,4444%: una transizione registrata, non una rampa continua inventata. Il dato non è confuso con il vecchio `amount` già presente nel progetto della 0.1.0.

Nell’istanza moderna originale l’audit identifica otto lane registrate (`curveAmount5`, `trigger`, `on5`, `on1`, `on2`, `on3`, `on4`, `solo1`), con 26 FloatEvent totali, dei quali 18 a tempo non negativo. L’istanza duplicata contiene lo stesso pattern copiato: il totale di 16 lane/52 eventi fra le due istanze non viene presentato come due performance registrate indipendentemente.

Il confronto dei chunk reali del processore nel set finale conferma due differenze fra le istanze moderne: MIX, 80% contro 49,4444466%, e il settore visibile `selected`, Transient nell’originale e Classic nella copia. Gli altri 129 parametri e i sei slot curva risultano uguali. Classic contiene nove punti e PHASE 16,1111145%, Groove 193 punti con FREE DRAW attivo, Transient shape interno 7 (miniatura 8) e Amount 64,4444427%. Le differenze dei valori `Manual` della cache Live per controlli senza binding (`ParameterId=-1`) non vengono scambiate per lo stato autorevole del processore.

L’audit è stato rieseguito sul set definitivamente salvato dopo la chiusura, con SHA256 `CDC92A777F7980F036F72D01A51984EC5DECE56A389D0A61E6B91F41BD5E24EB`, coincidente con quello del POST. Legge gzip/XML senza modificare il set, la GUI, le preferenze o i file di sistema. Sono conservati quattro chunk schema 2 completi di sei slot: due con `modernMask=0` per mantenere il percorso legacy e due con `modernMask=63` per i settori moderni.

## Limiti del collaudo in Live

La visualizzazione delle sette categorie è riuscita; la selezione di una voce nei sottomenu non è stata completata con lo strumento di automazione disponibile, che chiudeva il popup attivando la finestra proprietaria. Le frecce e il passaggio circolare 1/700 sono invece stati esercitati realmente. L’esito della libreria esaustiva resta quello dei test nativi dei 700 preset.

Non sono stati simulati come riusciti i gesti che richiedevano Ctrl mantenuto premuto: la suite nativa verifica Ctrl+drag/rotella, mentre il presente collaudo Live verifica doppio clic, trascinamento dei punti, PHASE, Undo/Redo e FREE DRAW.

Waveform e meter sono stati osservati durante la riproduzione reale del LAB. Non viene dichiarato un ascolto umano, un export audio analizzato o un null test della compensazione host a livello di singolo campione. Le misure DSP automatiche rimangono documentate nel report generale.

## Interruzione durante l’automazione dell’interfaccia

Durante un tentativo automatizzato di impostare il campo Tempo principale di Live a 168, Live si è chiuso. Il log contiene la seguente riga:

```text
2026-10-28T14:24:55.208336: error: ASSERT 'Is<TValueType>()': expression failed in file 'F:\Jenkins\live\products\live\Live\UiFramework\Exp\AxVariantI.h:20'
```

Il campo interessato apparteneva all’interfaccia principale di Live. Questa evidenza documenta un’asserzione del framework di accessibilità/interfaccia di Live durante quella chiamata; **non identifica OpenDuck come causa**. Non è disponibile un’analisi dello stack che dimostri un difetto del plugin o ne escluda ogni possibile coinvolgimento. Il tentativo a 168 non è registrato come una prova riuscita.

Il riavvio ha presentato la richiesta di recupero del set QA. Il log alle 14:30:11–12 mostra il caricamento della copia in `Preferences/BaseFiles` e due ripristini del plugin v0.2.0. Dopo che i tentativi di automazione del dialogo non avevano reso accessibile l’operazione, l’utente ha selezionato **Sì**. È stato poi confermato il recupero dello stesso set TEST LAB 0.2.0, con host a 168 BPM e trasporto fermo, e le prove sono riprese. Il click dell’utente è registrato come intervento reale, non come successo dell’automazione del dialogo.

L’interruzione resta documentata nell’esito finale. Non viene cancellata dai risultati successivi né trasformata in una diagnosi di crash del plugin senza evidenze ulteriori.

## Checkpoint intermedio della configurazione

Il confronto in sola lettura con il baseline, eseguito durante l’interruzione, dà:

| Elemento | Confronto al checkpoint |
|---|---|
| `Preferences.cfg` | Hash diverso: ripristino finale ancora necessario |
| `Library.cfg` | Hash diverso: ripristino finale ancora necessario |
| `Options.txt` | Assente come all’inizio |
| `UsageData.cfg` | Hash uguale al baseline |
| DLL VST3 di sistema | Hash uguale al baseline |

Il confronto intermedio non presume che una differenza dei file di preferenze indichi una modifica a driver o buffer: Live salva anche stato d’interfaccia e selezioni temporanee. Le differenze rilevate in questo checkpoint sono state eliminate dal ripristino conclusivo verificato sotto.

La ripresa è rimasta nel solo TEST LAB. Il lavoro recuperato è stato conservato nel set della revisione, il trasporto fermato e Live chiuso regolarmente prima della verifica finale dei file. Media Player, driver e volume globale non sono stati gestiti dalle operazioni di collaudo.

## Esito finale e ripristino

Il [POST-RUN-REPORT.json](../work/Ableton-QA-v0.2.0/POST-RUN-REPORT.json), raccolto il 28 ottobre 2026 alle 14:11 UTC, registra `postRunChecksPassed=true` e nove controlli veri. Il file ha SHA256 `9340284F711EF80CDEAF92E3E1CF4F649ECD9F73F55BB2ECC5431EA2D7705F33`.

| Controllo conclusivo | Stato |
|---|---|
| Matrice funzionale osservata nel TEST LAB | Esiti riportati sopra; limiti sottomenu/Ctrl dichiarati |
| Set moderno salvato e riaperto senza INIT | Superato nello stesso processo Live, con valori e curve distinti delle due istanze |
| Chiusura regolare di Live e ritorno allo stato iniziale chiuso | Superato: `abletonClosed=true`, `initialProcessStateRestored=true`; nessun processo Live presente |
| Quattro condizioni iniziali delle preferenze | Superato: tre file esistenti ripristinati con hash identico; `Options.txt` assente come all’inizio |
| Integrità delle copie di preferenze iniziali | Superato: snapshot uguali al baseline |
| Bundle VST3 di sistema invariato | Superato: DLL e `moduleinfo.json` coincidono con il baseline |
| DLL Release 0.2.0 invariata | Superato: hash identico a quello del modulo caricato |
| Stato moderno salvato completo | Superato: quattro chunk schema 2, ciascuno con sei slot completi |
| Set TEST LAB originale invariato | Superato: SHA256 identico alla copia iniziale; le prove moderne sono nel set 0.2.0 separato |
| Configurazione audio e sorgenti plugin | Controlli finali dell’interfaccia conformi ai valori iniziali, seguiti dal ripristino esatto dei file |

Gli hash finali delle preferenze coincidono con i rispettivi hash iniziali:

| File | SHA256 finale |
|---|---|
| `Preferences.cfg` | `27FEC4D5DD42EB3E5C0F3DDD89F950ABCD3D6A4F07C67B16D6423C4A4FB0A6D5` |
| `Library.cfg` | `E184B03CF76401C2A01449BC467C3630259311F913E399BFA2571988A7DEB4B8` |
| `UsageData.cfg` | `D44DDEFE2484AD82D33BC7259D8E76DB0CDC92291C7B55A193A254C2C34F586A` |
| `Options.txt` | Assente prima e dopo |

Prima della chiusura, l’interfaccia Audio mostrava gli stessi valori iniziali: MME/DirectX; ingresso `Linea (4- Roland Rubix 24) DX`; uscita `Altoparlanti (4- Roland Rubix 24) DX`; frequenza 44.100 Hz; buffer d’ingresso 8192 e d’uscita 3920; compensazione d’errore 0; Test Tone disattivato. Le sorgenti plugin mostravano VST2 `C:\VSTx64` attivo, cartella VST3 di sistema attiva e cartella VST3 personalizzata ripristinata al valore iniziale `C:\Program Files\Common Files\VST3`, attiva. Non è stata mantenuta la cartella di build come sorgente temporanea.

Il set originale conserva SHA256 `D9819A87D33C18E1E6B855919E803E670BB3932D4339CA2C39A1F3718B5186E4`. Il setup di produzione non è stato eseguito: la consegna dell’installer aggiornato rimane destinata all’esecuzione personale dell’utente.
