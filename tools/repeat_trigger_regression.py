from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.cpp';s=f.read_text();s=s.replace('f("hold", 0, 500, 30);','f("hold", 0, 500, 30);\n    f("detectorAttack",0.1f,20,1);f("detectorRelease",1,200,20);')
s=s.replace('const int holdSamples =', 'const float detectorAttack=std::exp(-1.0f/(value("detectorAttack")*0.001f*(float)rate)),detectorRelease=std::exp(-1.0f/(value("detectorRelease")*0.001f*(float)rate));\n    const int holdSamples =')
s=s.replace('follower = detector + (detector > follower ? a : r) * (follower - detector);','follower = detector + (detector > follower ? detectorAttack : detectorRelease) * (follower - detector);')
# Parameter text has a bounded precision independent of SliderAttachment refreshes.
a=s.index('auto f =');b=s.index('auto b =',a);old=s[a:b]
new='''auto f = [&](const char *id, float lo, float hi, float initial)
    {
        const juce::String key(id);juce::String unit;
        if(key=="amount"||key=="tension"||key=="shift"||key=="mix"||key=="swing")unit="%";
        if(key=="attack"||key=="release"||key=="hold"||key.startsWith("bandAttack")||key.startsWith("bandRelease")||key.startsWith("detector"))unit=" ms";
        if(key=="threshold"||key=="hysteresis")unit=" dB";
        if(key=="spectralLow"||key=="spectralHigh"||key.startsWith("cross"))unit=" Hz";
        if(key=="freeBpm")unit=" BPM";
        const int decimals=hi<=1?2:1;
        auto attributes=juce::AudioParameterFloatAttributes().withStringFromValueFunction([unit,decimals](float v,int){return juce::String(v,decimals)+unit;}).withValueFromStringFunction([](const juce::String& text){return text.getFloatValue();});
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id,1),id,juce::NormalisableRange<float>(lo,hi),initial,attributes));
    };
    '''
s=s[:a]+new+s[b:];f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text();needle='const char *labels[] = {"CLASSIC", "SPECTRAL", "MULTIBAND", "MORPH", "GROOVE", "TRANSIENT"};';s=s.replace(needle,'g.setColour(juce::Colour(0xff101719));g.fillRoundedRectangle(449,109,812,160,14);\n    '+needle);f.write_text(s)
f=p/'tests/ProcessorTests.cpp';s=f.read_text();pos=s.index('    if (argc > 1)\n    {');s=s[:pos]+'''
    {OpenDuckProcessor repeated;auto layout=repeated.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());repeated.setBusesLayout(layout);set(repeated,"trigger",1);set(repeated,"threshold",-36);repeated.prepareToPlay(44100,512);juce::AudioBuffer<float> bus(4,512);std::array<float,8> peaks{};const double samplesPerBeat=44100.0*60/145;for(int offset=0;offset<(int)(samplesPerBeat*8);offset+=512){bus.clear();for(int i=0;i<512;++i){double phase=std::fmod(offset+i,samplesPerBeat)/44100;float kick=(float)(0.09*std::sin(juce::MathConstants<double>::twoPi*65*phase)*std::exp(-phase/0.085));bus.setSample(2,i,kick);bus.setSample(3,i,kick);}repeated.processBlock(bus,midi);int beat=juce::jmin(7,(int)(offset/samplesPerBeat));peaks[(size_t)beat]=juce::jmax(peaks[(size_t)beat],repeated.engineActivity[0].load());}bool every=true;for(auto peak:peaks)every&=peak>0.15f;check(every,"audio sidechain retriggers on every kick at 145 BPM");}
''' +s[pos:];f.write_text(s)
