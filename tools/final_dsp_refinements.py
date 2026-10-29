from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.h';s=f.read_text().replace('static float shape(float phase,int type,float tension);','static float shape(float phase,int type,float tension);\n static float customShape(float phase,const std::array<float,8>& points,float tension);').replace('int delayPos=0,waveCounter=0;','int delayPos=0,waveCounter=0,sideSilence=0;');f.write_text(s)
f=p/'Source/PluginProcessor.cpp';s=f.read_text();pos=s.index('void OpenDuckProcessor::processBlock');s=s[:pos]+'''float OpenDuckProcessor::customShape(float t,const std::array<float,8>& points,float tension){float x=juce::jlimit(0.0f,1.0f,t)*7;int k=juce::jmin(6,(int)x);float f=std::pow(x-k,std::pow(2.0f,tension/50));return juce::jmap(f,points[(size_t)k],points[(size_t)(k+1)]);}
''' +s[pos:]
a=s.index(' auto custom=[&](float t)');b=s.index('\n float peakIn',a);s=s[:a]+' auto custom=[&](float t){return customShape(t,points,tension);};'+s[b:]
s=s.replace('sidechainPresent=hasSc;','').replace('const float level=std::abs(side);','const float level=std::abs(side);')
s=s.replace('  float inputLevel=0;','  if(std::abs(side)>1e-6f)sideSilence=0;else sideSilence=juce::jmin(sideSilence+1,(int)rate);\n  float inputLevel=0;')
s=s.replace(' freeCycle=cycleIndex;inMeter=peakIn;',' sidechainPresent=hasSc&&sideSilence<(int)(rate*0.5);freeCycle=cycleIndex;inMeter=peakIn;')
s=s.replace('waveCounter=0;wavePeak=0;morphSmooth.reset','waveCounter=0;wavePeak=0;sideSilence=(int)rate;morphSmooth.reset')
f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('"NO SIDECHAIN"','"NO SIDECHAIN SIGNAL"')
a=s.index('auto custom=[&](float v)');b=s.index(';float envelope',a);s=s[:a]+'''auto custom=[&](float v){std::array<float,8> pts;for(int k=0;k<8;++k)pts[(size_t)k]=proc.value("point"+juce::String(k));return OpenDuckProcessor::customShape(v,pts,proc.value("tension"));}'''+s[b:]
s=s.replace('float z=t*7;int j=juce::jmin(6,(int)z);v=juce::jmap(z-j,proc.value("point"+juce::String(j)),proc.value("point"+juce::String(j+1)));','std::array<float,8> pts;for(int j=0;j<8;++j)pts[(size_t)j]=proc.value("point"+juce::String(j));v=OpenDuckProcessor::customShape(t,pts,proc.value("tension"));')
s=s.replace('juce::MemoryBlock data;proc.getStateInformation(data);file.replaceWithData','proc.state.state.setProperty("presetName",file.getFileNameWithoutExtension(),nullptr);juce::MemoryBlock data;proc.getStateInformation(data);file.replaceWithData')
f.write_text(s)
f=p/'Source/Engines.h';s=f.read_text().replace('lastLow=lastHigh=-1;setCutoffs(180,2500);','lastLow=lastHigh=-1;first=true;transition=0;setCutoffs(180,2500);')
s=s.replace('lastLow=low;lastHigh=high;design(low,lp1);design(high,lp2);','''lastLow=low;lastHigh=high;design(low,target1);design(high,target2);if(first){lp1=target1;lp2=target2;first=false;}else{transition=juce::jmax(1,(int)(rate*0.02));for(int k=0;k<taps;++k){delta1[(size_t)k]=(target1[(size_t)k]-lp1[(size_t)k])/transition;delta2[(size_t)k]=(target2[(size_t)k]-lp2[(size_t)k])/transition;}}''')
s=s.replace('void advance(){cursor=(cursor+1)%taps;}','void advance(){cursor=(cursor+1)%taps;if(transition>0){for(int k=0;k<taps;++k){lp1[(size_t)k]+=delta1[(size_t)k];lp2[(size_t)k]+=delta2[(size_t)k];}if(--transition==0){lp1=target1;lp2=target2;}}}')
s=s.replace('int cursor=0;','int cursor=0,transition=0;bool first=true;').replace('std::array<float,taps> lp1{},lp2{};','std::array<float,taps> lp1{},lp2{},target1{},target2{},delta1{},delta2{};')
f.write_text(s)
