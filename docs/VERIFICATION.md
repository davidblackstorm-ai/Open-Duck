# Verifica OpenDuck Pro X 0.1.0 — 28 ottobre 2026

Build locale Windows x64 Release, VST3 soltanto. Sviluppo e artefatti su `E:\Codex\Lavori\OpenDuck Pro X`. Il setup e un prodotto utilizzabile di versione 0.1.0; questo report descrive prove effettive e relativi limiti, non una garanzia assoluta.

## Risultati automatici

| Controllo | Risultato | Evidenza |
|---|---|---|
| Compilazione MSVC x64 Release | Superata | Logs/build.log |
| Suite processore, DSP, stato e rendering | 36 superati, 0 falliti | Logs/tests.log |
| Validator ufficiale Steinberg sul plugin | 47 superati, 0 falliti | Logs/validator-final.log |
| pluginval 1.0.4, strictness 10 | Successo, uscita 0 | Logs/pluginval-final.log |
| Installer QA: installazione/aggiornamento/rimozione | Superati, uscita 0 | Logs/qa-*-final.log |
| Preset valido conservato dopo upgrade e uninstall | Hash invariato | Logs/installer-qa-results-final.json |
| Installazione standard locale | Uscita 0; hash uguale alla build | Logs/install-final.log |

La suite include INIT Classic-only, roundtrip APVTS/custom/SOLO/selezione, indipendenza istanze, distruzione editor, stato malformato, trasporto e seek simulati, Host/Free, MIDI con offset di campione, sidechain audio, smoothing bypass, automazioni random, blocchi 1/17/64/257/1024 e frequenze 44.1/48/96 kHz.

Misure: ricostruzione STFT errore massimo 1.49012e-7 a guadagno unitario; latenza STFT 1024 e FIR 128, totale misurato 1152 campioni. FIR tre bande complementari con somma unitaria al ritardo previsto. Il test selettivo attenua 1 kHz preservando 12 kHz. Riduzione combinata limitata alla profondita richiesta, senza moltiplicazione indiscriminata dei motori. Il test anti-click a DC con attack minimo misura salto massimo 0.0376128 dopo l'avvio, inferiore al limite 0.045 del test. Otto istanze, tutti sei i motori accesi, rapporto tempo offline/reale circa 0.35 su questa macchina; non equivale alla percentuale CPU riportata da Ableton.

Analisi di un segnale sintetico specifico: energia sopra 20 kHz circa -74.3 dB rispetto all'energia totale, picco 0.20000005, nessun non-finito. Questa misura non certifica assenza universale di aliasing o di artefatti su qualsiasi materiale.

## Prove effettive in Ableton Live 12.4.7

Tutte le prove audio effettuate nel solo `OpenDuck Pro X - TEST LAB Project\OpenDuck Pro X - TEST LAB.als`. Nessuna modifica a driver, Rubix, buffer, sample rate, volume Windows o preferenze globali. Windows Media Player lasciato aperto e intatto. Un recupero preesistente e stato prima conservato in `work\Ableton-recovery-preserved`, inclusa una copia salvata del set recuperato; non e stato sovrascritto il progetto utente.

- Caricamento VST3 dalla posizione standard e latenza mostrata dall'host 1152 campioni / 26.1 ms a 44.1 kHz.
- Cambio BPM 120 -> 145, aggiornamento nel plugin; Host Sync disattivato mostra Free 120 senza cambiare il tempo del set, poi ritorno Host 145.
- Play/Stop e salto del trasporto da misura 69 alla prima misura, senza crash. Prove a precisione di campione di seek/griglia anche nella suite con playhead controllato.
- Routing kick originale -> sidechain della traccia basso; waveform bianca proveniente dal bus reale, meter stereo e riduzione attivi. Corretto un problema di retrigger con detector attack/release separati; regressione di otto kick a 145 BPM superata.
- Importazione clip MIDI originale, routing MIDI To -> traccia basso -> OpenDuck, trigger MIDI selezionato e risposta alle note.
- Editing diretto del nodo custom, Undo ripristina l'inviluppo precedente e Redo ripristina il nodo editato.
- Duplicazione della traccia basso con una seconda istanza; Amount dell'originale 80%, della copia modificato a 60%. Stati distinti verificati nel file del set.
- Automazione Amount registrata sull'istanza duplicata. Il set contiene tre FloatEvent (80% iniziale, transizione a 60%); al seek iniziale l'host richiama 80%, come previsto dall'automazione.
- Chiusura editor, cambio traccia, salvataggio e riavvio completo di Ableton: curva custom, selezione Spectral, ON Classic+Spectral, trigger MIDI e valori richiamati. Nessun reset INIT.
- Chiusura normale ripetuta con log `GlobalExit(): End (0)`. Una precedente build Direct2D si era bloccata in chiusura: e stato adottato il renderer software JUCE, poi verificata la chiusura regolare.

Il valore di latenza esposto all'host e stato osservato e il ritardo DSP misurato. Non e stato eseguito un null test di un export Ableton per certificare la compensazione PDC a livello di singolo campione su ogni configurazione host.

## Grafica

Anteprime native 3344x1882 al 200%, sette manopole a minimo/medio/massimo: cerchi e LED concentrici, gap coerente, indicatore parametrico unico. Eliminato il vecchio segno bianco dalla texture interna, corretti valori sovrapposti Mix/Free BPM e leggibilita Swing. Le miniature usano le stesse equazioni del DSP; custom su otto nodi con Undo/Redo. Telaio e master originali invariati, nessun A/B e nessun grafico di spettro aggiuntivo.

Le immagini `Preview/` sono anteprime da sottoporre alla valutazione artistica dell'utente al ritorno. Non vengono etichettate come PNG definitivo approvato. Con ingresso sidechain basso, anche la waveform visualizzata ha ampiezza bassa: non viene inventata una forma d'onda decorativa.

## Installer

Inno Setup 7, italiano/inglese, benvenuto con logo e screenshot del master, licenza obbligatoria, progressione reale di copia, pagina conclusiva, uninstaller. Setup standard in Common Files/VST3, nessun VST2 o player standalone. Verifica QA isolata con AppId separato; non rimuove il plugin standard. Conservazione verificata con un preset `.odpx` valido e confronto SHA256 prima/dopo aggiornamento e uninstall. Disinstallazione interattiva offre scelta esplicita per eliminare preset, predefinita No; quella silenziosa li conserva.

Musica techno originale sintetizzata di 16 secondi, seed deterministico, picco -18.4 dBFS; AUDIO ON/OFF osservati e chiamata PlaySound riuscita (`success=1`). Nessuna regolazione del volume globale, nessuna musica nel VST. Il setup non forza la chiusura di DAW o Media Player.

L'installer non ha firma Authenticode: non sono disponibili certificati del titolare e non ne sono stati acquistati. Nessun test su altre DAW o altre macchine; build locale, non certificazione commerciale di distribuzione. Licenze e sorgenti corrispondenti inclusi.
