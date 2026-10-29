# Verifica OpenDuck Pro X 0.2.0 — 28 ottobre 2026

Report della revisione nativa Windows x64 Release. La grafica Premium è stata approvata dall’utente come riferimento prima dell’integrazione. Il plugin corrente contiene controlli JUCE attivi e la libreria nativa: non è soltanto il precedente concept HTML.

La build Release con runtime statico supera **73 test interni**, **11 verifiche dell’host VST3 indipendente**, **47 controlli Steinberg** e **pluginval strictness 10**. La QA isolata del nuovo installer è superata. I risultati della 0.1.0 rimangono nel [report precedente](VERIFICATION.md) e non vengono attribuiti alla nuova build.

## Stato dei controlli

| Controllo 0.2.0 | Stato al momento di questo report | Evidenza |
|---|---|---|
| Build MSVC x64 Release | Superata | [Build Release](../build/build-v0.2.0.log), [build host](../build/build-host-v0.2.0.log) |
| Suite processore, stato, DSP, editor e rendering | **73 superati, 0 falliti** | [Log della suite](../build/tests-v0.2.0.log) |
| Runtime MSVC statico e dipendenze del DLL | Superato: nessun import MSVCP140/VCRUNTIME | [Dipendenze](../build/dependencies-v0.2.0.log) |
| Validator ufficiale Steinberg sulla 0.2.0 | **47 superati, 0 falliti** sul binario finale | [Log Steinberg](../build/steinberg-v0.2.0.log) |
| pluginval livello 10 sulla 0.2.0 | **SUCCESS, uscita 0** sul binario finale | [Log pluginval](../build/pluginval-v0.2.0.log) |
| Host QA indipendente che carica il bundle VST3 | **11 superati, 0 falliti** | [Log del contratto VST3](../build/host-contract-v0.2.0.log), [sorgente](../tests/Vst3HostContract.cpp) |
| QA installer 0.2.0 in destinazione isolata | Superata: installazione precedente, upgrade, reinstallazione e rimozione | [Risultati JSON](../build/installer-qa-results-v0.2.0.json), [log verifica](../build/installer-verification-v0.2.0.log) |
| Prove effettive Ableton della 0.2.0 | Eseguite nel TEST LAB, con limiti dichiarati | [Report Live](Ableton-QA-v0.2.0.md), [audit automazione e stato](../work/Ableton-QA-v0.2.0/AUTOMATION-AUDIT.json) |
| Chiusura e ripristino della configurazione Live iniziale | **9 controlli conclusivi superati** | [POST-RUN-REPORT.json](../work/Ableton-QA-v0.2.0/POST-RUN-REPORT.json): preferenze esatte, Live chiuso, bundle e set originale invariati |
| Installazione standard della 0.2.0 | Non eseguita dall’agente | L’utente eseguirà personalmente il setup prodotto |

Il setup di produzione non è stato installato: per il collaudo Live è stata caricata la build dalla cartella su E:, con selezione temporanea della sorgente plugin. Dopo il salvataggio del solo set QA moderno e la chiusura regolare, il confronto finale conferma il ripristino esatto delle quattro condizioni iniziali delle preferenze, Live chiuso come all’inizio, copia VST3 di sistema e set originale invariati. Le prove Live effettive rimangono distinte dai test dell’host offline e non vengono estese ad altre macchine.

## Libreria, curve e interazione nativa

- Il catalogo contiene esattamente 700 comportamenti in sette categorie da 100. Nomi e combinazioni effettive di parametri risultano distinti; tutti i preset applicano i valori dichiarati e disattivano EDIT POINTS/FREE DRAW.
- Le 16 curve di base sono finite, limitate a 0–1 e con profili distinti. Il disegno libero conserva una forma a dente di sega, senza convertirla obbligatoriamente in una campana.
- Le frecce native precedente/successivo scorrono tutta la libreria con ritorno circolare; il menu espone le categorie e la selezione diretta.
- Una nuova istanza usa il primo shape con editing disattivato in ciascuno dei sei slot e solo Classic ON. Il comportamento previsto di INIT è Amount 100% nello slot e MIX globale 80%; il valore globale viene pubblicato attraverso la notifica APVTS del parametro.
- La suite aziona componenti ed eventi del vero editor: selezione di settore, drag orizzontale della curva completa, doppio clic per un punto dopo EDIT POINTS, Ctrl + rotella/drag per una gobba e FREE DRAW per un contorno alternato.
- La selezione visiva non cambia ON/OFF degli engine né le altre curve. Con sei engine attivi, cambiare soltanto il settore visibile produce audio identico a livello di bit.
- Due regressioni di layout verificano che aprire DETAILS e poi passare a Groove riservi correttamente l’intestazione espansa: i suoi controlli non trascinano la curva, che rimane trascinabile nell’area sottostante.
- Il rendering produce le sette manopole attive a minimo, medio e massimo al 200%. Questo controllo automatico non sostituisce il giudizio visivo su ogni dimensione di finestra o configurazione DPI.

## Persistenza e compatibilità

Il formato di stato è ora schema 2: parametri APVTS e sei curve indipendenti, inclusi nodi X/Y, preset, flag di editing e stato dell’istanza. La suite verifica salvataggio/ripristino dei sei slot, tre distruzioni e ricreazioni consecutive dell’editor, istanze separate e copie restaurate senza condivisione mutabile della curva.

Sono verificati il limite massimo di 256 punti, sanitizzazione dei punti non finiti/fuori range/duplicati/non ordinati e Undo/Redo del gesto di curva con richiamo del relativo parametro. Uno schema futuro o un conteggio di nodi corrotto viene ignorato senza sostituire parzialmente lo stato valido.

I 71 parametri originali mantengono ordine, identificatori, range e indici delle choice; il vecchio `shape` continua a usare Custom=5. I nuovi parametri sono aggiunti dopo quelli esistenti. Un archivio indipendente della 0.1.0 viene caricato come schema 1; il render conserva il comportamento alla precisione del riferimento PCM a 24 bit, con errore massimo registrato **1,18969e−7**. Il percorso legacy rimane attivo anche dopo un nuovo salvataggio/ripristino. La selezione di un preset nuovo riguarda solo il settore destinatario.

Queste verifiche native sono integrate dal collaudo del set moderno in Live 12.4.7: salvataggio con Ctrl+S e riapertura con Ctrl+O nello stesso processo hanno richiamato istanze con MIX 80% e 49,4%, Amount Transient 64,4%, BAND 3,4 kHz, curve diverse per settore, nove punti Classic con PHASE +16,1% e FREE DRAW Groove di 193 punti. L’audit del file ALS finale conferma otto lane registrate nell’istanza originale, inclusi due eventi `curveAmount5` a tempo non negativo per la transizione 100% → 64,4%, e lo stesso pattern copiato nell’istanza duplicata. I chunk finali differiscono per MIX e settore visibile (`selected`); gli altri 129 parametri e i sei slot curva sono uguali. Il POST rileva quattro chunk schema 2 completi, due sul percorso legacy e due moderni. I dettagli e i limiti sono nel [report Ableton](Ableton-QA-v0.2.0.md).

Durante la prova una chiamata UIA al campo Tempo principale di Live ha preceduto un’asserzione `AxVariantI.h:20` e la chiusura dell’host. Non è una diagnosi di crash del plugin. Dopo il click reale dell’utente su Sì, Live ha recuperato lo stesso set e il collaudo è ripreso. La scelta nei sottomenu e i gesti Ctrl non sono dichiarati superati nell’interfaccia Live; restano verificati dalla suite nativa. Non è stato eseguito un ascolto umano certificato, un export analizzato o un null test PDC dell’host.

## Contratto del binario VST3 in un host indipendente

`OpenDuckHostContract` non collega il codice del processore: carica il bundle compilato da un percorso esplicito tramite il wrapper VST3 JUCE, senza installazione o modifica dei percorsi di scansione. Le **11 verifiche superate** coprono:

- Caricamento del componente e della sua interfaccia completa di parametri, con latenza esposta di 1152 campioni.
- Contesto VST3 reale con BPM 120/180, cambio del ritmo udibile, Play/Stop e seek PPQ.
- Negoziazione del bus stereo sidechain e ducking sul kick effettivamente inviato al bus, senza trigger su sidechain silenziosa.
- Note MIDI inviate attraverso il bus eventi con offset di campione; assenza di trigger quando non arrivano note.
- Ripristino dello stato componente/controller con sei slot diversi in una nuova istanza del binario; tre ricreazioni dell’editor senza applicazione di INIT.
- Otto istanze caricate separatamente, con sei motori attivi e parametri indipendenti.

Si tratta di un host di test offline specifico del progetto, **non di Ableton Live**. Non apre un dispositivo audio e non modifica driver o preferenze. I suoi 11 risultati sono separati dalle prove reali Live descritte nel report dedicato.

## DSP e limiti pratici

La ricostruzione STFT a guadagno unitario mantiene errore massimo **1,49012e−7** e ritardo di 1024 campioni. Le tre bande FIR complementari si ricombinano a guadagno unitario con 128 campioni di ritardo. La latenza totale dichiarata e misurata resta **1152 campioni**; il bypass è trasparente al medesimo ritardo.

Ogni motore viene confrontato con la propria curva custom piatta a 0 e a 1: tutti e sei producono una differenza misurabile, inclusi Morph e Transient. Il nuovo percorso usa curve, fase, quantità e tempi dello slot; Transient conserva detector, soglia, isteresi e hold, applicandoli alla curva scelta. Morph interpola la curva Soft con quella selezionata.

Il test BAND FOCUS a 160 Hz attenua una sinusoide a 80 Hz preservandone una a 6 kHz. Con BAND disattivato, le nuove maschere non modificano il processo spettrale legacy a livello di bit. La selettività riusa la stessa STFT; non dichiara un taglio ideale a 160 Hz. La risoluzione è `sampleRate / 1024`, cioè 46,875 Hz a 48 kHz, con transizione smussata e variazioni del controllo FOCUS smussate su 25 ms.

Le automazioni per slot, preset e fase restano finite a 44,1/48/96 kHz e blocchi 1/17/64/257/1024. Restano superati i controlli di smoothing, budget di attenuazione, Host/Free, seek simulato, MIDI con offset di campione, silenzio sidechain e retrigger di otto kick a 145 BPM. Il massimo salto post-avvio del test anti-click è **0,0376128** rispetto al limite 0,045 del test.

Otto istanze con sei engine attivi completano la prova offline con rapporto tempo di elaborazione/tempo audio **0,394927** su questa macchina. Non è una percentuale CPU di Ableton e non garantisce la stessa prestazione con ogni materiale, frequenza di campionamento o computer.

## Installer, sorgenti e licenze

Il setup di produzione 0.2.0 è compilato. La QA usa un’identità separata e la destinazione `work/installer-qa-v0.2.0`, senza installare la nuova release in Common Files/VST3. Il [risultato strutturato](../build/installer-qa-results-v0.2.0.json) conferma tutti i seguenti controlli:

- Installazione della precedente 0.1.0 isolata e aggiornamento a 0.2.0, seguiti da reinstallazione e disinstallazione riuscite.
- `binaryMatches` e `allPayloadFilesMatch`: i due file del bundle installato corrispondono al payload prodotto.
- `allManagedPayloadRemoved`: la rimozione elimina tutti i file gestiti del bundle.
- `legacyPresetPreserved` e `modernPresetPreserved`: conservazione dei preset dei due formati.
- `productionUntouched`: la copia di sistema precedente rimane intatta.

Sono disponibili i log di [installazione precedente](../build/qa-v0.2.0-install-legacy.log), [aggiornamento](../build/qa-v0.2.0-upgrade.log), [reinstallazione](../build/qa-v0.2.0-reinstall.log) e [rimozione](../build/qa-v0.2.0-uninstall.log). L’utente eseguirà personalmente il setup di produzione.

Il processo di build non installa il plugin. CMake imposta il runtime MSVC statico prima della creazione dei target; il controllo delle dipendenze sul DLL finale conferma l’assenza di import esterni MSVCP140/VCRUNTIME. Nessun VC Redistributable viene installato. Il pacchetto sorgenti deve conservare codice, CMake, test, asset Premium, script installer, JUCE 8.0.9 con avvisi e testo AGPL, oltre ai riferimenti delle dipendenze. Inno Setup e i validatori restano strumenti QA/build esterni. La firma Authenticode non è dichiarata disponibile.

La musica originale del setup e il controllo AUDIO ON/OFF sono conservati dal progetto precedente. La QA automatica silenziosa della 0.2.0 verifica payload, aggiornamento, rimozione e preset; non viene descritta come una nuova prova percettiva della musica o del rendering del wizard. Le precedenti prove interattive restano nel report della 0.1.0. Nessuna musica è integrata nel percorso audio del VST. Il collaudo Live ha usato una selezione temporanea della sorgente plugin, poi ripristinata. Le schermate finali confermano MME/DirectX, Roland Rubix 24, 44.100 Hz, buffer d’ingresso 8192 e d’uscita 3920, compensazione 0 e Test Tone disattivato, come all’inizio. I tre file di preferenze esistenti sono stati ripristinati dopo la chiusura e verificati tramite hash; `Options.txt` rimane assente. Media Player e il volume globale non sono stati gestiti dalle operazioni di collaudo. La [verifica conclusiva](../work/Ableton-QA-v0.2.0/POST-RUN-REPORT.json) ha SHA256 `9340284F711EF80CDEAF92E3E1CF4F649ECD9F73F55BB2ECC5431EA2D7705F33`.

Controllo visivo effettivo: le quattro anteprime native al 200% sono state aperte e controllate. Valori, unità, menu e sedici miniature sono contenuti nei pannelli; le manopole risultano circolari e concentriche a minimo, medio e massimo. Il piccolo difetto di hit-test Groove con DETAILS aperto è stato corretto e coperto da due regressioni.
