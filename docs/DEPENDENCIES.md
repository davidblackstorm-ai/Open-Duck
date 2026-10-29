# Dipendenze e licenze

- JUCE 8.0.9: commit `f72bad64d29715216226685810c5196bd0d79d77`, https://github.com/juce-framework/JUCE/tree/8.0.9. Sorgente integrale incluso nel pacchetto; avvisi e licenze originali conservati. Percorso AGPLv3 per questa build locale. Non e stata acquistata una licenza commerciale.
- VST3 SDK usato per il validator: commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`, https://github.com/steinbergmedia/vst3sdk. Non serve un secondo SDK per compilare il plugin: JUCE include gli header necessari. Il validator non viene redistribuito nel setup.
- pluginval 1.0.4: https://github.com/Tracktion/pluginval/releases/tag/v1.0.4. Solo strumento QA, escluso da setup e sorgenti originali.
- Inno Setup 7: https://jrsoftware.org/isinfo.php. Compilatore esterno, non redistribuito. Script completo e asset installer inclusi.
- Python con NumPy e Pillow: solo strumenti per segnali originali, asset e analisi; non richiesto dal VST o dal setup.
- Nessun codice proprietario di altri plugin riutilizzato. La musica e sintetizzata con seed deterministico, priva di campioni di terzi.

## Riferimenti tecnici consultati

- JUCE FFT: https://docs.juce.com/master/classjuce_1_1dsp_1_1FFT.html
- APVTS: https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html
- Posizioni VST3: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html
- Ableton VST: https://help.ableton.com/hc/en-us/articles/209071729-Using-VST-plug-ins-on-Windows
- JUCE Direct2D: https://juce.com/blog/juce-8-feature-overview-direct-2d/
- Segnalazioni primarie su blocchi GUI: https://forum.juce.com/t/host-freezing-when-multiple-plugin-ui-instances-are-open-since-juce-8-0-8/67441
- Identita installazione Inno: https://jrsoftware.org/ishelp/topic_setup_appid.htm

Il master grafico fornito dall'utente non viene rilicenziato: i diritti dell'artwork restano ai rispettivi titolari. Il pacchetto e destinato al lavoro locale commissionato; non e stato pubblicato su servizi esterni.
