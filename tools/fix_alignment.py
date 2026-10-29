from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'CMakeLists.txt';s=f.read_text().replace('tests/ProcessorTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp','tests/ProcessorTests.cpp').replace('target_link_libraries(OpenDuckTests PRIVATE OpenDuckProX)','target_link_libraries(OpenDuckTests PRIVATE OpenDuckProX OpenDuckAssets juce::juce_audio_utils juce::juce_dsp)\njuce_generate_juce_header(OpenDuckTests)');f.write_text(s)
f=p/'Source/PluginProcessor.h';s=f.read_text().replace('juce::AudioProcessorValueTreeState state;','juce::UndoManager undo;\n juce::AudioProcessorValueTreeState state;').replace(' std::array<std::atomic<float>,6> engineActivity{};\n juce::UndoManager undo;',' std::array<std::atomic<float>,6> engineActivity{};').replace('float,1024>','float,1152>');f.write_text(s)
f=p/'Source/Engines.h';s=f.read_text();s=s[:s.index('// Three LR4')]+'''
// Complementary linear-phase windowed-sinc FIR crossover. All three bands sum
// exactly to a 128-sample delay, including when the cutoff is automated.
class BandEngine {
public:
 static constexpr int taps=257,delay=128;
 void prepare(double sr){rate=sr;for(auto& a:history)a.fill(0);cursor=0;lastLow=lastHigh=-1;setCutoffs(180,2500);}
 void setCutoffs(float low,float high){if(low==lastLow&&high==lastHigh)return;lastLow=low;lastHigh=high;design(low,lp1);design(high,lp2);}
 float process(int ch,float x,const std::array<float,3>& gains,float& unattenuated){auto& data=history[(size_t)ch];data[(size_t)cursor]=x;float lo=0,belowHigh=0;for(int k=0;k<taps;++k){float sample=data[(size_t)((cursor+taps-k)%taps)];lo+=sample*lp1[(size_t)k];belowHigh+=sample*lp2[(size_t)k];}unattenuated=data[(size_t)((cursor+taps-delay)%taps)];float result=lo*gains[0]+(belowHigh-lo)*gains[1]+(unattenuated-belowHigh)*gains[2];return result;}
 void advance(){cursor=(cursor+1)%taps;}
private:
 void design(float hz,std::array<float,taps>& coeff){float sum=0;double fc=hz/rate;for(int k=0;k<taps;++k){int n=k-delay;double sinc=n==0?2*fc:std::sin(juce::MathConstants<double>::twoPi*fc*n)/(juce::MathConstants<double>::pi*n);double w=0.42-0.5*std::cos(juce::MathConstants<double>::twoPi*k/(taps-1))+0.08*std::cos(2*juce::MathConstants<double>::twoPi*k/(taps-1));coeff[(size_t)k]=(float)(sinc*w);sum+=coeff[(size_t)k];}for(auto& c:coeff)c/=sum;}
 double rate=48000;float lastLow=-1,lastHigh=-1;int cursor=0;
 std::array<float,taps> lp1{},lp2{};std::array<std::array<float,taps>,2> history{};
};
''';f.write_text(s)
f=p/'Source/PluginProcessor.cpp';s=f.read_text().replace('setLatencySamples(SpectralEngine::size)','setLatencySamples(SpectralEngine::size+BandEngine::delay)').replace('(1-engineSmooth[1])/juce::jmax(0.05f,minimumBand)','(1-engineSmooth[1])/juce::jmax(0.05f,minimumBand)').replace('juce::jlimit(0.0f,0.95f,(1-engineSmooth[1])','juce::jlimit(0.0f,1.0f,(1-engineSmooth[1])')
a=s.index('  float input[2]');b=s.index('  delayPos=',a)
s=s[:a]+'''  float input[2]{},filtered[2]{},spec[2]{},dry[2]{};for(int c=0;c<channels;++c){input[c]=main.getSample(c,i);dry[c]=dryDelay[(size_t)c][(size_t)delayPos];dryDelay[(size_t)c][(size_t)delayPos]=input[c];float aligned=0;filtered[c]=bands.process(c,input[c],gains,aligned);}bands.advance();
  spectral.process(filtered,side,spec,channels,spectralDepth,specLow,specHigh,rate);
  mixSmooth=mix+(mixSmooth-mix)*slew;bypassSmooth=(bypass?0.0f:1.0f)+(bypassSmooth-(bypass?0.0f:1.0f))*slew;
  for(int c=0;c<channels;++c){float y=dry[c]+(spec[c]-dry[c])*mixSmooth*bypassSmooth;if(!std::isfinite(y))y=0;main.setSample(c,i,y);peakOut=juce::jmax(peakOut,std::abs(y));}
''' +s[b:];s=s.replace('delayPos=(delayPos+1)%1024','delayPos=(delayPos+1)%1152');f.write_text(s)
