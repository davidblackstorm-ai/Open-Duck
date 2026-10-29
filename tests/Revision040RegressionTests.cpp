#include "PluginProcessor.h"
#include "TemplateGeometry.h"
#include <iostream>
#include <chrono>
namespace {
int passed=0,failed=0;
void check(bool value,const char* text){std::cout<<(value?"PASS ":"FAIL ")<<text<<std::endl;value?++passed:++failed;}
void set(OpenDuckProcessor& p,const char* id,float v){auto* a=p.state.getParameter(id);a->setValueNotifyingHost(a->convertTo0to1(v));}
class Clock final:public juce::AudioPlayHead {
public:int64_t sample=0;double rate=48000;
juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;p.setBpm(120);p.setPpqPosition(sample/rate*2);p.setTimeInSamples(sample);p.setIsPlaying(true);return p;}
};
void ui(){auto p=std::make_unique<OpenDuckProcessor>();std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
    std::array<juce::TextButton*,3> triggers{};std::array<juce::Slider*,3> sliders{};
    for(int i=0;i<e->getNumChildComponents();++i){auto* c=e->getChildComponent(i);if(auto* b=dynamic_cast<juce::TextButton*>(c)){const auto name=b->getButtonText();if(name=="SYNC")triggers[0]=b;if(name=="MIDI")triggers[1]=b;if(name=="AUDIO")triggers[2]=b;}if(auto* k=dynamic_cast<juce::Slider*>(c)){if(k->getName()=="position4")sliders[0]=k;if(k->getName()=="duration4")sliders[1]=k;if(k->getName()=="curveTension0")sliders[2]=k;}}
    bool exclusive=std::all_of(triggers.begin(),triggers.end(),[](auto* b){return b!=nullptr;});
    if(exclusive)for(int i=0;i<3;++i){triggers[(size_t)i]->onClick();int count=0;for(auto* b:triggers)count+=b->getToggleState()?1:0;exclusive&=count==1&&triggers[(size_t)i]->getToggleState();}
    check(exclusive,"SYNC MIDI AUDIO clicks remain mutually exclusive");
    bool visualExclusive=true;for(int selected=0;selected<3;++selected){triggers[(size_t)selected]->onClick();auto image=e->createComponentSnapshot(e->getLocalBounds(),true);
        for(int other=0;other<3;++other)if(other!=selected){auto area=triggers[(size_t)other]->getBounds().expanded(2).getIntersection(image.getBounds());for(int y=area.getY();y<area.getBottom();++y)for(int x=area.getX();x<area.getRight();++x){auto colour=image.getPixelAt(x,y);if(colour.getRed()<100&&colour.getGreen()>180&&colour.getBlue()>180)visualExclusive=false;}}
    }
    check(visualExclusive,"inactive trigger buttons contain no cyan pixels baked into the reference");

    bool attached=std::all_of(sliders.begin(),sliders.end(),[](auto* s){return s!=nullptr;});
    const char* ids[]={"position4","duration4","curveTension0"};
    if(attached)for(int i=0;i<3;++i)for(int j=0;j<=100;++j){auto* slider=sliders[(size_t)i];const float v=float(slider->getMinimum()+(slider->getMaximum()-slider->getMinimum())*j/100.);slider->setValue(v,juce::sendNotificationSync);attached&=std::abs(p->value(ids[i])-v)<1e-3;}
    check(attached,"303 native slider changes reach host parameters through attachments");
    bool pixels=true;int samples=0;
    for(int width:{864,1296,1728,2592}) {e->setSize(width,juce::roundToInt(width*912.f/1728));const float s=width/1728.f;
        for(int n=0;n<3;++n)for(float u:{0.f,.5f,1.f}){auto* slider=sliders[(size_t)n];slider->setValue(slider->getMinimum()+u*(slider->getMaximum()-slider->getMinimum()),juce::sendNotificationSync);auto shot=e->createComponentSnapshot(slider->getBounds(),true);
            const float d=float(juce::jmin(slider->getWidth(),slider->getHeight()))-8*s,r=d*.5f-11*s,stroke=juce::jmax(2.f,d*.022f);const auto a=odx::template4::Rotary::from(u,0,1);const auto c=juce::Point<float>(slider->getWidth()*.5f,slider->getHeight()*.5f);
            for(int y=0;y<shot.getHeight();++y)for(int x=0;x<shot.getWidth();++x){const float dx=x+.5f-c.x,dy=y+.5f-c.y,radius=std::hypot(dx,dy);if(std::abs(radius-r)>stroke*.7f)continue;const auto colour=shot.getPixelAt(x,y);if(colour.getGreen()<170||colour.getBlue()<170||colour.getRed()>100)continue;++samples;float angle=std::atan2(dx,-dy);while(angle<a.start)angle+=odx::template4::pi*2;const float tolerance=3.f/juce::jmax(20.f,r);pixels&=u>0&&angle<=a.angle+tolerance&&angle>=a.start-tolerance;}
        }}
    std::cout<<"MEASURE inspected-cyan-annulus-pixels "<<samples<<std::endl;check(pixels&&samples>100,"rendered LED pixels stay inside active angular sector at four scales");
}
void stress(){constexpr int instances=4,seconds=120,blockSize=256;std::array<std::unique_ptr<OpenDuckProcessor>,instances> p;std::array<Clock,instances> clocks;for(int n=0;n<instances;++n){p[(size_t)n]=std::make_unique<OpenDuckProcessor>();p[(size_t)n]->setPlayHead(&clocks[(size_t)n]);p[(size_t)n]->prepareToPlay(48000,blockSize);set(*p[(size_t)n],"position4",20.f+n*15);}
    juce::AudioBuffer<float> block(2,blockSize);juce::MidiBuffer midi;bool finite=true,independent=true;double peak=0;auto began=std::chrono::steady_clock::now();
    for(int64_t at=0;at<int64_t(seconds)*48000;at+=blockSize){for(int n=0;n<instances;++n){auto& processor=*p[(size_t)n];if(n==0&&at%8192==0)set(processor,"position4",float((at/8192)%101));for(int ch=0;ch<2;++ch)for(int i=0;i<blockSize;++i)block.setSample(ch,i,.15f*std::sin(float((at+i)*.009+n*.25)));clocks[(size_t)n].sample=at;processor.processBlock(block,midi);for(int ch=0;ch<2;++ch)for(int i=0;i<blockSize;++i){const float y=block.getSample(ch,i);finite&=std::isfinite(y);peak=juce::jmax(peak,double(std::abs(y)));}if(n>0)independent&=std::abs(processor.value("position4")-(20+n*15))<1e-4;}
        if(at%(48000*8)==0)std::cout<<"PROGRESS stress "<<at/48000<<"/120 seconds x4"<<std::endl;
    }
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();std::cout<<"MEASURE 4 instances x120s audio, peak "<<peak<<", offline seconds "<<wall<<std::endl;check(finite&&peak<.151,"four instances remain finite under 120s host parameter automation");check(independent,"long automation leaves other instance states independent");
}
void ai(int captureSeconds=5){auto p=std::make_unique<OpenDuckProcessor>();auto buses=p->getBusesLayout();buses.inputBuses.set(1,juce::AudioChannelSet::stereo());check(p->setBusesLayout(buses),"stereo external sidechain bus accepted");Clock clock;p->setPlayHead(&clock);p->setSectorParameter(0,"trigger",1);p->prepareToPlay(48000,256); if(captureSeconds==10)set(*p,"curveAmount0",20);
    check(p->startLocalAssistCapture(0,captureSeconds),"local assistant arms real five-second capture");const auto before=p->getCurveSnapshot(0);const float mix=p->value("position4"),duration=p->value("duration4");
    juce::AudioFormatManager formats;formats.registerBasicFormats();juce::File root=juce::File::getCurrentWorkingDirectory();std::unique_ptr<juce::AudioFormatReader> bass(formats.createReaderFor(root.getChildFile("tests/fixtures/audio/LAB-Bass-145.wav"))),kick(formats.createReaderFor(root.getChildFile("tests/fixtures/audio/LAB-Kick-145.wav")));
    if(!bass||!kick){check(false,"PCM fixtures readable");return;}
    juce::AudioBuffer<float> main(2,(int)bass->lengthInSamples),side(2,(int)kick->lengthInSamples);bass->read(&main,0,main.getNumSamples(),0,true,true);kick->read(&side,0,side.getNumSamples(),0,true,true);
    juce::AudioBuffer<float> block(4,256);juce::MidiBuffer midi;for(int at=0;at<48000*(captureSeconds+1);at+=256){for(int i=0;i<256;++i)for(int ch=0;ch<2;++ch){const int index=(at+i)%main.getNumSamples();block.setSample(ch,i,main.getSample(ch,index));block.setSample(ch+2,i,side.getSample(ch,index%side.getNumSamples()));}clock.sample=at;p->processBlock(block,midi);}
    const auto began=std::chrono::steady_clock::now();auto status=p->localAssistStatus(0);while((status.state==odx::ai::State::capturing||status.state==odx::ai::State::analyzing)&&std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<180){juce::Thread::sleep(100);status=p->localAssistStatus(0);}
    std::cout<<"MEASURE AI state "<<int(status.state)<<" captured "<<status.capturedSeconds<<" valid hits "<<status.validHits<<" message "<<status.message<<std::endl;
    const auto after=p->getCurveSnapshot(0);check(before.shape==after.shape&&before.amount==after.amount&&before.tension==after.tension&&mix==p->value("position4")&&duration==p->value("duration4"),"analysis never applies a proposal automatically");
    odx::ai::Proposal proposal;const bool ready=p->localAssistProposal(0,proposal)&&proposal.improved;
    if(ready){check(proposal.result.finite&&proposal.result.objective<proposal.baseline.objective,"ready proposal has measured finite improvement");check(p->applyLocalAssist(0),"explicit APPLY accepted measured proposal");set(*p,"position4",70);check(p->canUndoLocalAssist(0)&&p->undoLocalAssist(0),"AI Undo remains available after manual edits");}
    else check(status.state==odx::ai::State::insufficient||status.state==odx::ai::State::ready,"unsupported or non-improving capture abstains explicitly");
    p->cancelLocalAssist(0);check(!p->applyLocalAssist(0),"Cancel prevents stale proposal application");
}
}
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;std::string mode=argc>1?argv[1]:"ui";if(mode=="stress")stress();else if(mode=="ai"||mode=="ai10")ai(mode=="ai10"?10:5);else ui();std::cout<<"RESULT Template040-"<<mode<<" "<<passed<<" passed, "<<failed<<" failed"<<std::endl;return failed?1:0;}
