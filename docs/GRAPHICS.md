# Grafica nativa — revisione 0.3.0

L'editor usa il telaio Premium cromato approvato, incorporato da `Assets/PremiumFrame.png`. Il master originale resta conservato in `Assets/DesignMaster.png`; non è la superficie interattiva corrente. I diritti dell'artwork restano ai rispettivi titolari.

Il piano di disegno misura 1767×890, con rapporto vincolato. La larghezza iniziale è 1326 px; sono supportate larghezze da 1010 a 1767 px e l'altezza segue il rapporto. La larghezza dell'istanza viene ricordata. La maniglia di ridimensionamento è interna al cromo vicino alla vite inferiore destra: non viene aggiunta una cornice esterna decorativa.

Manopole, valori digitabili, pulsanti, browser, menu, curva e pannello Advanced sono componenti JUCE nativi. Le etichette non sono stampate come sostituto dei controlli. I sette valori primari separano numero e unità e rimangono nei propri pannelli alle tre dimensioni verificate. La grafica metallica delle manopole e gli anelli sono disegnati dal LookAndFeel comune. L'editor usa immagini software per il telaio e il percorso di rendering compatibile documentato nei sorgenti.

La curva bersaglio usa le funzioni di tempo e guadagno condivise con il DSP, comprese le inverse per l'editing. Non è una misura della risposta per frequenza di Spectral. Edit Points e Free Draw sono opzioni esplicite; l'istanza nuova mostra il primo shape continuo, senza nodi. Il preview ha sorgenti Input, Sidechain e Output reali: 512 bucket min/max di un ciclo sincronizzato. Il riferimento di ampiezza è comune, così un'uscita attenuata non viene ingrandita per sembrare invariata. Senza audio non viene generata una waveform. Freeze conserva un frame reale e Zoom ne modifica soltanto la vista.

Il registro di avanzamento vive nel browser locale separato dal VST. Le stime di lavoro e le attività rosse/verdi non diventano meter o indicatori fittizi nel plugin. L'AI espone avanzamento della cattura e del calcolo effettivi, con stati di astensione e proposte applicabili solo dopo il confronto audio.

Le immagini `build/visual-qa-v0.3.0/native-*.png` derivano dal componente nativo. `dashboard/v0.3.0/OpenDuck-Pro-X-concept-0.3.0.jpg` è invece il concept, distinto dalle prove del VST3. Gli esiti finali e le dimensioni effettivamente collaudate sono nel rapporto della revisione.
