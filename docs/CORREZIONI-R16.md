# Open Duck — Black Circuit — revisione R16

Correzioni richieste dopo la registrazione del 29 ottobre 2026. La versione
precedente è stata disinstallata manualmente. Questa revisione viene sviluppata
e verificata senza aprire o controllare Ableton e senza installare il plugin
nella cartella VST3 di sistema, come richiesto.

## Motore audio

- ANTI-CLICK protegge anche SYNC: raccordo causale di 2 ms dei riavvii e dei
  salti ripidi del guadagno, senza aggiungere latenza al percorso SYNC.
- Nel nuovo motore, con ANTI-CLICK acceso, il coefficiente di ducking resta
  tra 0 e 1. La protezione agisce sull'inviluppo; non viene applicato un
  hard clipper al segnale audio. Con ANTI-CLICK spento resta disponibile il
  profilo misurato originale, compresi i suoi piccoli superamenti.
- Il detector legge tutti i canali effettivi del bus sidechain: un ingresso
  MAIN mono non esclude più il canale destro di un sidechain stereo.
- Il sidechain resta esplicito. Non viene usato il MAIN come sorgente nascosta.

Il limite del coefficiente riguarda il ducking full band. La ricostruzione
selettiva BAND può avere transitori propri; non viene dichiarata una garanzia
universale sul picco del segnale audio in tale modalità.

## Controlli

- MIX, PHASE e CURVE usano lo stesso drag verticale relativo: 1200 pixel per
  l'intero intervallo e precisione dieci volte maggiore con Shift.
- Il movimento orizzontale non accelera o annulla quello verticale. Cambiare
  Shift durante il gesto non cambia il valore a puntatore fermo.
- Arco LED, lineetta e percentuale continuano a leggere lo stesso parametro.
  Il renderer e la posizione delle manopole sono conservati.
- Il grafico si aggiorna a 60 Hz; il timer non ridisegna più tutto il pannello
  a ogni aggiornamento. MIX non forza il ridisegno delle 16 miniature, che
  rappresentano il profilo completo indipendentemente dalla profondità.
- Il grafico distingue POWER OFF, MIX 0%, bus sidechain non collegato,
  attesa del kick, segnale sotto soglia e guadagno reale. La mini scheda mostra
  livello del sidechain e numero di trigger accettati.

L'apertura iniziale resta a MIX 0% con POWER spento. La selezione AUDIO non
accende automaticamente il plugin e non inventa trigger o attenuazioni.

## Verifica e consegna

I risultati della R15 sono storici. Solo log e ricevute generati sulla nuova
DLL R16 descrivono le verifiche di questa revisione. La prova in Ableton e
l'avvio dell'installer vengono lasciati all'utente; non sono dichiarati
eseguiti su questa revisione.
