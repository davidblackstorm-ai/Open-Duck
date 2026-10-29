# OpenDuck Pro X 0.3.0 — copertura reale del nucleo audio congelato

Documento di verifica del 28 ottobre 2026. Il nucleo DSP qui descritto è congelato per la generazione del corpus e l'addestramento. Non certifica ancora il pacchetto finale con modelli addestrati, installer o sessione Ableton conclusa. Distingue le funzioni implementate dalle prove effettivamente eseguite e dalle differenze rispetto ai prodotti di riferimento.

## Risultati e provenienza

| Verifica sul progetto E: | Esito osservato | Portata |
|---|---|---|
| `build/dsp-freeze-gate-v0.3.0.log` | **51 passed, 0 failed** | Esecuzione del DSP reale, segnali sintetici, misure e stato; elenco completo in fondo. |
| `build/ui-freeze-v0.3.0.log` | **38 passed, 0 failed** | Eventi del componente nativo, binding, punti, manopole, layout e riapertura editor. Alcuni check sono scrittura screenshot, non giudizi estetici automatici. |
| `build/host-first-v0.3.0.log` | **11 passed, 0 failed** | Bundle VST3 effettivamente caricato: PDC, tempo/trasporto, sidechain, MIDI, stato, riapertura e istanze indipendenti. |
| `build/ai-contract-development-v0.3.0.log` | **40 passed, 0 failed** | Contratto capture/astensione, eseguito esplicitamente con `--allow-untrained`: non è una prova della qualità dei modelli finali. |
| `build/steinberg-pre-ai-v0.3.0.log` | **47 tests passed, 0 tests failed; exit 0** | Validator Steinberg sul bundle esplicito; preliminare pre-AI. |
| `build/pluginval-pre-ai-v0.3.0.log` | **Strictness level 10; SUCCESS; exit 0** | Nessun salto dei test GUI. Processing a 44,1/48/96 kHz e blocchi 64/128/256/512/1024. Preliminare pre-AI. |

Steinberg e pluginval sono stati eseguiti il 28 ottobre 2026, dalle 16:03:45 UTC; metadati e hash si trovano nei corrispondenti file `.metadata.json`. Entrambi hanno verificato lo stesso binario, rimasto invariato durante l'esecuzione:

`1d55d87a8d4fc2d463ef2b2e581bca9b08813262bbaa65dc0b326248f2a844d1`

Bundle verificato: `E:\Codex\Lavori\OpenDuck Pro X\build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3`. I validator dovranno essere ripetuti sul binario finale che incorpora i modelli. Il validator Steinberg e [pluginval](https://github.com/Tracktion/pluginval) verificano il contratto del plugin; non sostituiscono un collaudo musicale nel DAW.

La suite storica `build/legacy-second-v0.3.0.log` registra ancora **75 passed, 1 failed**, sull'asserzione UI Advanced → Groove. Il nuovo percorso UI ha 38 check verdi, ma questo documento non presenta la suite storica come completamente superata: serve il rerun finale dopo l'allineamento della sua aspettativa grafica, oppure la correzione se ancora riproducibile. Non è stato modificato il DSP per far sparire un risultato.

## Misure audio riproducibili

Fonte: `build/visual-qa-v0.3.0/dsp030-measures.json`, prodotto dall'eseguibile `Revision030DspTests`; non sono valori stimati dal disegno dell'interfaccia.

| Grandezza | Misura | Condizioni e interpretazione |
|---|---:|---|
| Latenza dichiarata e allineata | **12.112 campioni a 48 kHz** | 252,333 ms; verificata anche tramite wrapper VST3. |
| Amount 0: errore RMS rispetto all'ingresso ritardato | **1,9926337 × 10⁻⁸** | Tono reale attraverso il processor; tolleranza della prova 3 × 10⁻⁶. |
| Amount 50: guadagno misurato | **0,03162063**, circa **−30 dB** | Curva piatta a zero, MIX 100; scala di profondità in dB, non interpolazione lineare a −6 dB. |
| Residuo wet Ducking / Spectral / Multiband / Morph / Groove / Transient | **0 / 0,000061834 / 0 / 0 / 0 / 0** | Curva piatta a zero, Amount/MIX 100, plateau sostenuto dopo warmup; Spectral circa −84,18 dB. Ogni settore supera il criterio di −60 dB nelle condizioni della prova. Zero è il risultato numerico della prova, non una promessa di cancellazione universale per ogni curva o frequenza. |
| Peggior callback osservato nel test di automazione | **2,6167 ms** | 24 blocchi da 257 campioni a 48 kHz, tutti i sei settori attivi, crossover e slope aggiornati per settore; deadline 5,35417 ms. Non è una garanzia per ogni CPU, dimensione di blocco o carico del DAW. |
| Anticipo Lookahead 10 ms | **480 campioni** | Note MIDI con posizione nota, 48 kHz; differenza misurata fra onset, tolleranza 2 campioni; PDC costante. |

MIX 80 conserva matematicamente il 20% dry: quando il wet si chiude, rimane circa −13,98 dB rispetto all'ingresso. La profondità completa richiede MIX 100. L'Amount descrive la profondità del settore; MIX descrive il bilanciamento dry/wet dell'intera catena.

## Matrice funzionale e criteri di prova

Le diciture **misurato** e **testato** si riferiscono ai log sopra; **implementato** indica codice concreto, senza pretendere una prova audio separata di ogni combinazione.

| Area | Comportamento effettivo 0.3.0 | Evidenza e limiti |
|---|---|---|
| INIT | Ducking selezionato e unico settore ON; primo shape in alto a sinistra; Amount 100, MIX 100; Edit Points e Free Draw OFF. Profondità iniziale Spectral/Groove/bande senza vecchi cap 75/65/35%. | DSP check 1–2; UI INIT. |
| Routing e indipendenza | Sei settori in serie, ognuno con curva, clock, parametri e stato propri. Selezionare il settore non abilita né modifica gli altri. Gli altri rami restano indipendenti anche con più istanze. | Chiusura misurata separatamente per i sei settori; UI cambio settore; wrapper otto istanze. Non equivale al precedente budget MAX. |
| Legge di profondità | Fino a 60 dB per settore; endpoint chiuso a zero. Amount 0, bypass e MIX 0 mantengono l'ingresso allineato. | Null test, bypass/PDC, MIX 0/80 e Amount 50; plateau di tutti i settori. |
| Ducking | Modulazione full range; facoltativamente 2 o 3 bande. Il segnale stereo del detector usa energia e non una somma che annulla L = −R. | Chiusura, null, sidechain in opposizione di fase, multibanda e wrapper SC/MIDI. |
| Spectral | STFT con selettività spettrale della sidechain, intervallo di frequenze e gain mask per banda. Il ramo 0.3 non applica il vecchio cap 0,95. Sidechain e programma vengono analizzati sulla medesima finestra fisica ritardata. | Plateau ≥60 dB e prova 1/6 kHz che respinge la sidechain di un futuro ciclo da 215 ms. Il guadagno per frequenza dipende dal contenuto, non soltanto dalla curva bersaglio. |
| Multiband | 3 curve indipendenti oppure curve linked, profondità e solo per banda; controllo master combinato con Attack/Release/Phase propri della banda. | Prove su componenti a bassa/alta frequenza e differenza audio dei tempi/phase di banda. Non è stato misurato ogni possibile assetto di crossover e ogni curva. |
| Morph | Character e morph cambiano la curvatura del recupero mantenendo gli endpoint; la combinazione non riapre una curva completamente chiusa. | Plateau Morph supera −60 dB; inverse preview verificato anche in Morph. |
| Groove | Clock proprio, shape, step e swing; profondità iniziale completa. | Plateau, inverse con Swing e UI cambio settore. Non è documentato come sequencer MIDI in uscita. |
| Transient | Gate legato al detector o trigger accettato; viene ritardato con la stessa timeline del programma. | Prova audio con kick breve da 4 ms; il gate non finisce 250 ms prima del segnale che deve trattare. |
| Attack / Release | Deformano il tempo della curva, in modo visibile nel preview e udibile. Non cambiano Speed. Mapping invertibile sull'intero ciclo, compreso il tratto dopo il recupero. | Differenze audio Multiband, clock invariato a 1/4, inversione 1.001 coordinate e UI punto tardivo senza reset dei valori. |
| Tension / Character / Smoothing | Tension agisce anche sulle curve custom. Character è una trasformazione della curvatura che conserva gli endpoint. Smoothing separato è un filtro del guadagno 0–30 ms. | Tension misurato nell'audio; Character incluso nell'inversa del preview. Non c'è un benchmark separato di ogni valore di Smoothing. |
| Phase, Length e Rise Slope | Phase fine ±50%, raccordata sul percorso ciclico più breve; Length/Rise Slope deformano il tempo mantenendo un mapping invertibile. I 16 profili ricordano le proprie trasformazioni. | Inverse completa, memoria del profilo e UI FINE. Il preview rappresenta il target a regime; il breve transitorio di smoothing non è un nuovo nodo della curva. |
| Punti e disegno | Punti opzionali, massimo 256; doppio click, spostamento XY, campana con Ctrl, Free Draw che conserva il percorso del mouse e Ramp distinta. Sei tipi di segmento, curvatura e inversione persistenti. | UI eventi nativi Ctrl/Free Draw/Ramp; stato schema3 include geometria. Il limite è 256 nodi, non un numero illimitato. |
| Gain preview condiviso | La legge del guadagno/tempo è comune al DSP e al grafico; coordinate inverse includono Character, Morph, Swing, Offset, Lookahead e timewarp. | DSP inverse completa in sei settori; UI punti e controlli indipendenti per banda. Non implica che la forma d'onda d'uscita sia identica alla curva: è audio filtrato e dipende dal contenuto. |
| Speed e Snap | 20 durate musicali oppure Hz 0,05–40; Snap ha 7 divisioni 1/2/4/8/16/32/64 e influenza l'editing, non il clock audio. | Speed invariato al cambio Release, Free Hz immune a PPQ seek/BPM, UI Snap separato. Frazioni arbitrarie come 5/16 non sono implementate come griglia custom. |
| Trasporto | Repeat segue Play/Stop; lo Stop rilascia a unity. Run while stopped è un'opzione esplicita. Sync usa PPQ; Hz libero non si aggancia silenziosamente al DAW. | Tre prove DSP trasporto/Hz; wrapper BPM, Stop e seek. |
| Trigger | Repeat, Sidechain, MIDI e Input separati; One shot / Loop / Sustain, marker End, End Hold e intervallo Loop. | MIDI/SC e Sustain NoteOff misurati; Loop/End sono implementati, ma non tutte le combinazioni di marker hanno una misura audio dedicata nei 51 check. |
| Detector | HP/LP, soglia, isteresi, holdoff, Attack/Release detector e ascolto del detector; segnale stereo a energia. | SC, kick corto e L = −R misurati. Non esiste in questi 51 check una scansione esaustiva della risposta HP/LP o dei tempi di isteresi/holdoff. |
| MIDI DSP | Filtro canale, note e velocity; velocity depth continua; sample offset NoteOn/NoteOff; Sustain rilascia sulla timeline ritardata. SysEx/testo non creano copie di payload nel ramo audio 0.3. | Prove nota36/42, canale, tre profondità velocity, SysEx lungo e NoteOff. Nessuna pretesa di automazione MIDI in uscita. |
| Lookahead / Offset | Anticipo 0–15 ms, offset ±200 ms; buffer causali e PDC fisso. | Lookahead 480 campioni, inverse preview, wrapper PDC e tail. Vedi latenza dichiarata sotto. |
| Bande e solo | 2 bande: Low e High coprono tutto sotto/sopra CrossLow. 3 bande: Low/Mid/High. In 2 bande MID e CrossHigh sono disabilitati nel pannello; High Solo include anche la zona sotto CrossHigh. | Test bass/high, High Solo e gating UI 2/3 bande. |
| Crossover / fase | FIR lineare 257 tap oppure implementazione minimum phase; slope nominali 12/24/48 dB/oct. Cutoff ordinati e limitati secondo Nyquist, anche a 8 kHz. Tabelle coseno FIR preparate prima dell'audio callback. | 8 kHz finito con cutoff estremi; deadline di automazione sei settori. Le pendenze FIR finite/windowed sono approssimazioni del target, non un brickwall o una pendenza ideale a ogni frequenza. |
| Scope | 512 bucket min/max di Main, SC e Out con origine di ciclo comune da PPQ/trigger. Freeze conserva un frame completato; zoom legge lo stesso frame senza cambiare il DSP. | Test kick4ms singolo ciclo, freeze e UI riferimento comune senza rinormalizzare l'uscita attenuata. Il primo frame richiede un ciclo reale completo; non viene inventata una forma d'onda. |
| Meter | IN/OUT e DUCK riferiti a segnale reale; DUCK confronta RMS allineati. `ScopeFrame.gain` è rapporto globale dei picchi Out/Main; attività Spectral deriva dai gain spettrali effettivi. | Implementazione verificata per lettura codice; non c'è nei 51 check un confronto numerico completo di ogni LED. Il gain globale non è una misura della funzione di trasferimento di ogni frequenza/banda. |
| Libreria | **700 preset totali**: Ducking 120, Spectral 120, Multiband 120, Morph 120, Groove 100, Transient 120; 7 categorie per settore. Cataloghi filtrati, frecce con wrap e menu a tendina. 16 shape di fabbrica. | Catalogo con nomi unici e distribuzione testati; UI scorrimento preserva MIX/trigger. Non sono 700 per ognuno dei sei settori. |
| Trigger LOCK | Il browsing preserva MIX, trigger, routing e ON/SOLO; LOCK mantiene anche Speed. Senza LOCK lo Speed del comportamento può essere applicato. | Confronto LOCK/unlocked su un preset concreto e UI frecce. |
| Gruppi | Off/A–D, opt-in per shape del medesimo settore; controlli, phase e routing locali restano propri. Off riporta la shape locale. Dati condivisi fra istanze nello stesso processo host. | Due istanze: ricezione geometria, phase conservata e ritorno Off. Non è sincronizzazione cross-process fra sandbox separati del DAW. |
| Operazioni | Mirror verticale/orizzontale, clear, rotate, quantise, double/halve curve, ramp/step/gate, randomise e reset delle trasformazioni; undo per azioni curve e banda. | Doppio mirror testato; UI Clear collegato; altre operazioni hanno codice concreto ma non ciascuna una prova audio indipendente nei 51 check. |
| Trace e user shape | Trace di un ciclo SC completato o audio realmente fornito; RMS inverso in 193 punti, rifiuto del silenzio, opzionale trace per banda; salvataggio/caricamento XML proprio con validazione. | Rifiuto silenzio e audio sintetico reale →193 punti custom. Trace normalizza il ciclo nel dominio raw; Attack/Release/Phase restano trasformazioni successive, quindi non promette una copia sample-for-sample dell'ampiezza del file. Non importa formati proprietari commerciali. |
| Persistenza | Schema3 contiene parametri di settore, curve/bande, segmenti, gruppi/local shape e memorie dei profili. Riapertura editor non applica INIT. | Roundtrip DSP schema3, UI reopen, wrapper component/controller e tre riaperture editor. La prima apertura di una nuova istanza resta INIT. |
| Compatibilità | ID/range/ordine storici e cardinalità delle choice preservati; solo etichetta Classic → Ducking. Schema1/2 resta nei rami audio originali e PDC 1.152 campioni finché si sceglie l'upgrade esplicito. | Test schema2/parametri senza upgrade e upgrade dichiarato; UI legacy e wrapper stato. L'intera suite storica va comunque rieseguita sul binario finale. |

## Latenza, sample rate e real-time

La latenza del ramo 0.3 è fissa per sample rate:

`ceil(sampleRate × 0,215) + 6 × 128 + 1024` campioni.

| Sample rate | PDC | Tempo |
|---:|---:|---:|
| 8.000 Hz | 3.512 campioni | 439,000 ms |
| 44.100 Hz | 11.274 campioni | 255,646 ms |
| 48.000 Hz | 12.112 campioni | 252,333 ms |
| 96.000 Hz | 22.432 campioni | 233,667 ms |

Il budget causale copre offset anticipato massimo 200 ms più lookahead 15 ms, sei stadi FIR allineati a 128 campioni e STFT da 1.024 campioni. Resta presente anche con lookahead zero, bypass o settori OFF. **Questa versione non è a latenza zero e ha una latenza elevata per il monitoring dal vivo.** Il buffer di storia supporta anche l'offset negativo; la sua capacità non aggiunge un secondo PDC. Il PDC non viene rinegoziato dentro `processBlock`.

Il tail dichiarato include PDC, STFT e margine FIR; il test dell'impulso ritardato verifica che la coda osservata rientri nel valore dichiarato. Il PDC preparato di 12.112 campioni è verificato dal wrapper reale. La sezione iniziale “Plugin info” di pluginval, prima dei test di processing, stampa invece latenza 0: non viene usata come prova della latenza operativa.

I test DSP mirati sono principalmente a 48 kHz con blocchi 257, con una prova di robustezza a 8 kHz. Il validator esterno aggiunge 44,1/48/96 kHz e blocchi 64–1.024. La tabella è la formula dichiarata, non una misura indipendente dell'impulso per ogni sample rate. L'analisi delle allocazioni di ogni callback e un benchmark esteso a molte CPU non sono attestati da questi log. Il parser R3 evita per costruzione le copie MidiMessage dei payload SysEx; il relativo check verifica equivalenza audio con/senza SysEx, non intercetta globalmente tutte le chiamate a `new`.

## AI locale: distinzione dal supporto MIDI audio

Il DSP tratta realmente i trigger MIDI: le prove NoteOn, filtri, velocity e Sustain NoteOff sono verdi. La capture AI corrente non registra un flusso di eventi MIDI utilizzabile dal renderer offline. Di conseguenza **l'AI si astiene esplicitamente nel contesto MIDI**; non genera note fittizie e non confronta un render silenzioso come se fosse valido. Si astiene anche se una fase Repeat libera non è riproducibile oppure il tempo cambia durante la capture.

Il contratto capture/renderer ha 40 check verdi eseguiti con `--allow-untrained`, modelli non ancora pronti. Corpus 144 × 8 × 6 e training sono in corso al congelamento di questo documento: qualità, disponibilità di sei modelli, parità runtime/teacher e vantaggio rispetto al baseline non sono qui dichiarati superati. Vanno riportati con i risultati finali del proprietario AI. L'applicazione AI è per settore; Group deve essere Off e non viene disattivato silenziosamente per applicare una proposta.

## Riferimenti funzionali e differenze dichiarate

I prodotti esterni definiscono riferimenti di comportamento, non identità del codice o una certificazione dei loro produttori.

- [Kickstart 2, manuale ufficiale](https://downloads.cableguys.com/Nicky-Romero-Kickstart-2-Manual.pdf?v=2.0.8): confronto per browsing shape, shift e mix; OpenDuck mantiene un editor proprio con punti opt-in e sei settori.
- [Duck 1, manuale ufficiale](https://deviousmachines.com/manuals/Duck%20Manual.pdf) e [novità Duck 1.3](https://deviousmachines.com/2022/04/19/heres-whats-new-in-duck-1-3/): riferimento per trigger, gruppi e ascolto delle bande; implementazione e prove OpenDuck sono quelle della matrice, non una promessa di compatibilità preset commerciale.
- [Duck 2, editing delle envelope](https://deviousmachines.com/manuals/duck2/html/editing-envelopes.html), [multibanda](https://deviousmachines.com/manuals/duck2/html/multiband-processing.html), [trigger](https://deviousmachines.com/manuals/duck2/html/trigger-modes.html), [offset](https://deviousmachines.com/manuals/duck2/html/offset-control.html) e [gruppi](https://deviousmachines.com/manuals/duck2/html/grouping.html): confronto per editor, segmenti e controlli; i nomi simili non rendono identici algoritmi, filtri o interpolazioni.
- [Duck 2, toolbar ufficiale](https://deviousmachines.com/manuals/duck2/html/editor-toolbar.html): OpenDuck dispone di double/halve **della curva entro il ciclo corrente**, non dell'identica funzione commerciale che cambia la lunghezza del ciclo e tutte le bande. Dispone di un randomise proprio, non dei cinque generatori commerciali distinti. Snap offre sette divisioni predefinite, senza frazioni arbitrarie. La presenza di ramp/step/gate non certifica tutte le modalità di stamp/painting del prodotto di riferimento.
- [Duck 2, Trace](https://deviousmachines.com/manuals/duck2/html/trace-envelope.html) e [libreria](https://deviousmachines.com/manuals/duck2/html/shape-library.html): OpenDuck usa capture/audio reali e un proprio formato XML. Non dichiara importazione di librerie proprietarie o identità numerica del Trace.

Queste differenze e i limiti di prova impediscono di descrivere la release come replica completa di ogni funzione Duck 2. Sono disponibili funzioni native concrete; l'equivalenza commerciale totale non è stata dimostrata.

## Hash del nucleo congelato

Hash SHA256 del contenuto sorgente verificati anche sulla copia E: il 28 ottobre 2026. I file non sono stati modificati durante l'esecuzione dei validator preliminari.

| File in `Source/` | SHA256 |
|---|---|
| `PluginProcessor.cpp` | `F8384E45ABEC2C0F25C42A28C1569DA60DC075A294F1C825C95EEBC7CCFA4F4B` |
| `PluginProcessor.h` | `ED738226CB2F4AE518BCCF1919748DED788558F5EBBD6934D1ADB33E9D1E64BA` |
| `Engines.h` | `94A6555AE2C49CB1ACD3BC9D13DBA7009AEE5014573083572E9DB188F08D8B40` |
| `BehaviorLibrary.h` | `D2A7B842E7608E957258B53201271414C567D276AD8B6387949EC51B99068F25` |

Il bridge AI e le risorse dei modelli appartengono alla successiva integrazione: i loro hash finali vanno aggiunti dal packaging. Non occorre cambiare questi file per aggiornare il presente documento.

## Elenco esatto dei 51 check DSP superati

Nomi originali dell'ultimo log `dsp-freeze-gate-v0.3.0.log`, nell'ordine di esecuzione. Il nome di un check non estende la sua copertura oltre gli input e le soglie effettive del test.

1. new INIT: MIX100 Amount100 first shape with editing opt-in
2. new INIT has no hidden75/65/35-percent engine-depth caps
3. 700 unique sector behaviors, six disjoint catalogs and seven categories each
4. timewarp is invertible across the whole cycle, including points after recovery
5. Amount0 nulls against the latency-aligned input through the real DSP
6. bypass preserves input with the declared PDC
7. MIX0 is the exact delayed dry path
8. reported revision3 tail contains the measured delayed impulse and FIR/STFT ringing
9. sector 0 closes a sustained wet plateau by at least60dB
10. sector 1 closes a sustained wet plateau by at least60dB
11. sector 2 closes a sustained wet plateau by at least60dB
12. sector 3 closes a sustained wet plateau by at least60dB
13. sector 4 closes a sustained wet plateau by at least60dB
14. sector 5 closes a sustained wet plateau by at least60dB
15. MIX80 retains the mathematically required 20-percent dry floor
16. Amount50 produces measured minus30dB rather than linear minus6dB
17. Spectral follows the sidechain frequency from the same program cycle instead of a future215ms window
18. Multiband lower Attack measurably changes audio with BAND off
19. Multiband lower Release changes recovery without being the cycle clock
20. Speed remains one quarter-note when Release changes
21. Tension changes custom-curve audio, not only stored parameters
22. opposite-polarity stereo sidechain still triggers the real ducking path
23. MIDI note range accepts kick36 and rejects note42 sample-accurately
24. MIDI channel filter rejects an excluded channel
25. long SysEx is ignored without changing accepted short-note audio
26. lookahead10ms actually advances ducking by480samples while PDC stays fixed
27. Transient gate follows a short kick through the same delayed timeline as the program
28. velocity depth is a continuous modulation, with audible intermediate strength
29. Sustain NoteOff releases on the delayed program timeline, then returns to unity
30. scope freeze retains a real completed cycle without subsequent overwrite
31. single-cycle scope contains one4mskick, not the previous rolling two-second history
32. Repeat responds to real host Stop by releasing to unity
33. optional Run while stopped continues the cycle independently of host playback
34. Free Hz ignores host PPQ seek and BPM changes instead of silently phase-locking
35. two-band Low100 High0 attenuates bass while retaining high-frequency program
36. two-band High Solo retains the whole region above CrossLow, including frequencies below CrossHigh
37. three independent Multiband curves act on their actual frequency bands
38. independent band Attack/Release/Phase remain effective alongside master controls
39. trigger-settings LOCK preserves Speed during browsing; unlocked browsing updates it
40. full preview coordinate inverse includes Character Morph Swing Offset Lookahead and timewarp
41. 8kHz host with extreme crossover controls remains finite and within Nyquist
42. six-sector crossover and slope automation meets the actual257-sample callback deadline
43. Group shares only same-sector shape while keeping recipient phase
44. Group Off restores the original local shape and independent controls
45. schema3 restores per-sector controls, three band curves and segment geometry
46. schema2 and legacy control edits retain the original engine and PDC until explicit upgrade
47. explicit audio-engine upgrade activates serial0.3 routing and its declared PDC
48. each factory shape remembers its own Shift Length Tension and Slope transformations
49. two vertical mirror operations preserve the original audible shape
50. Trace rejects silent input instead of inventing a curve
51. Trace builds an inverse envelope from actual supplied audio

## Gate ancora necessari per la consegna

Rerun completo delle suite dopo integrazione dei modelli, qualità/parità e contratti AI con modelli effettivi, validator sul DLL finale, QA installer isolato e verifica degli hash del pacchetto. La singola sessione finale Ableton è autorizzata dopo gli altri gate e richiede il ripristino esatto delle impostazioni iniziali. Questo documento non afferma che quella sessione o l'installazione utente siano già avvenute.
