#include "PluginProcessor.h"
#include <iostream>

namespace {
int passed=0,failed=0;
void check(bool ok,const std::string&name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';ok?++passed:++failed;}
void set(OpenDuckProcessor&p,const juce::String&id,float value){auto*q=p.state.getParameter(id);if(!q)throw std::runtime_error("Missing parameter");q->setValueNotifyingHost(q->convertTo0to1(value));}
struct Head:juce::AudioPlayHead{double ppq=0;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setBpm(120.);p.setPpqPosition(ppq);p.setIsPlaying(true);return p;}};
}
int main(){
    std::cout<<std::unitbuf;juce::ScopedJuceInitialiser_GUI init;
    try{
        auto owner=std::make_unique<OpenDuckProcessor>();auto&p=*owner;Head head;p.setPlayHead(&head);
        auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
        if(!p.setBusesLayout(layout))throw std::runtime_error("Stereo SC rejected");
        for(int mode=0;mode<6;++mode){set(p,"on"+juce::String(mode),mode==0?1.f:0.f);p.setSectorParameter(mode,"trigger",0);p.setSectorParameter(mode,"speed",2);}
        constexpr int blockSize=257;constexpr double rate=48000.;p.prepareToPlay(rate,blockSize);
        const int freezeBlock=((54000+p.getLatencySamples()+blockSize-1)/blockSize)*blockSize;
        const int resumeBlock=((freezeBlock+48000+blockSize-1)/blockSize)*blockSize;
        const int sourceChange=resumeBlock-p.getLatencySamples();
        std::array<OpenDuckProcessor::ScopeFrame,6> frozen{},resumed{};
        std::array<bool,6> stable{{true,true,true,true,true,true}},newFrame{};
        for(int start=0;start<resumeBlock+2*24000+1024;start+=blockSize){
            if(start==freezeBlock){for(int mode=0;mode<6;++mode)frozen[(size_t)mode]=p.getScopeFrame(mode);p.setScopeFrozen(true);}
            if(start==resumeBlock)p.setScopeFrozen(false);
            juce::AudioBuffer<float>audio(4,blockSize);juce::MidiBuffer midi;
            for(int i=0;i<blockSize;++i){const float marker=start+i>=sourceChange?.8f:.1f;audio.setSample(0,i,.15f);audio.setSample(1,i,.15f);audio.setSample(2,i,marker);audio.setSample(3,i,-marker);}
            head.ppq=start/rate*2;p.processBlock(audio,midi);
            for(int mode=0;mode<6;++mode){const auto frame=p.getScopeFrame(mode);const auto n=(size_t)mode;
                if(start>=freezeBlock&&start<resumeBlock)stable[n]&=frame.generation==frozen[n].generation&&frame.sideMin==frozen[n].sideMin&&frame.sideMax==frozen[n].sideMax&&std::abs(frame.phase-frozen[n].phase)<1e-5f;
                if(start>=resumeBlock&&!newFrame[n]&&frame.complete&&frame.generation>frozen[n].generation){resumed[n]=frame;newFrame[n]=true;}
            }
        }
        for(int mode=0;mode<6;++mode){const auto n=(size_t)mode;const std::string prefix="mode"+std::to_string(mode)+": ";
            check(frozen[n].complete&&frozen[n].generation>0,prefix+"Freeze begins with a real completed cycle");
            check(stable[n],prefix+"Freeze preserves clicked cursor phase, generation and every stored SC bin during continued audio");
            bool fresh=newFrame[n]&&resumed[n].bins>=510;
            for(int bin=0;bin<512;++bin)fresh&=resumed[n].sideMax[(size_t)bin]>.799f;
            check(fresh,prefix+"first completed cycle after Unfreeze contains only new source samples, without pre-freeze splice");
        }
    }catch(const std::exception&e){check(false,e.what());}
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
