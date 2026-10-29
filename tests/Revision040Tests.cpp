#include "PluginProcessor.h"
#include <juce_cryptography/juce_cryptography.h>
#include "TemplateGeometry.h"
#include <iostream>
#include <set>

namespace {
int passes=0,failures=0;
void check(bool v,const char* name){std::cout<<(v?"PASS ":"FAIL ")<<name<<std::endl;v?++passes:++failures;}
void set(OpenDuckProcessor& p,const char* id,float v){auto* parameter=p.state.getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(v));}
class Clock final:public juce::AudioPlayHead{
public:
    int64_t sample=0;double rate=48000;
    juce::Optional<PositionInfo> getPosition()const override{PositionInfo pos;pos.setBpm(120);pos.setPpqPosition(sample/rate*2);pos.setTimeInSamples(sample);pos.setIsPlaying(true);return pos;}
};
void preview(OpenDuckProcessor& p,const juce::File& folder,int width,const juce::String& suffix){
    p.state.state.setProperty("templateAiView",false,nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(width,juce::roundToInt(width*912.f/1728));
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true);juce::PNGImageFormat png;auto stream=folder.getChildFile("template-"+suffix+".png").createOutputStream();if(stream){stream->setPosition(0);stream->truncate();png.writeImageToStream(image,*stream);}
    bool geometry=true;for(int i=0;i<editor->getNumChildComponents();++i){auto* c=editor->getChildComponent(i);if(c->isVisible())geometry&=editor->getLocalBounds().contains(c->getBounds());}
    check(geometry,"visible native component bounds stay inside template");
    for(int i=0;i<editor->getNumChildComponents();++i)if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i)))if(button->getButtonText()=="AI")button->onClick();
    auto overlay=editor->createComponentSnapshot(editor->getLocalBounds(),true);auto overlayStream=folder.getChildFile("template-ai-"+suffix+".png").createOutputStream();if(overlayStream){overlayStream->setPosition(0);overlayStream->truncate();png.writeImageToStream(overlay,*overlayStream);}

}
}
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI gui;const auto folder=juce::File(argc>1?argv[1]:".");folder.createDirectory();
    bool angles=true,linear=true,inverse=true;
    for(int i=0;i<=10000;++i){const float u=i/10000.f;const auto a=odx::template4::Rotary::from(u,0,1);
        angles&=std::abs(a.angle-(a.start+u*(a.end-a.start)))<1e-6f;
        linear&=std::abs(odx::template4::toDisplay(.5f,u*100,0)-u)<1e-6f;
        const float x=.2f+u*.6f;inverse&=std::abs(odx::template4::toSource(odx::template4::toDisplay(x,60,15),60,15)-x)<1e-6f;}
    check(angles,"10001 drag positions share exact arc and pointer angular mapping");check(linear,"MIX moves linearly over one cycle without changing gain");check(inverse,"position and duration forward/inverse geometry agree");
    bool archEndpoints=true;for(float tension:{-100.f,0.f,100.f}){OpenDuckProcessor::CurveSnapshot arch;arch.template4=true;arch.shape=6;arch.tension=tension;archEndpoints&=OpenDuckProcessor::effectiveCurveSample(arch,1,.5,0,.5f)<1e-6;}
    check(archEndpoints,"Arch endpoint remains closed under fractional curvature powers");
    auto p=std::make_unique<OpenDuckProcessor>();check(p->value("template4")>.5f,"new instance uses template revision");
    auto c=p->getCurveSnapshot(0);check(c.shape==0&&!c.custom&&c.amount==100,"new instance starts Soft Duck INIT");
    juce::MemoryBlock initial;p->getStateInformation(initial);folder.getChildFile("Template-init.odpx").replaceWithData(initial.getData(),initial.getSize());
    const auto f=p->getScopeFrame(0);check(f.bins==0&&f.generation==0,"no fabricated waveform at startup");
    const float before=p->previewCurveGain(0,.3f);set(*p,"position4",60);const float shifted=p->previewCurveGain(0,.4f);check(std::abs(before-shifted)<1e-5,"MIX horizontal translation matches preview samples");
    set(*p,"duration4",30);const float centred=p->previewCurveGain(0,.5f+.1f+(.3f-.5f)*1.3f);check(std::abs(before-centred)<1e-5,"PHASE changes duration around centre independently of MIX");
    set(*p,"curveTension0",35);check(std::abs(p->value("position4")-60)<1e-4&&std::abs(p->value("duration4")-30)<1e-4,"CURVE leaves position and duration unchanged");
    juce::MemoryBlock state;p->getStateInformation(state);folder.getChildFile("QA-StateRoundtrip.odpx").replaceWithData(state.getData(),state.getSize());auto other=std::make_unique<OpenDuckProcessor>();other->setStateInformation(state.getData(),(int)state.getSize());
    check(std::abs(other->value("position4")-60)<1e-4&&std::abs(other->value("duration4")-30)<1e-4&&std::abs(other->value("curveTension0")-35)<1e-4,"schema4 exact parameter roundtrip");set(*other,"position4",10);check(std::abs(p->value("position4")-60)<1e-4,"independent instances do not share position");
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());xml->setAttribute("schema",3);juce::MemoryBlock legacy;juce::AudioProcessor::copyXmlToBinary(*xml,legacy);other->setStateInformation(legacy.getData(),(int)legacy.getSize());check(other->value("template4")==0,"schema1-3 restore legacy MIX interpretation");
    set(*p,"position4",50);set(*p,"duration4",0);set(*p,"curveTension0",0);
    preview(*p,folder,864,"50pct");preview(*p,folder,1296,"75pct");preview(*p,folder,1728,"100pct");preview(*p,folder,2592,"150pct");
    juce::String catalog="id,name,shape,position_percent,preset_length,curve_percent,amount_percent,envelope_sha256\n";
    std::set<std::string> fingerprints;std::map<std::string,int> ids;
    for(const auto& factory:OpenDuckProcessor::factoryPresets()){
        p->applyTemplatePreset(factory.id);
        std::string key;for(int i=0;i<=512;++i){int q=juce::roundToInt(p->previewCurveGain(0,i/512.f)*1000000);key.append(reinterpret_cast<const char*>(&q),sizeof(q));}const auto current=p->getCurveSnapshot(0);catalog+=juce::String(factory.id+1)+",\""+OpenDuckProcessor::templatePresetName(factory.id)+"\","+juce::String(current.shape)+","+juce::String(p->value("position4"),3)+","+juce::String(current.length,3)+","+juce::String(current.tension,3)+","+juce::String(current.amount,3)+","+juce::SHA256(key.data(),key.size()).toHexString()+"\n";
        if(!fingerprints.insert(key).second)std::cout<<"DUPLICATE "<<factory.id<<" / "<<ids[key]<<std::endl;else ids[key]=factory.id;
    }
    folder.getChildFile("Factory-Presets-700.csv").replaceWithText(catalog);
    std::cout<<"MEASURE distinct-preset-envelopes "<<fingerprints.size()<<"/700\n";check(fingerprints.size()==700,"700 presets have distinct quantized DSP envelopes");
    p=std::make_unique<OpenDuckProcessor>();Clock clock;p->setPlayHead(&clock);p->prepareToPlay(48000,256);const int latency=p->getLatencySamples();juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    double maxError=0,maxJump=0;double expectedSmooth=1;float previous=.2f;int measured=0;const int total=48000*8;
    for(int at=0;at<total;at+=256){const int n=juce::jmin(256,total-at);block.setSize(2,n,false,false,true);for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)block.setSample(ch,i,.2f);clock.sample=at;p->processBlock(block,midi);
        for(int i=0;i<n;++i){const float y=block.getSample(0,i); const float phase=std::fmod((at+i-latency+48000)/24000.f,1.f); const float target=p->previewCurveGain(0,phase); expectedSmooth=target+(expectedSmooth-target)*std::exp(-1.0/48); if(at+i>latency+48000){const float expected=.2f*float(expectedSmooth);maxError=std::max(maxError,double(std::abs(y-expected)));maxJump=std::max(maxJump,double(std::abs(y-previous)));++measured;}previous=y;}}
    std::cout<<"MEASURE latency "<<latency<<" max-envelope-error "<<maxError<<" max-step "<<maxJump<<" samples "<<measured<<std::endl;
    check(maxError<.015,"real DSP gain matches visible envelope with declared latency and smoothing");check(maxJump<.01,"constant-input output has controlled envelope transitions");
    std::cout<<"RESULT Template040 "<<passes<<" passed, "<<failures<<" failed"<<std::endl;return failures?1:0;
}


