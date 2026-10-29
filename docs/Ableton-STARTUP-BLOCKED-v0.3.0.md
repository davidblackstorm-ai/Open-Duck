# Ableton 12: tentativi di avvio e ripristino — 28 ottobre 2026

Il collaudo finale OpenDuck Pro X 0.3.0 dentro Ableton **non è stato eseguito**. Non esistono nuove prove di caricamento del DLL finale, automazione salvata, sei analisi AI nella DAW o esportazioni audio Ableton per questa revisione. I risultati dei validatori e dei collaudi fuori dalla DAW restano separati.

## Tentativi osservati

- Ableton Live 12 Suite 12.4.15: due processi dedicati sono rimasti all'avviso che un'altra istanza della stessa versione era già in esecuzione. Il controllo Windows supportato ha restituito `failed to activate captured window`, anche dopo una nuova selezione e un avvio interattivo. Nessun Set di prova o OpenDuck è stato caricato. Sono stati terminati esclusivamente i processi di avvio QA, verificandone percorso, PID e timestamp; il processo originale è rimasto aperto.
- Ableton Live 12 Beta 12.3b10 già installata: un avvio ha raggiunto il Set vuoto e si è chiuso dopo `Accessibility Requested`, con assert `AXVariantI.h:20`. OpenDuck non era caricato. Il successivo avvio ordinario è rimasto alla domanda di recupero del solo Set vuoto; anche quel dialogo non è risultato attivabile dal controllo supportato. È stato chiuso esclusivamente il processo QA identificato.
- Le note ufficiali Ableton 12.3b13 descrivono una correzione successiva per un crash Windows con NVDA/Narrator nel browser/File Manager. È un precedente pertinente, non la dimostrazione della causa dell'assert osservato. Non sono stati inventati flag, modificati file di licenza o aggirati dialoghi protetti.

## Ripristino verificato

- Quattro preferenze del profilo 12.4.15 ripristinate esattamente, inclusi hash e metadati/assenza iniziale.
- Quattro preferenze del profilo 12.3b10 ripristinate esattamente; nessun processo Beta residuo.
- Solo il processo Ableton originale risulta presente, con PID e istante di avvio iniziali, progetto aperto e non salvato come prima. Non sono stati eseguiti Save, Close o sostituzioni del progetto originale. Finestra riportata massimizzata e poi ridotta a icona; l'ultima osservazione Sky conferma `window is minimized`.
- Percorsi plug-in, driver, frequenza di campionamento, buffer e volume audio non modificati per i tentativi. L'opzione temporanea delle istanze multiple è stata riportata al valore iniziale e le preferenze originali ripristinate.
- Bundle OpenDuck installato invariato; DLL finale del progetto e installer invariati. Installer di produzione mai eseguito.

I backup privati e i report di ripristino sono conservati sotto `work/Ableton-QA-v0.3.0`. Nessun esito DAW PASS è stato creato; la finalizzazione della release resta sospesa al collaudo effettivo.

## Riferimenti

- [Ableton: istanze multiple su Windows](https://help.ableton.com/hc/en-us/articles/4409675284882-Allow-multiple-instances-Windows)
- [Ableton: note Beta, inclusa 12.3b13](https://www.ableton.com/en/release-notes/live-12-beta/)
- [Ableton: panoramica accessibilità](https://help.ableton.com/hc/en-us/articles/11550373507868-Accessibility-in-Live-Overview)
