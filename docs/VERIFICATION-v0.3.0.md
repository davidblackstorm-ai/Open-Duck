# OpenDuck Pro X 0.3.0 — rapporto di verifica

Generato da evidenze effettive il 2026-10-28 19:29:24 UTC.

**Gate non-DAW e QA installer superati; sessione finale Ableton pendente e autorizzata.**

Il rapporto usa conteggi e hash dei file controllati. Le prove numeriche non costituiscono un voto percettivo né una dichiarazione automatica di ascolto umano o di gesti UI osservati.

## Identità e provenienza

| Artefatto | SHA256 |
| --- | --- |
| DLL VST3 finale | `a22be99e689d64304fcc525c9c4eeae9b793022836bcaca96cf125ccc636f271` |
| Setup di produzione | `856b84535985cb72554083c2946518f05f0b7a4fb8efa0553ec78bf0dfde030a` |
| Manifest non-DAW | `5c2b8593d40c3ba92cb6fcea935d6de965657f18e46e1d1645bf11e375b72b8b` |
| Header dei pesi NeuralModels.h | `039333f56cbf30f346037c007cad246f09a7709ba9e9ecc787a1a55a160e9aa9` |
| Training | `6c0d9d2f71db163d34d24cc27c1fcdd2ed2cb5894e4e76749f329d24ae9a9841` |
| Manifest corpus congelato | `6ef52bac15a3c0564d1b7a8e1c62abe628869208df251bcc02d3d5b178224eed` |
| Golden inference | `371ad5e24cfb623af5910139c3433bb70b2412f5224281b4c2fbc87485d08893` |
| Held-out DSP | `84a26861df6b35a2b338b0b9e219f628eb191d86aba80a9e156a6bcf9c296741` |
| Provenienza DSP/guard/modelli R2 | `d67706933a3e06ff6beeeff6468c9a99ef39d6c3cf43eec8b68fc8c8fd22968b` |
| Misure35 render musicali | `aa645ded49e2cfc1c82314b903d1744b277b04e461291eb8e2c63e5402622867` |
| QA installer | `aebe29bc19e61d70ae25163ca475f7cea68f2a7a504eae248d524b038305f076` |

Il codice corrispondente completo è nell'archivio Sorgenti; i file Source non vengono collegati come se fossero presenti nella sola cartella delle evidenze. Il manifest non-DAW contiene gli hash di Source e tests. Nucleo audio:

| Sorgente | SHA256 |
| --- | --- |
| `Source/PluginProcessor.cpp` | `520ff6ac97f2a5887af33e36cd45e0591d41dafc17d943dea96e67c14d509af2` |
| `Source/PluginProcessor.h` | `b856b8c98ee3876543d667c38566c93c7f6bd176fe4551f67ec28b93a838cac4` |
| `Source/Engines.h` | `cfffd8d764f5e25c820f81f4dee55d3ffab0f98febcae54a1aaf94ccc56191a4` |
| `Source/BehaviorLibrary.h` | `d2a7b842e7608e957258b53201271414c567d276ad8b6387949ec51b99068f25` |
| `Source/AssistTrainingData.h` | `c16905be776da6602ff731847953285a82bc507b179a7ff36f90caad74ce5126` |

## Gate finali effettivi

| Verifica | Esito | Evidenza |
| --- | --- | --- |
| Suite nativa storica | 76 passed, 0 failed; exit 0 | [tests-v0.3.0.log](../build/tests-v0.3.0.log) |
| DSP 0.3 | 51 passed, 0 failed; exit 0 | [dsp-v0.3.0.log](../build/dsp-v0.3.0.log) |
| Regressioni qualità audio | 16 passed, 0 failed; exit 0 | [quality-v0.3.0.log](../build/quality-v0.3.0.log) |
| Render musicali kick/basso | 35 render; gate null, PDC, finitezza e clipping superati; exit0 | [audio-v0.3.0.log](../build/audio-v0.3.0.log) |
| UI nativa 0.3 | 43 passed, 0 failed; exit 0 | [ui-v0.3.0.log](../build/ui-v0.3.0.log) |
| AI capture/contratto | 76 passed, 0 failed; exit 0 | [ai-v0.3.0.log](../build/ai-v0.3.0.log) |
| Bridge AI reale | 179 passed, 0 failed; exit 0 | [bridge-v0.3.0.log](../build/bridge-v0.3.0.log) |
| Parità e held-out | 15 passed, 0 failed; exit 0 | [parity-v0.3.0.log](../build/parity-v0.3.0.log) |
| Contratto host VST3 | 11 passed, 0 failed; exit 0 | [host-contract-v0.3.0.log](../build/host-contract-v0.3.0.log) |
| Validator Steinberg | 47 passed, 0 failed; exit 0 | [steinberg-v0.3.0.log](../build/steinberg-v0.3.0.log) |
| pluginval | Strictness 10, SUCCESS, exit 0; GUI inclusa | [pluginval-v0.3.0.log](../build/pluginval-v0.3.0.log) |
| Steinberg extensive-global | 537 passed, 0 failed; exit0 | [steinberg-extensive-global-v0.3.0.log](../build/steinberg-extensive-global-v0.3.0.log) |
| Steinberg extensive-local | 537 passed, 0 failed; exit0 | [steinberg-extensive-local-v0.3.0.log](../build/steinberg-extensive-local-v0.3.0.log) |
| QA installer | 0.2.0 → 0.3.0, reinstall e uninstall superati; preset preservati; produzione invariata | [installer-qa-results-v0.3.0.json](../build/installer-qa-results-v0.3.0.json) |
| Compilazione setup professionale | Input identici al QA verificati per hash; nessuna installazione di produzione | [installer-production-v0.3.0.json](../build/installer-production-v0.3.0.json) |
| Ableton Live 12 | PENDENTE, autorizzato dopo gli altri gate | Nessuna sessione attestata in modalità pre-DAW |

Nessuna nuova installazione di produzione è stata eseguita dagli strumenti di verifica. Il setup è destinato all'installazione dell'utente. I log sono distribuiti sotto `Logs/`, le evidenze native più pesanti sotto `Preview/`; i documenti del corpus/training sono sotto `docs/research-v0.3.0/`.

## Sei modelli locali: dati e risultati distinti

Sei MLP FP32 96–24–16–43. Per ogni settore: 96 scene di training, 24 di validazione e 24 di test, separate per famiglia di sintesi, senza dividere crop della medesima sorgente fra i gruppi. Il corpus usa 144 scene e 8 candidati per settore, renderizzati dal teacher C++ sul DSP reale. Non usa label ottenuti da un renderer Python sostitutivo.

Fonti: [training.json](research-v0.3.0/training.json), [manifest.json](research-v0.3.0/teacher-final/manifest.json), [golden.csv](research-v0.3.0/golden.csv), [ai-heldout-v0.3.0.json](../build/ai-heldout-v0.3.0.json).

| Settore | Train/val/test | MAE controlli attivi sul test | Accuratezza shape sul test |
| --- | --- | ---: | ---: |
| Ducking | 96/24/24 | 0.14663 | 0.4583333 |
| Spectral | 96/24/24 | 0.159596 | 0.5833333 |
| Multiband | 96/24/24 | 0.1580143 | 0.5833333 |
| Morph | 96/24/24 | 0.1218677 | 0.375 |
| Groove | 96/24/24 | 0.2903818 | 0.5416667 |
| Transient | 96/24/24 | 0.1158571 | 0.4583333 |

MAE e accuratezza shape misurano l'export sui parametri, non il gradimento del suono. Le seguenti prove audio usano il DSP compilato su famiglie held-out; un obiettivo inferiore è migliore secondo il criterio numerico del renderer:

| Settore | Scene audio | Vittorie modello solo | Vittorie modello + refinement | Obiettivo baseline | Obiettivo modello solo | Obiettivo accettato |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ducking | 12 | 2 | 4 | 0.8235369 | 0.4174425 | 0.707221 |
| Spectral | 12 | 2 | 5 | 0.7632426 | 0.6370719 | 0.6298217 |
| Multiband | 12 | 1 | 2 | 0.6177204 | 0.4615256 | 0.5743818 |
| Morph | 12 | 3 | 6 | 0.7267946 | 0.5559163 | 0.5380088 |
| Groove | 12 | 0 | 4 | 0.7482127 | 0.7446286 | 0.7327185 |
| Transient | 12 | 2 | 4 | 0.6219973 | 0.4831396 | 0.4666634 |

Scene held-out con attacchi main non coincidenti misurati: 0/72. La copertura degli attacchi va distinta da quiet, bande e recupero: il controllo dedicato di rifiuto è documentato nella suite [ai-v0.3.0.log](../build/ai-v0.3.0.log); un campo protectedAttacks pari a zero non viene presentato come prova aggiuntiva degli attacchi.

Il risultato accettato include la baseline quando non c'è miglioramento: le astensioni impediscono il peggioramento, ma non sono vittorie del modello. Il refinement è una ricerca limitata attorno all'inferenza; i suoi risultati non vengono attribuiti al solo modello. Nessun punteggio percettivo o confronto di ascolto con plugin commerciali è dedotto da questi numeri.

| Settore | SHA256 float32 dei pesi dichiarato dal training |
| --- | --- |
| Ducking | `9d54cb819a4b5dd83874f0e061c22bbcc83fe9c0caae5ac8b066c56ea88d2c83` |
| Spectral | `5f2d3af962c79a88ca945f6e373d155c226833d05bfb47e6eb640aae489089c3` |
| Multiband | `efe479d092e5b0a5619a354657a33459192e83df6d00b85f87550313a8481936` |
| Morph | `252e96c8bbc0eb74f31f7e25a4ad045f358a4190b42ec592943eda63b7a60d8c` |
| Groove | `9a0f9ab87fde8cc1e63e0315ffb2564df0781cde267a5cd0f34981c482ec6130` |
| Transient | `7f529f8946b203bc97eceafea6a39b750da47e3331b302bf28251506bf10aa81` |

## Provenienza R2 e preservazione audio

Evidenza: [ai-quality-lineage-v0.3.0.json](../build/ai-quality-lineage-v0.3.0.json). Nuovo corpus generato dopo il fix della legge gain e della maschera stereo. DSP, renderer e guard del teacher coincidono byte per byte con i sorgenti di runtime congelati; i sei modelli sono stati riaddestrati con seed e corpus R2. Non si attribuisce al vecchio corpus R1 la correzione del nuovo DSP.

I35 render musicali ([quality-audio-v0.3.0.json](../build/quality-audio-v0.3.0.json)) usano kick e basso separati, poi sommati, ogni settore e la catena completa. I gate verificano assenza di campioni non finiti/clipping, PDC, null senza intervento e recupero nelle finestre aperte. I WAV e i risultati RMS sono conservati senza normalizzazione: la riduzione durante il ducking resta intenzionale e non viene chiamata perdita zero. Non costituiscono una certificazione percettiva.

## Misure audio del build finale

Fonte: [dsp030-measures.json](../build/visual-qa-v0.3.0/dsp030-measures.json).

| Grandezza | Valore misurato |
| --- | ---: |
| PDC preparato a 48 kHz | 12112 campioni = 252.333333 ms |
| Null RMS Amount 0 contro ingresso ritardato | 1.992634e-08 |
| Guadagno Amount 50 su curva chiusa | 0.03162063 |
| Peggior callback della prova di automazione | 3.3588 ms |

| Settore | Residuo wet del plateau chiuso | Attenuazione del rapporto |
| --- | ---: | ---: |
| Ducking | 0 | zero numerico nella prova |
| Spectral | 6.183405e-05 | -84.17545 dB |
| Multiband | 0 | zero numerico nella prova |
| Morph | 0 | zero numerico nella prova |
| Groove | 0 | zero numerico nella prova |
| Transient | 0 | zero numerico nella prova |

La profondità del plateau non descrive ogni curva, frequenza o segnale musicale. Il callback è misurato su 24 blocchi da 257 campioni a 48 kHz, tutti i sei settori e automazione dei crossover/slope: deadline 5,35417 ms; il valore osservato non garantisce il medesimo carico su ogni CPU.

Il PDC fisso è `ceil(sampleRate × 0,215) + 6 × 128 + 1024`: 11.274 campioni/255,646 ms a 44,1 kHz; 12.112/252,333 ms a 48 kHz; 22.432/233,667 ms a 96 kHz. Comprende offset anticipato massimo, lookahead, sei stadi FIR e STFT. Resta anche con lookahead zero, bypass o settori OFF: è una latenza elevata per il monitoring diretto. Le prove bypass/dry sono allineate al PDC, non confrontate con un ingresso non ritardato.

## Ambito funzionale e limiti dichiarati

700 comportamenti totali distribuiti fra i sei settori, sette categorie per settore e 16 shape; il catalogo non equivale a 700 preset per ciascun settore. INIT usa il primo shape, Amount/MIX100 e punti OFF. I punti e il disegno libero sono opzionali. Stato di progetto e riapertura dell'editor restano distinti da una nuova istanza INIT.

Capture AI di 5 o 10 secondi e inferenza/refinement funzionano localmente in C++/CPU, senza Python, download o servizio cloud nel VST. Il DSP supporta i trigger MIDI; l'AI si astiene senza eventi MIDI registrati nella capture. Si astiene anche sul clock Hz/Repeat libero quando la fase non è riproducibile e se il contesto cambia. Le sei assistenze restano isolate; Group deve essere Off per l'applicazione AI.

I progetti schema1/2 mantengono il ramo storico e il suo PDC finché viene scelto l'upgrade esplicito al routing seriale0.3. Il supporto di funzioni ispirate a Kickstart/Duck non dichiara identità dei loro algoritmi, importazione di librerie proprietarie o replica completa di ogni operazione Duck2. Le osservazioni UI/audio del DAW appartengono soltanto al verbale manuale effettivamente disponibile.

## Gate DAW ancora aperto

La singola sessione finale Ableton è autorizzata ma ancora pendente. Non sono attestati avvio, gesti UI, esportazioni, ascolti o ripristino delle preferenze. Dopo il verbale manuale e le prove post-run, rieseguire il generatore senza `--pre-daw`.

I backup privati delle preferenze, i restore snapshot, i log Live e i crash report restano esclusi dalla distribuzione. Gli archivi e il manifesto SHA256 di consegna vengono verificati separatamente dal packaging; questo rapporto non dichiara un'installazione utente già eseguita.
