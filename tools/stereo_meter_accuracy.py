from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.h';s=f.read_text().replace('return 0;\n    }\n    int getNumPrograms()', 'return (2.0 * SpectralEngine::size + BandEngine::taps) / rate;\n    }\n    int getNumPrograms()')
s=s.replace('std::atomic<float> bpm{120}, inMeter{0}, outMeter{0}, reduction{0};','std::atomic<float> bpm{120}, inMeter{0}, outMeter{0}, reduction{0};\n    std::array<std::atomic<float>,2> inputPeaks{},outputPeaks{};');f.write_text(s)
f=p/'Source/PluginProcessor.cpp';s=f.read_text().replace('float peakIn = 0, peakOut = 0, maxReduction = 0;', 'float peakIn = 0, peakOut = 0, maxReduction = 0;std::array<float,2> channelIn{},channelOut{};')
s=s.replace('input[c] = main.getSample(c, i);','input[c] = main.getSample(c, i);channelIn[(size_t)c]=juce::jmax(channelIn[(size_t)c],std::abs(input[c]));')
s=s.replace('peakOut = juce::jmax(peakOut, std::abs(y));','peakOut = juce::jmax(peakOut, std::abs(y));channelOut[(size_t)c]=juce::jmax(channelOut[(size_t)c],std::abs(y));')
s=s.replace('inMeter = peakIn;', 'for(int c=0;c<2;++c){inputPeaks[(size_t)c]=channelIn[(size_t)c];outputPeaks[(size_t)c]=channelOut[(size_t)c];}\n    inMeter = peakIn;')
f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text();a=s.index('const float levels[] =');b=s.index('g.setColour(juce::Colours::black);',a)
s=s[:a]+'''const float gainReductionDb=-juce::Decibels::gainToDecibels(juce::jmax(0.000001f,1-proc.reduction.load()),-60.0f);
    for(int meter=0;meter<3;++meter)for(int channel=0;channel<2;++channel)for(int k=0;k<22;++k)
    {float level=meter==0?proc.inputPeaks[(size_t)channel].load():proc.outputPeaks[(size_t)channel].load();float db=juce::Decibels::gainToDecibels(level,-60.0f);bool lit=meter==1?k>=22-gainReductionDb*22/60:k<(db+60)*22/60;auto col=meter==0?modeColours[0]:meter==1?juce::Colours::silver:juce::Colours::white;g.setColour(lit?col:juce::Colour(0xff252b2e));g.fillRect(1391+meter*75+channel*18,574-k*10,13,7);}
    '''+s[b:];f.write_text(s)
f=p/'tests/ProcessorTests.cpp';s=f.read_text().replace('block % 20 ? 0 : 100','block % 20 ? 0.0f : 100.0f');f.write_text(s)
