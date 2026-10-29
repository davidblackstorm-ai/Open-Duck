from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.h'
s=f.read_text().replace('#include <JuceHeader.h>','#include <JuceHeader.h>\n#include "Engines.h"')
s=s.replace('std::atomic<unsigned> waveWrite{0};','std::atomic<unsigned> waveWrite{0};\n std::array<std::atomic<float>,6> engineActivity{};\n juce::UndoManager undo;')
s=s.replace('int hold=0;','int hold=0;\n SpectralEngine spectral; BandEngine bands;\n std::array<std::array<float,1024>,2> dryDelay{};\n std::array<std::array<float,1024>,2> wetDelay{};\n int delayPos=0,waveCounter=0;float wavePeak=0,spectralSmooth=0,mixSmooth=1,bypassSmooth=1;\n std::array<float,3> bandSmooth{{1,1,1}};\n std::array<float,6> engineSmooth{};\n juce::SmoothedValue<float> morphSmooth;')
f.write_text(s)
f=p/'Source/PluginProcessor.cpp';s=f.read_text();s=s.replace('state(*this,nullptr,','state(*this,&undo,')
s=s.replace('b("sync",true);', 'f("morph",0,1,0.5f);f("spectralLow",20,2000,40);f("spectralHigh",2001,20000,8000);f("bandLow",0,1,1);f("bandMid",0,1,0.65f);f("bandHigh",0,1,0.35f);f("crossLow",40,500,180);f("crossHigh",1000,10000,2500);f("hysteresis",1,18,3);f("grooveDepth",0,1,0.65f);f("spectralDepth",0,1,0.75f);\n b("sync",true);')
s=s.replace('for(int i=0;i<8;++i)f(', 'for(int i=0;i<16;++i)b("step"+juce::String(i),(i%4)!=3);\n for(int i=0;i<8;++i)f(')
a=s.index('void OpenDuckProcessor::prepareToPlay');b=s.index('float OpenDuckProcessor::shape',a)
s=s[:a]+'''void OpenDuckProcessor::prepareToPlay(double sr,int){rate=sr;phase=0.999999;follower=0;smoothedGain=1;gate=false;hold=0;for(auto& s:waveform)s=0;spectral.reset();bands.prepare(sr);for(auto& d:dryDelay)d.fill(0);for(auto& d:wetDelay)d.fill(0);delayPos=0;bandSmooth.fill(1);engineSmooth.fill(0);spectralSmooth=0;mixSmooth=value("mix")/100;bypassSmooth=1;waveCounter=0;wavePeak=0;morphSmooth.reset(sr,0.02);morphSmooth.setCurrentAndTargetValue(value("morph"));setLatencySamples(SpectralEngine::size);}
''' +s[b:]
a=s.index('void OpenDuckProcessor::processBlock');b=s.index('void OpenDuckProcessor::getStateInformation',a)
s=s[:a]+'''void OpenDuckProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi){
 juce::ScopedNoDenormals noDenormals;auto main=getBusBuffer(buffer,false,0);auto sc=getBusBuffer(buffer,true,1);const int channels=main.getNumChannels();
 const bool hasSc=sc.getNumChannels()>0;sidechainPresent=hasSc;const int trigger=(int)value("trigger"),shapeType=(int)value("shape");
 double tempo=value("freeBpm"),ppq=0;bool playing=true,positioned=false;const bool synced=value("sync")>0.5f;
 if(synced)if(auto* ph=getPlayHead())if(auto pos=ph->getPosition()){if(auto b=pos->getBpm())tempo=*b;playing=pos->getIsPlaying();if(auto q=pos->getPpqPosition()){ppq=*q;positioned=true;}}
 bpm=(float)tempo;const double divisions[]={1,0.5,0.25,1.5,0.75,1.0/3,1.0/6};const double beats=divisions[juce::jlimit(0,6,(int)value("grid"))];
 double cycleIndex=positioned?std::floor(ppq/beats):0;if(trigger==0&&positioned)phase=ppq/beats-cycleIndex;
 const float amount=value("amount")/100,mix=value("mix")/100,tension=value("tension"),shift=value("shift")/100,swing=value("swing")/100;
 const float threshold=juce::Decibels::decibelsToGain(value("threshold")),hysteresis=juce::Decibels::decibelsToGain(-value("hysteresis"));
 const float a=std::exp(-1.0f/((value("attack")*0.001f)*(float)rate)),r=std::exp(-1.0f/((value("release")*0.001f)*(float)rate)),slew=std::exp(-1.0f/(0.005f*(float)rate));
 const int holdSamples=(int)(value("hold")*0.001*rate);const double releaseSeconds=value("release")*0.001;
 std::array<float,8> points;for(int k=0;k<8;++k)points[(size_t)k]=value("point"+juce::String(k));
 std::array<bool,6> on,solo;bool anySolo=false;for(int k=0;k<6;++k){on[(size_t)k]=value("on"+juce::String(k))>0.5f;solo[(size_t)k]=value("solo"+juce::String(k))>0.5f;anySolo|=solo[(size_t)k];}
 std::array<bool,16> steps;for(int k=0;k<16;++k)steps[(size_t)k]=value("step"+juce::String(k))>0.5f;
 auto enabled=[&](int k){return anySolo?solo[(size_t)k]:on[(size_t)k];};
 const bool bypass=value("bypass")>0.5f;const float specLow=value("spectralLow"),specHigh=juce::jmin(value("spectralHigh"),(float)rate*0.49f),specDepth=value("spectralDepth"),grooveDepth=value("grooveDepth");
 const std::array<float,3> bandDepth{{value("bandLow"),value("bandMid"),value("bandHigh")}};bands.setCutoffs(value("crossLow"),juce::jmin(value("crossHigh"),(float)rate*0.45f));morphSmooth.setTargetValue(value("morph"));
 auto custom=[&](float t){float x=juce::jlimit(0.0f,1.0f,t)*7;int k=juce::jmin(6,(int)x);return juce::jmap(x-k,points[(size_t)k],points[(size_t)(k+1)]);};
 float peakIn=0,peakOut=0,maxReduction=0;std::array<float,6> activities{};auto event=midi.cbegin();
 for(int i=0;i<main.getNumSamples();++i){
  while(event!=midi.cend()&&(*event).samplePosition<=i){if(trigger==2&&(*event).getMessage().isNoteOn())phase=0;++event;}
  float side=0;for(int c=0;c<sc.getNumChannels();++c)side+=sc.getSample(c,i)/(float)sc.getNumChannels();
  if(std::abs(side)>std::abs(wavePeak))wavePeak=side;if(++waveCounter>=juce::jmax(1,(int)(rate/256))){waveform[waveWrite.fetch_add(1)%512].store(wavePeak,std::memory_order_relaxed);waveCounter=0;wavePeak=0;}
  float inputLevel=0;for(int c=0;c<channels;++c)inputLevel=juce::jmax(inputLevel,std::abs(main.getSample(c,i)));peakIn=juce::jmax(peakIn,inputLevel);
  const float detector=hasSc?std::abs(side):trigger==1?0:inputLevel;follower=detector+(detector>follower?a:r)*(follower-detector);
  if(!gate&&follower>threshold){if(trigger==1)phase=0;gate=true;hold=holdSamples;}if(hold>0)--hold;else if(follower<threshold*hysteresis)gate=false;
  float t=(float)phase;const float split=0.5f+swing*0.5f;t=t<split?0.5f*t/split:0.5f+0.5f*(t-split)/(1-split);t=juce::jlimit(0.0f,1.0f,t-shift);
  float envelope=shapeType==5?custom(t):shape(t,shapeType,tension);if(trigger!=0&&phase>=0.999998)envelope=1;if(trigger==0&&!playing)envelope=1;
  const float curveReduction=amount*(1-envelope);const float morph=morphSmooth.getNextValue();const float morphEnvelope=(1-morph)*shape(t,0,tension)+morph*(shapeType==5?custom(t):shape(t,1,tension));
  const int step=((int)cycleIndex%16+16)%16;
  std::array<float,6> desired{{curveReduction,curveReduction*specDepth,curveReduction,amount*(1-morphEnvelope),steps[(size_t)step]?curveReduction*grooveDepth:0,gate?amount:0}};
  if(trigger==0&&!playing){desired.fill(0);}if(trigger!=0&&phase>=0.999998){desired[3]=0;}
  float fullReduction=0;for(int k=0;k<6;++k){float target=enabled(k)?desired[(size_t)k]:0;float& v=engineSmooth[(size_t)k];v=target+(target>v?a:r)*(v-target);activities[(size_t)k]=juce::jmax(activities[(size_t)k],v);if(k==0||k==3||k==4||k==5)fullReduction=juce::jmax(fullReduction,v);}
  // Fullband engines share a maximum reduction; band/spectral stages share a bounded budget.
  std::array<float,3> gains;float minimumBand=1;for(int k=0;k<3;++k){float bd=engineSmooth[2]*bandDepth[(size_t)k];gains[(size_t)k]=1-juce::jmax(fullReduction,bd);minimumBand=juce::jmin(minimumBand,gains[(size_t)k]);}
  const float permittedExtra=juce::jlimit(0.0f,0.95f,(1-engineSmooth[1])/juce::jmax(0.05f,minimumBand));const float spectralDepth=1-permittedExtra;
  float input[2]{},filtered[2]{},spec[2]{};for(int c=0;c<channels;++c){input[c]=main.getSample(c,i);filtered[c]=bands.process(c,input[c],gains,dryDelay[(size_t)c][(size_t)delayPos]);}
  spectral.process(filtered,side,spec,channels,spectralDepth,specLow,specHigh,rate);
  mixSmooth=mix+(mixSmooth-mix)*slew;bypassSmooth=(bypass?0.0f:1.0f)+(bypassSmooth-(bypass?0.0f:1.0f))*slew;
  for(int c=0;c<channels;++c){const float dry=wetDelay[(size_t)c][(size_t)delayPos];wetDelay[(size_t)c][(size_t)delayPos]=dryDelay[(size_t)c][(size_t)delayPos];float y=dry+(spec[c]-dry)*mixSmooth*bypassSmooth;if(!std::isfinite(y))y=0;main.setSample(c,i,y);peakOut=juce::jmax(peakOut,std::abs(y));}
  delayPos=(delayPos+1)%1024;maxReduction=juce::jmax(maxReduction,1-minimumBand*(1-spectralDepth));
  if(trigger==0){if(playing){phase+=tempo/(60*rate*beats);if(phase>=1){phase-=1;cycleIndex+=1;}}}else phase=juce::jmin(0.999999,phase+1/(rate*releaseSeconds));
 }
 inMeter=peakIn;outMeter=peakOut;reduction=maxReduction;for(int k=0;k<6;++k)engineActivity[(size_t)k]=activities[(size_t)k];
}
''' +s[b:]
f.write_text(s)
