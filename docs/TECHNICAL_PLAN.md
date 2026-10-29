# Architettura e ordine di collaudo — 0.3.0

Progetto locale: `E:\Codex\Lavori\OpenDuck Pro X`. Sorgenti, corpus, compilazioni e installer restano nel progetto. La specifica TXT originale è conservata come riferimento storico; le successive istruzioni dell'utente introducono Ducking, 700 preset separati, il telaio Premium e sei assistenze locali.

La build usa C++17, JUCE 8.0.9 fissato al commit documentato, MSVC x64, runtime C++ statico e CMake. Produce soltanto VST3; `COPY_PLUGIN_AFTER_BUILD` è disabilitato. Nessuna build installa il plugin di produzione.

Schema3 elabora sei motori in serie con curve, trigger e parametri indipendenti. Ducking lavora sul guadagno, Spectral usa STFT e maschere della sidechain, Multiband usa crossover a due/tre bande, Morph deforma il recupero, Groove applica i passi e Transient segue il detector. Dry, wet e bypass condividono un riferimento temporale con PDC dichiarato. I vecchi stati schema1/2 mantengono il ramo precedente finché l'utente sceglie l'upgrade esplicito.

APVTS conserva i 131 parametri già pubblicati e aggiunge 282 controlli di settore. Gli stati includono sei curve, diciotto curve di banda, segmenti, geometria dei gruppi e memorie delle sedici forme per ogni settore. I dati vivono nel processore; creare o distruggere l'editor non applica INIT.

Il callback usa buffer preparati, dati atomici e tentativi di lettura non bloccanti delle curve. La cattura AI copia main e sidechain in memoria predisposta prima dell'acquisizione. Estrazione delle caratteristiche, rete e renderer di confronto girano nel worker. Ogni settore conserva modello, proposta e stato propri; Apply/Undo sono espliciti e non modificano gli altri settori. Un contesto non più valido o insufficiente produce astensione. Il plugin non richiede rete, Python o browser.

L'addestramento usa sei reti separate e target scelti dal DSP reale su un corpus sintetico originale. Le famiglie di sintesi sono separate fra train, validation e test. Manifest, sorgenti congelati, dataset, parità numerica e held-out audio distinguono l'esperimento di training dal runtime consegnato. Un miglioramento dell'obiettivo sul frammento non certifica una preferenza musicale universale.

R2 corregge la risalita del guadagno con `floor+(1-floor)*envelope` e la maschera Spectral stereo con FFT separate e combinazione della potenza. Amount delle bande e inversa grafica condividono la stessa legge. I guard di preservazione precedono la scelta del candidato. La Strength interpolata viene renderizzata nuovamente sul worker e Apply usa soltanto quel candidato verificato. Il corpus precedente R1 non e riutilizzato come teacher del DSP corretto.

Ordine dei gate: regressioni di qualita e 35 render musicali; pilota train-only; nuovo corpus e training/reproducibilita; build finale; suite di compatibilità, DSP, qualita, render musicali, UI, AI e bridge; parità e held-out; host VST3 indipendente; Steinberg e pluginval. Il runner finale registra undici gate. Seguono compilazione e upgrade/reinstall/rimozione dell'installer isolato. Solo dopo questi controlli viene eseguita l'unica sessione finale Ableton autorizzata. Prima si conservano configurazione, identita dei processi iniziali e hash; dopo si chiude soltanto il processo QA e si ripristinano esattamente percorsi e preferenze. Un progetto utente gia aperto rimane aperto e non viene salvato o sostituito. Non si modifica il bundle di sistema e non si installa la release di produzione.

I risultati effettivi sono registrati in `docs/VERIFICATION-v0.3.0.md`, nei manifest e nel registro locale. Una compilazione riuscita non sostituisce i test. Sorgenti, licenze e prove selezionate accompagnano l'installer; preferenze, backup privati e log personali del DAW sono esclusi dal pacchetto.
