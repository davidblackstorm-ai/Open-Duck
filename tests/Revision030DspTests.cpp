#include "../Source/PluginProcessor.h"
#include <iostream>
#include <set>
#include <numeric>
#include <functional>
#include <chrono>

namespace {
int passed=0,failed=0;
void check(bool ok,const char*name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';ok?++passed:++failed;}
void set(OpenDuckProcessor&p,const juce::String&id,float v){auto*q=p.state.getParameter(id);if(!q)throw std::runtime_error("Missing parameter: "+id.toStdString());q->setValueNotifyingHost(q->convertTo0to1(v));}
struct Head:juce::AudioPlayHead{
    double ppq=0,tempo=120;bool playing=true;
    juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setBpm(tempo);p.setPpqPosition(ppq);p.setIsPlaying(playing);return p;}
};
using Setup=std::function<void(OpenDuckProcessor&)>;
using Signal=std::function<float(int,int)>;
using Midi=std::function<void(juce::MidiBuffer&,int,int)>;
struct Render {std::vector<float> input,output;int latency=0;double tailSeconds=0;OpenDuckProcessor::ScopeFrame scope;};
OpenDuckProcessor::CurveSnapshot flat(float y){OpenDuckProcessor::CurveSnapshot s;s.custom=true;s.shape=16;s.count=2;s.points[0]={0,y};s.points[1]={1,y};return s;}
void isolate(OpenDuckProcessor&p,int mode){for(int k=0;k<6;++k){set(p,"on"+juce::String(k),k==mode?1.f:0.f);set(p,"solo"+juce::String(k),0);}set(p,"mix",100);}
Render render(int mode,const Setup&setup={},double duration=1.1,int block=257,double sr=48000,
              const Signal&mainSignal={},const Signal&scSignal={},const Midi&events={},
              const std::function<void(OpenDuckProcessor&,int)>&tick={}){
    auto owner=std::make_unique<OpenDuckProcessor>();auto&p=*owner;Head head;p.setPlayHead(&head);
    auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
    if(!p.setBusesLayout(layout))throw std::runtime_error("Sidechain layout rejected");
    isolate(p,mode);if(setup)setup(p);p.prepareToPlay(sr,block);
    Render result;result.latency=p.getLatencySamples();result.tailSeconds=p.getTailLengthSeconds();const int samples=int(duration*sr);
    result.input.resize((size_t)samples);result.output.resize((size_t)samples);
    for(int start=0;start<samples;start+=block){const int count=juce::jmin(block,samples-start);juce::AudioBuffer<float>b(4,count);juce::MidiBuffer midi;
        for(int i=0;i<count;++i){const int at=start+i;for(int c=0;c<2;++c){const float main=mainSignal?mainSignal(at,c):.25f*std::sin(float(2*juce::MathConstants<double>::pi*1000*at/sr));
            b.setSample(c,i,main);b.setSample(c+2,i,scSignal?scSignal(at,c):0);if(c==0)result.input[(size_t)at]=main;}}
        if(events)events(midi,start,count);if(tick)tick(p,start);head.ppq=start/sr*head.tempo/60;p.processBlock(b,midi);
        for(int i=0;i<count;++i)result.output[(size_t)(start+i)]=b.getSample(0,i);
    }result.scope=p.getScopeFrame(mode);return result;
}
double rms(const std::vector<float>&v,int begin,int end=-1){end=end<0?(int)v.size():end;double sum=0;for(int i=begin;i<end;++i)sum+=double(v[(size_t)i])*v[(size_t)i];return std::sqrt(sum/juce::jmax(1,end-begin));}
double difference(const Render&a,const Render&b,int from){double sum=0;for(int i=from;i<(int)juce::jmin(a.output.size(),b.output.size());++i){double d=a.output[(size_t)i]-b.output[(size_t)i];sum+=d*d;}return std::sqrt(sum/juce::jmax(1,int(a.output.size())-from));}
double nullError(const Render&r,double scale=1){double sum=0;int count=0;for(int i=r.latency+4096;i<(int)r.output.size();++i){double d=r.output[(size_t)i]-scale*r.input[(size_t)(i-r.latency)];sum+=d*d;++count;}return std::sqrt(sum/juce::jmax(1,count));}
double toneAmplitude(const Render&r,double hz,double sr=48000){const int begin=juce::jmax(r.latency+4096,int(sr*.6));double re=0,im=0;int n=0;
    for(int i=begin;i<(int)r.output.size();++i){const double ph=2*juce::MathConstants<double>::pi*hz*i/sr;re+=r.output[(size_t)i]*std::cos(ph);im+=r.output[(size_t)i]*std::sin(ph);++n;}return 2*std::hypot(re,im)/juce::jmax(1,n);}
}
int main(int argc,char**argv){
    std::cout<<std::unitbuf;juce::ScopedJuceInitialiser_GUI init;
    try{
    auto processor=std::make_unique<OpenDuckProcessor>();auto&p=*processor;
    check(p.value("mix")==100&&p.getCurveSnapshot(0).amount==100&&!p.editPointsEnabled(0)&&p.getCurveSnapshot(0).shape==0,"new INIT: MIX100 Amount100 first shape with editing opt-in");
    check(p.value("spectralDepth")==1&&p.value("grooveDepth")==1&&p.value("bandLow")==1&&p.value("bandMid")==1&&p.value("bandHigh")==1,"new INIT has no hidden75/65/35-percent engine-depth caps");
    std::array<int,6> counts{};std::array<std::array<int,7>,6> categories{};std::set<std::string> names;
    bool catalog=true;for(const auto&pr:OpenDuckProcessor::sectorPresets()){catalog&=pr.mode>=0&&pr.mode<6&&pr.category>=0&&pr.category<7;
        if(catalog){++counts[(size_t)pr.mode];++categories[(size_t)pr.mode][(size_t)pr.category];}names.insert(pr.name.toStdString());}
    catalog&=counts==std::array<int,6>{{120,120,120,120,100,120}}&&names.size()==700;
    for(const auto&c:categories)for(int n:c)catalog&=n>0;
    check(catalog,"700 unique sector behaviors, six disjoint catalogs and seven categories each");
    bool inverse=true;auto warp=p.getCurveSnapshot(0);warp.attack=80;warp.release=800;warp.riseSlope=2.5f;
    for(int i=0;i<=1000;++i){float x=i/1000.f;float y=OpenDuckProcessor::rawCurveTimeToCycle(warp,x,.5);float back=OpenDuckProcessor::curveTimeToRaw(warp,y,.5);inverse&=std::abs(back-x)<.0002f;}
    check(inverse,"timewarp is invertible across the whole cycle, including points after recovery");

    const auto zero=render(0,[](auto&q){auto s=flat(0);s.amount=0;q.setCustomCurve(0,s);});
    check(nullError(zero)<3e-6,"Amount0 nulls against the latency-aligned input through the real DSP");
    const auto bypass=render(0,[](auto&q){q.setCustomCurve(0,flat(0));set(q,"bypass",1);});
    check(nullError(bypass)<3e-6,"bypass preserves input with the declared PDC");
    const auto mixZero=render(0,[](auto&q){q.setCustomCurve(0,flat(0));set(q,"mix",0);});
    check(nullError(mixZero)<1e-7,"MIX0 is the exact delayed dry path");
    const auto tail=render(0,[](auto&q){for(int mode=0;mode<6;++mode){set(q,"on"+juce::String(mode),1);auto curve=flat(.8f);curve.amount=50;q.setCustomCurve(mode,curve);q.setSectorParameter(mode,"bands",2);q.setSectorParameter(mode,"lowAmount",20);q.setSectorParameter(mode,"midAmount",50);q.setSectorParameter(mode,"highAmount",80);}},.8,257,48000,[](int sample,int){return sample==0?.5f:0.f;});
    int lastTail=-1;for(int i=0;i<(int)tail.output.size();++i)if(std::abs(tail.output[(size_t)i])>1e-9f)lastTail=i;
    check(lastTail>tail.latency&&lastTail<int(std::ceil(tail.tailSeconds*48000))+4,"reported revision3 tail contains the measured delayed impulse and FIR/STFT ringing");
    std::array<double,6> residuals{};
    for(int mode=0;mode<6;++mode){const auto cut=render(mode,[=](auto&q){q.setCustomCurve(mode,flat(0));for(int j=0;j<16;++j)set(q,"step"+juce::String(j),1);},1.1,257,48000,{},[=](int,int){return mode==5?.5f:0.f;});
        residuals[(size_t)mode]=rms(cut.output,cut.latency+12000)/.176776695;
        check(residuals[(size_t)mode]<.001,("sector "+std::to_string(mode)+" closes a sustained wet plateau by at least60dB").c_str());}
    const auto mix80=render(0,[](auto&q){q.setCustomCurve(0,flat(0));set(q,"mix",80);});
    check(nullError(mix80,.2)<3e-6,"MIX80 retains the mathematically required 20-percent dry floor");
    const auto half=render(0,[](auto&q){auto s=flat(0);s.amount=50;q.setCustomCurve(0,s);});
    const double halfGain=rms(half.output,half.latency+12000)/.176776695;
    check(std::abs(20*std::log10(halfGain)+30)<.05,"Amount50 produces measured minus30dB rather than linear minus6dB");
    const auto spectralAligned=render(1,[](auto&q){q.setCustomCurve(1,flat(0));set(q,"spectralHigh",10000);},1.2,257,48000,
        [](int i,int){return float(.15*std::sin(2*juce::MathConstants<double>::pi*1000*i/48000)+.15*std::sin(2*juce::MathConstants<double>::pi*6000*i/48000));},
        [](int i,int){const int phase=i%24000;const int frequency=phase<4800?1000:6000;return float(.7*std::sin(2*juce::MathConstants<double>::pi*frequency*i/48000));});
    auto windowTone=[&](double hz){double re=0,im=0;const int start=spectralAligned.latency+25440,count=1536;
        for(int j=0;j<count;++j){const int i=start+j;const double ph=2*juce::MathConstants<double>::pi*hz*i/48000;re+=spectralAligned.output[(size_t)i]*std::cos(ph);im+=spectralAligned.output[(size_t)i]*std::sin(ph);}return 2*std::hypot(re,im)/count;};
    check(windowTone(1000)<.03&&windowTone(6000)>.12,"Spectral follows the sidechain frequency from the same program cycle instead of a future215ms window");

    auto shaped=[](OpenDuckProcessor&q,float attack,float release,float tension){auto s=flat(0);s.count=4;s.points[0]={0,1};s.points[1]={.14f,0};s.points[2]={.57f,1};s.points[3]={1,1};s.attack=attack;s.release=release;s.tension=tension;q.setCustomCurve(2,s);};
    const auto fast=render(2,[&](auto&q){shaped(q,1,80,0);},1.5);
    const auto slowAttack=render(2,[&](auto&q){shaped(q,70,80,0);},1.5);
    const auto slowRelease=render(2,[&](auto&q){shaped(q,1,350,0);},1.5);
    check(difference(fast,slowAttack,fast.latency+2048)>.005,"Multiband lower Attack measurably changes audio with BAND off");
    check(difference(fast,slowRelease,fast.latency+2048)>.005,"Multiband lower Release changes recovery without being the cycle clock");
    check(std::abs(fast.scope.periodSeconds-slowRelease.scope.periodSeconds)<1e-9&&std::abs(fast.scope.periodSeconds-.5)<1e-9,"Speed remains one quarter-note when Release changes");
    const auto tensionA=render(0,[](auto&q){auto s=flat(0);s.points[1].y=1;s.tension=-70;q.setCustomCurve(0,s);});
    const auto tensionB=render(0,[](auto&q){auto s=flat(0);s.points[1].y=1;s.tension=70;q.setCustomCurve(0,s);});
    check(difference(tensionA,tensionB,tensionA.latency+2048)>.003,"Tension changes custom-curve audio, not only stored parameters");

    auto pulse=[](int sample,int channel){const int within=sample%24000;const float value=within<192?.7f:0.f;return channel?-value:value;};
    const auto anti=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",1);},1.4,257,48000,[](int,int){return .25f;},pulse);
    const auto noSide=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",1);},1.4,257,48000,[](int,int){return .25f;});
    check(difference(anti,noSide,anti.latency+2048)>.02&&anti.scope.triggerSample>0,"opposite-polarity stereo sidechain still triggers the real ducking path");
    auto note=[](int number,int channel,int velocity){return [=](juce::MidiBuffer&m,int start,int count){const int at=2400;if(at>=start&&at<start+count)m.addEvent(juce::MidiMessage::noteOn(channel,number,(juce::uint8)velocity),at-start);};};
    auto midiSetup=[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",2);q.setSectorParameter(0,"noteLow",36);q.setSectorParameter(0,"noteHigh",36);q.setSectorParameter(0,"midiChannel",10);q.setSectorParameter(0,"velocityLow",64);};
    const auto accepted=render(0,midiSetup,1.1,257,48000,[](int,int){return .25f;},{},note(36,10,100));
    const auto rejected=render(0,midiSetup,1.1,257,48000,[](int,int){return .25f;},{},note(42,10,100));
    const auto wrongChannel=render(0,midiSetup,1.1,257,48000,[](int,int){return .25f;},{},note(36,1,100));
    check(difference(accepted,rejected,accepted.latency+1000)>.02&&nullError(rejected)<1e-6,"MIDI note range accepts kick36 and rejects note42 sample-accurately");
    check(nullError(wrongChannel)<1e-6,"MIDI channel filter rejects an excluded channel");
    std::array<juce::uint8,8192> systemPayload{};const auto systemMessage=juce::MidiMessage::createSysExMessage(systemPayload.data(),int(systemPayload.size()));
    const auto mixedMidi=render(0,midiSetup,1.1,257,48000,[](int,int){return .25f;},{},[&](juce::MidiBuffer&m,int start,int count){if(start==0)m.addEvent(systemMessage,0);if(2400>=start&&2400<start+count)m.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)100),2400-start);});
    check(difference(accepted,mixedMidi,accepted.latency+1000)<1e-7,"long SysEx is ignored without changing accepted short-note audio");
    const auto lookahead=render(0,[&](auto&q){midiSetup(q);q.setSectorParameter(0,"lookahead",10);},1.1,257,48000,[](int,int){return .25f;},{},note(36,10,100));
    auto onset=[](const Render&r){for(int i=r.latency+1500;i<(int)r.output.size();++i)if(r.output[(size_t)i]<.20f)return i;return -1;};
    check(onset(accepted)>0&&std::abs(onset(accepted)-onset(lookahead)-480)<=2,"lookahead10ms actually advances ducking by480samples while PDC stays fixed");
    const auto transientPulse=render(5,[](auto&q){q.setCustomCurve(5,flat(0));q.setSectorParameter(5,"trigger",1);},1.4,257,48000,[](int,int){return .25f;},pulse);
    const auto transientSilent=render(5,[](auto&q){q.setCustomCurve(5,flat(0));q.setSectorParameter(5,"trigger",1);},1.4,257,48000,[](int,int){return .25f;});
    check(difference(transientPulse,transientSilent,transientPulse.latency+2048)>.01,"Transient gate follows a short kick through the same delayed timeline as the program");
    const auto velocity0=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",2);q.setSectorParameter(0,"velocityDepth",0);},1.1,257,48000,[](int,int){return .25f;},{},note(36,10,32));
    const auto velocityHalf=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",2);q.setSectorParameter(0,"velocityDepth",.5f);},1.1,257,48000,[](int,int){return .25f;},{},note(36,10,32));
    const auto velocity1=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",2);q.setSectorParameter(0,"velocityDepth",1);},1.1,257,48000,[](int,int){return .25f;},{},note(36,10,32));
    check(difference(velocity0,velocityHalf,velocity0.latency+1000)>.01&&difference(velocityHalf,velocity1,velocity0.latency+1000)>.01,"velocity depth is a continuous modulation, with audible intermediate strength");
    const auto sustain=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"trigger",2);q.setSectorParameter(0,"loop",2);},1.2,257,48000,[](int,int){return .25f;},{},[](juce::MidiBuffer&m,int start,int count){
        if(2400>=start&&2400<start+count)m.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)100),2400-start);
        if(14400>=start&&14400<start+count)m.addEvent(juce::MidiMessage::noteOff(1,36),14400-start);
    });
    const int audibleOff=14400+sustain.latency;
    check(rms(sustain.output,audibleOff-2400,audibleOff-480)<1e-5&&rms(sustain.output,audibleOff+7200+1024)>.249,"Sustain NoteOff releases on the delayed program timeline, then returns to unity");
    uint64_t frozenGeneration=0;
    const auto frozen=render(0,{},1.4,257,48000,{},pulse,{},[&](auto&q,int sample){if(sample>=48000&&!q.isScopeFrozen()){frozenGeneration=q.getScopeFrame(0).generation;q.setScopeFrozen(true);}});
    int sideBins=0;for(int i=0;i<512;++i)if(std::max(std::abs(frozen.scope.sideMin[(size_t)i]),std::abs(frozen.scope.sideMax[(size_t)i]))>.1f)++sideBins;
    check(frozenGeneration>0&&frozen.scope.complete&&frozen.scope.generation==frozenGeneration,"scope freeze retains a real completed cycle without subsequent overwrite");
    check(sideBins>0&&sideBins<12&&frozen.scope.bins>=510,"single-cycle scope contains one4mskick, not the previous rolling two-second history");
    Head stoppedHead;
    const auto stopped=render(0,[&](auto&q){q.setPlayHead(&stoppedHead);q.setCustomCurve(0,flat(0));},1.2,257,48000,{}, {}, {},[&](auto&,int sample){if(sample>=24000)stoppedHead.playing=false;});
    check(rms(stopped.output,stopped.latency+4096,stopped.latency+8000)<1e-5&&rms(stopped.output,48000)> .17,"Repeat responds to real host Stop by releasing to unity");
    Head continuingHead;
    const auto continuing=render(0,[&](auto&q){q.setPlayHead(&continuingHead);q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"runStopped",1);},1.2,257,48000,{}, {}, {},[&](auto&,int sample){if(sample>=24000)continuingHead.playing=false;});
    check(rms(continuing.output,48000)<1e-5,"optional Run while stopped continues the cycle independently of host playback");
    Head hzNormalHead,hzSeekHead;
    const auto hzNormal=render(0,[&](auto&q){q.setPlayHead(&hzNormalHead);q.setSectorParameter(0,"speedMode",1);q.setSectorParameter(0,"hz",3.7f);},1.4,257,48000,{}, {}, {},[&](auto&,int sample){hzNormalHead.ppq=sample/24000.;});
    const auto hzSeek=render(0,[&](auto&q){q.setPlayHead(&hzSeekHead);q.setSectorParameter(0,"speedMode",1);q.setSectorParameter(0,"hz",3.7f);},1.4,257,48000,{}, {}, {},[&](auto&,int sample){hzSeekHead.ppq=sample/24000.+(sample>=24000?128:0);hzSeekHead.tempo=sample>=24000?90:120;});
    check(difference(hzNormal,hzSeek,hzNormal.latency+2048)<1e-7,"Free Hz ignores host PPQ seek and BPM changes instead of silently phase-locking");

    auto dual=[](int i,int){return float(.15*std::sin(2*juce::MathConstants<double>::pi*100*i/48000)+.15*std::sin(2*juce::MathConstants<double>::pi*8000*i/48000));};
    const auto lowCut=render(0,[](auto&q){q.setCustomCurve(0,flat(0));q.setSectorParameter(0,"bands",1);q.setSectorParameter(0,"crossLow",1500);q.setSectorParameter(0,"lowAmount",100);q.setSectorParameter(0,"highAmount",0);},1.5,257,48000,dual);
    check(toneAmplitude(lowCut,100)<.0015&&std::abs(toneAmplitude(lowCut,8000)-.15)<.002,"two-band Low100 High0 attenuates bass while retaining high-frequency program");
    const auto highSolo=render(0,[](auto&q){q.setCustomCurve(0,flat(1));q.setSectorParameter(0,"bands",1);q.setSectorParameter(0,"crossLow",500);q.setSectorParameter(0,"crossHigh",5000);q.setSectorParameter(0,"highSolo",1);},1.5,257,48000,[](int i,int){return float(.15*std::sin(2*juce::MathConstants<double>::pi*100*i/48000)+.15*std::sin(2*juce::MathConstants<double>::pi*2500*i/48000));});
    check(toneAmplitude(highSolo,100)<.0015&&toneAmplitude(highSolo,2500)>.145,"two-band High Solo retains the whole region above CrossLow, including frequencies below CrossHigh");
    const auto independent=render(2,[](auto&q){q.setSectorParameter(2,"linked",0);q.setSectorParameter(2,"bands",2);q.setSectorParameter(2,"crossLow",1500);q.setSectorParameter(2,"crossHigh",4000);q.setBandCurveSnapshot(2,0,flat(0));q.setBandCurveSnapshot(2,1,flat(1));q.setBandCurveSnapshot(2,2,flat(1));},1.5,257,48000,dual);
    check(toneAmplitude(independent,100)<.0015&&std::abs(toneAmplitude(independent,8000)-.15)<.002,"three independent Multiband curves act on their actual frequency bands");
    const auto bandTimed=render(2,[&](auto&q){shaped(q,10,150,0);q.setSectorParameter(2,"linked",0);auto curve=q.getCurveSnapshot(2);curve.attack=80;curve.release=400;curve.phase=20;q.setBandCurveSnapshot(2,0,curve);q.setBandCurveSnapshot(2,1,curve);q.setBandCurveSnapshot(2,2,curve);},1.5);
    const auto bandUntimed=render(2,[&](auto&q){shaped(q,10,150,0);q.setSectorParameter(2,"linked",0);auto curve=q.getCurveSnapshot(2);q.setBandCurveSnapshot(2,0,curve);q.setBandCurveSnapshot(2,1,curve);q.setBandCurveSnapshot(2,2,curve);},1.5);
    check(difference(bandTimed,bandUntimed,bandTimed.latency+2048)>.01,"independent band Attack/Release/Phase remain effective alongside master controls");
    set(p,"v3Speed0",19);p.setSectorParameter(0,"locked",1);p.applySectorPreset(21,0);const float lockedSpeed=p.value("v3Speed0");
    p.setSectorParameter(0,"locked",0);p.applySectorPreset(21,0);
    check(lockedSpeed==19&&p.value("v3Speed0")!=19,"trigger-settings LOCK preserves Speed during browsing; unlocked browsing updates it");
    auto geometry=std::make_unique<OpenDuckProcessor>();bool fullInverse=true;
    for(int mode=0;mode<6;++mode){auto curve=flat(.2f);curve.count=3;curve.points[1]={.4f,.6f,4,.1f};curve.points[2]={1,.9f};curve.phase=14;curve.amount=83;curve.attack=22;curve.release=210;geometry->setCustomCurve(mode,curve);
        geometry->setSectorParameter(mode,"character",.8f);geometry->setSectorParameter(mode,"offset",13);geometry->setSectorParameter(mode,"lookahead",7);geometry->setSectorParameter(mode,"swing",20);geometry->setSectorParameter(mode,"riseSlope",1.3f);
        const float cycle=geometry->rawToPreviewCycle(mode,.4f);const float raw=geometry->previewCycleToRaw(mode,cycle);
        const float gain=geometry->previewCurveGain(mode,cycle);const float value=geometry->previewGainToCurveEnvelope(mode,cycle,gain);
        fullInverse&=std::abs(raw-.4f)<.0002f&&std::abs(value-.6f)<.0003f;
    }
    check(fullInverse,"full preview coordinate inverse includes Character Morph Swing Offset Lookahead and timewarp");
    const auto smallRate=render(2,[](auto&q){q.setCustomCurve(2,flat(0));q.setSectorParameter(2,"crossLow",20000);q.setSectorParameter(2,"crossHigh",20000);q.setSectorParameter(2,"lowAmount",20);q.setSectorParameter(2,"midAmount",60);q.setSectorParameter(2,"highAmount",80);},.8,257,8000);
    check(std::all_of(smallRate.output.begin(),smallRate.output.end(),[](float v){return std::isfinite(v);}),"8kHz host with extreme crossover controls remains finite and within Nyquist");
    auto performance=std::make_unique<OpenDuckProcessor>();for(int mode=0;mode<6;++mode){set(*performance,"on"+juce::String(mode),1);performance->setSectorParameter(mode,"bands",2);performance->setSectorParameter(mode,"lowAmount",20);performance->setSectorParameter(mode,"midAmount",60);performance->setSectorParameter(mode,"highAmount",90);}
    auto performanceLayout=performance->getBusesLayout();performanceLayout.inputBuses.set(1,juce::AudioChannelSet::stereo());
    if(!performance->setBusesLayout(performanceLayout))throw std::runtime_error("Performance stereo sidechain layout rejected");
    performance->prepareToPlay(48000,257);Head performanceHead;performance->setPlayHead(&performanceHead);double worst=0;
    for(int iteration=0;iteration<24;++iteration){for(int mode=0;mode<6;++mode){performance->setSectorParameter(mode,"crossLow",float(80+iteration*17));performance->setSectorParameter(mode,"crossHigh",float(3000+iteration*83));performance->setSectorParameter(mode,"slope",float(iteration%3));}
        juce::AudioBuffer<float>block(4,257);for(int i=0;i<257;++i){const int sample=iteration*257+i;block.setSample(0,i,.25f);block.setSample(1,i,.25f);
            block.setSample(2,i,float(.3*std::sin(2*juce::MathConstants<double>::pi*3*sample/1024)));
            block.setSample(3,i,float(.3*std::sin(2*juce::MathConstants<double>::pi*21*sample/1024)));}
        performanceHead.ppq=iteration*257.0/48000*performanceHead.tempo/60;
        juce::MidiBuffer midi;auto beforeClock=std::chrono::steady_clock::now();performance->processBlock(block,midi);worst=std::max(worst,std::chrono::duration<double>(std::chrono::steady_clock::now()-beforeClock).count());}
    check(worst<257.0/48000,"six-sector crossover automation with distinct stereo-key FFTs meets the actual257-sample callback deadline");

    auto a=std::make_unique<OpenDuckProcessor>(),b=std::make_unique<OpenDuckProcessor>();
    auto local=b->getCurveSnapshot(0);local.phase=17;b->setCurveSnapshot(0,local);
    a->setSectorParameter(0,"group",2);b->setSectorParameter(0,"group",2);
    auto shared=flat(.2f);shared.count=3;shared.points[1]={.4f,.7f,4,.3f};shared.points[2]={1,.2f};a->setCustomCurve(0,shared);
    const auto remote=b->getCurveSnapshot(0);
    check(remote.count==3&&std::abs(remote.points[1].y-.7f)<1e-6&&remote.phase==17,"Group shares only same-sector shape while keeping recipient phase");
    b->setSectorParameter(0,"group",0);
    check(!b->getCurveSnapshot(0).custom&&b->getCurveSnapshot(0).phase==17,"Group Off restores the original local shape and independent controls");

    auto bandShape=flat(.4f);bandShape.points[0].segment=2;bandShape.points[0].curvature=-.4f;p.setBandCurveSnapshot(2,1,bandShape);
    set(p,"curvePhase4",13.25f);p.setSectorParameter(4,"hz",3.7f);juce::MemoryBlock state;p.getStateInformation(state);
    auto restored=std::make_unique<OpenDuckProcessor>();restored->setStateInformation(state.getData(),int(state.getSize()));
    check(restored->usesRevision3(4)&&std::abs(restored->value("curvePhase4")-13.25f)<1e-6&&std::abs(restored->value("v3Hz4")-3.7f)<1e-6&&restored->getBandCurveSnapshot(2,1).points[0].segment==2&&std::abs(restored->getBandCurveSnapshot(2,1).points[0].curvature+.4f)<1e-6,"schema3 restores per-sector controls, three band curves and segment geometry");
    auto legacyXml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));legacyXml->setAttribute("schema",2);juce::MemoryBlock legacy;juce::AudioProcessor::copyXmlToBinary(*legacyXml,legacy);
    auto old=std::make_unique<OpenDuckProcessor>();old->setStateInformation(legacy.getData(),int(legacy.getSize()));set(*old,"curveAmount0",61);old->setSectorParameter(0,"speed",7);
    check(!old->usesRevision3(0)&&old->getLatencySamples()==1152,"schema2 and legacy control edits retain the original engine and PDC until explicit upgrade");
    old->upgradeToRevision3();check(old->usesRevision3(0)&&old->getLatencySamples()==old->revision3LatencySamples(),"explicit audio-engine upgrade activates serial0.3 routing and its declared PDC");
    auto memory=std::make_unique<OpenDuckProcessor>();memory->selectFactoryShape(0,0);set(*memory,"curvePhase0",12);set(*memory,"curveLength0",.65f);memory->setSectorParameter(0,"riseSlope",1.7f);memory->selectFactoryShape(1,0);memory->selectFactoryShape(0,0);
    check(std::abs(memory->value("curvePhase0")-12)<1e-6&&std::abs(memory->value("curveLength0")-.65f)<1e-6&&std::abs(memory->value("v3RiseSlope0")-1.7f)<1e-6,"each factory shape remembers its own Shift Length Tension and Slope transformations");
    const auto before=p.getCurveSnapshot(0);p.transformCurve(OpenDuckProcessor::CurveOperation::mirrorVertical,0);p.transformCurve(OpenDuckProcessor::CurveOperation::mirrorVertical,0);
    bool mirror=true;const auto after=p.getCurveSnapshot(0);for(int i=0;i<=500;++i)mirror&=std::abs(OpenDuckProcessor::sampleCurve(before,i/500.f)-OpenDuckProcessor::sampleCurve(after,i/500.f))<.003f;
    check(mirror,"two vertical mirror operations preserve the original audible shape");
    juce::AudioBuffer<float> silent(2,512);silent.clear();check(p.traceAudio(silent,48000).failed(),"Trace rejects silent input instead of inventing a curve");
    juce::AudioBuffer<float> trace(1,24000);for(int i=0;i<24000;++i)trace.setSample(0,i,float(std::exp(-i/2400.)*std::sin(2*juce::MathConstants<double>::pi*80*i/48000)));
    check(p.traceAudio(trace,48000,0).wasOk()&&p.getCurveSnapshot(0).custom&&p.getCurveSnapshot(0).count==193,"Trace builds an inverse envelope from actual supplied audio");

    if(argc>1){const juce::File folder(juce::String::fromUTF8(argv[1]));folder.createDirectory();auto*report=new juce::DynamicObject;juce::Array<juce::var> values;for(double x:residuals)values.add(x);report->setProperty("sectorWetResidualRatios",values);report->setProperty("latencySamples48000",zero.latency);report->setProperty("halfAmountMeasuredGain",halfGain);report->setProperty("amountZeroNullRms",nullError(zero));report->setProperty("worstAutomationBlockMs",worst*1000);report->setProperty("passed",passed);report->setProperty("failed",failed);folder.getChildFile("dsp030-measures.json").replaceWithText(juce::JSON::toString(juce::var(report),true));}
    }catch(const std::exception&e){std::cout<<"FAIL exception: "<<e.what()<<'\n';++failed;}
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
