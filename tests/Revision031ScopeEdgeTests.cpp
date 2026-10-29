#include "PluginProcessor.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace {
constexpr int blockSize=257;constexpr double rate=48000.;int passed=0,failed=0;
void check(bool ok,const std::string&name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';ok?++passed:++failed;}
void set(OpenDuckProcessor&p,const juce::String&id,float value){auto*q=p.state.getParameter(id);if(!q)throw std::runtime_error("Missing parameter");q->setValueNotifyingHost(q->convertTo0to1(value));}
struct Head:juce::AudioPlayHead{double ppq=0;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setBpm(120.);p.setPpqPosition(ppq);p.setIsPlaying(true);return p;}};
std::unique_ptr<OpenDuckProcessor>make(Head&head,int trigger){auto p=std::make_unique<OpenDuckProcessor>();p->setPlayHead(&head);auto layout=p->getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());if(!p->setBusesLayout(layout))throw std::runtime_error("Stereo SC rejected");for(int mode=0;mode<6;++mode){set(*p,"on"+juce::String(mode),mode==0?1.f:0.f);p->setSectorParameter(mode,"trigger",float(trigger));p->setSectorParameter(mode,"speed",2);}p->prepareToPlay(rate,blockSize);return p;}
bool allNew(const OpenDuckProcessor::ScopeFrame&f){if(!f.complete||f.bins<510)return false;for(int bin=0;bin<512;++bin)if(f.sideMax[(size_t)bin]<.799f)return false;return true;}
void periodChange(){Head head;auto owner=make(head,0);auto&p=*owner;
    const int changeBlock=((54000+p.getLatencySamples()+blockSize-1)/blockSize)*blockSize;
    const int sourceChange=changeBlock-p.getLatencySamples();
    std::array<OpenDuckProcessor::ScopeFrame,6>before{},firstNew{};std::array<bool,6>seen{};
    for(int start=0;start<changeBlock+2*48000+1024;start+=blockSize){
        if(start==changeBlock)for(int mode=0;mode<6;++mode){before[(size_t)mode]=p.getScopeFrame(mode);p.setSectorParameter(mode,"speed",1);}
        juce::AudioBuffer<float>audio(4,blockSize);juce::MidiBuffer midi;
        for(int i=0;i<blockSize;++i){const float marker=start+i>=sourceChange?.8f:.1f;audio.setSample(0,i,.15f);audio.setSample(1,i,.15f);audio.setSample(2,i,marker);audio.setSample(3,i,-marker);}
        head.ppq=start/rate*2;p.processBlock(audio,midi);
        if(start>=changeBlock)for(int mode=0;mode<6;++mode){const auto f=p.getScopeFrame(mode);const auto n=(size_t)mode;if(!seen[n]&&f.complete&&f.generation>before[n].generation){firstNew[n]=f;seen[n]=true;}}
    }
    for(int mode=0;mode<6;++mode){const auto n=(size_t)mode;check(before[n].complete&&std::abs(before[n].periodSeconds-.5)<1e-9&&seen[n]&&std::abs(firstNew[n].periodSeconds-1.)<1e-9&&allNew(firstNew[n]),"mode"+std::to_string(mode)+": first completed frame after mid-cycle Speed change has one new physical domain and no old-source bins");}
}
void boundedBurst(){Head head;auto owner=make(head,2);auto&p=*owner;constexpr int burst=4800,recovery=48000;bool finite=true;
    for(int start=0;start<recovery+p.getLatencySamples()+24000+1024;start+=blockSize){
        juce::AudioBuffer<float>audio(4,blockSize);juce::MidiBuffer midi;
        for(int i=0;i<blockSize;++i){const float marker=start+i>=recovery?.8f:.1f;audio.setSample(0,i,.15f);audio.setSample(1,i,.15f);audio.setSample(2,i,marker);audio.setSample(3,i,-marker);}
        if(burst>=start&&burst<start+blockSize)for(int note=0;note<600;++note){midi.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)100),burst-start);midi.addEvent(juce::MidiMessage::noteOff(10,36),burst-start);}
        if(recovery>=start&&recovery<start+blockSize)midi.addEvent(juce::MidiMessage::noteOn(10,36,(juce::uint8)100),recovery-start);
        head.ppq=start/rate*2;p.processBlock(audio,midi);
        for(int c=0;c<2;++c)for(int i=0;i<blockSize;++i)finite&=std::isfinite(audio.getSample(c,i));
    }
    check(finite,"1200 same-sample MIDI events leave the real audio output finite");
    for(int mode=0;mode<6;++mode){const auto f=p.getScopeFrame(mode);check(f.triggerSample==recovery&&allNew(f),"mode"+std::to_string(mode)+": a real normal hit restores a complete fresh scope after a burst beyond the bounded display queue");}
}
void traceRecent(){Head head;auto owner=make(head,1);auto&p=*owner;
    constexpr int seed=4800,period=24000,train=seed+3*period;
    const int stop=train+6*period+p.getLatencySamples()+257;
    for(int mode=0;mode<6;++mode){p.setSectorParameter(mode,"threshold",-36);p.setSectorParameter(mode,"detectorAttack",.1f);p.setSectorParameter(mode,"detectorRelease",2);p.setSectorParameter(mode,"holdoff",100);}
    for(int start=0;start<stop;start+=blockSize){juce::AudioBuffer<float>audio(4,blockSize);juce::MidiBuffer midi;
        for(int i=0;i<blockSize;++i){const int sample=start+i;int age=sample-seed,length=960;
            if(sample>=train){const int hit=(sample-train)/period;age=(sample-train)%period;length=hit%2?6720:960;}
            const float marker=age>=0&&age<length?.8f:0.f;
            audio.setSample(0,i,.15f);audio.setSample(1,i,.15f);audio.setSample(2,i,marker);audio.setSample(3,i,-marker);}
        head.ppq=start/rate*2;p.processBlock(audio,midi);
    }
    for(int mode=0;mode<6;++mode){const auto frame=p.getScopeFrame(mode);const auto result=p.traceSidechain(mode);const auto curve=p.getCurveSnapshot(mode);
        check(frame.triggerSample>=uint64_t(train+5*period)&&result.wasOk()&&curve.custom&&curve.count==193&&OpenDuckProcessor::sampleCurve(curve,.20f)<.1f&&OpenDuckProcessor::sampleCurve(curve,.50f)>.9f,
              "mode"+std::to_string(mode)+": Trace SC follows the recent140ms kick while the20ms seed and alternating hits are still being replaced");}
}
void concurrentFreeze(){Head head;auto owner=make(head,0);auto&p=*owner;
    std::atomic<bool>stop{false},workerFailed{false};std::atomic<int>callbacks{0};
    std::thread audio([&]{try{int sample=0;while(!stop.load()){
        juce::AudioBuffer<float>buffer(4,blockSize);juce::MidiBuffer midi;
        for(int i=0;i<blockSize;++i){const float marker=.1f+.7f*((sample+i)/24000%2);buffer.setSample(0,i,.15f);buffer.setSample(1,i,.15f);buffer.setSample(2,i,marker);buffer.setSample(3,i,-marker);}
        head.ppq=sample/rate*2;p.processBlock(buffer,midi);sample+=blockSize;++callbacks;
    }}catch(...){workerFailed=true;stop=true;}});
    auto wait=[&](int target){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(callbacks.load()<target&&!workerFailed.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();return callbacks.load()>=target&&!workerFailed.load();};
    bool stable=wait(300);const auto seed=p.getScopeFrame(0).generation;
    for(int repeat=0;repeat<300&&stable;++repeat){p.setScopeFrozen(true);std::array<OpenDuckProcessor::ScopeFrame,6>before{};
        for(int mode=0;mode<6;++mode)before[(size_t)mode]=p.getScopeFrame(mode);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        for(int mode=0;mode<6;++mode){const auto after=p.getScopeFrame(mode);const auto&f=before[(size_t)mode];stable&=after.generation==f.generation&&after.phase==f.phase&&after.mainMin==f.mainMin&&after.mainMax==f.mainMax&&after.sideMin==f.sideMin&&after.sideMax==f.sideMax&&after.outMin==f.outMin&&after.outMax==f.outMax;}
        p.setScopeFrozen(false);std::this_thread::yield();
    }
    const bool resumed=wait(callbacks.load()+300);const bool recent=p.getScopeFrame(0).generation>seed;
    stop=true;audio.join();
    check(stable&&!workerFailed.load(),"300 Freeze clicks concurrent with real audio callbacks preserve the returned cursor phase, generation and every waveform bin in all six sectors");
    check(resumed&&recent,"scope publishes a new complete cycle after concurrent Freeze stress ends");
}
}
int main(){std::cout<<std::unitbuf;juce::ScopedJuceInitialiser_GUI init;try{periodChange();boundedBurst();traceRecent();concurrentFreeze();}catch(const std::exception&e){check(false,e.what());}std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;}
