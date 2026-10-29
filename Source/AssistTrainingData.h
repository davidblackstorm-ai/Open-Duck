#pragma once
#include "LocalAssistRender.h"
#include "PluginProcessor.h"
#include <random>
#include <vector>

namespace odx::ai::training
{
struct Scene
{
    std::vector<float> main, side;
    double rate=48000,bpm=145;
    int seed=0,index=0,family=0,split=0;
    CaptureView view() const {return {main.data(),main.data(),side.data(),side.data(),(int)main.size(),rate,bpm};}
};
inline Scene synth(int sceneId,int mode,int seed=32030)
{
    Scene s;s.seed=seed+sceneId*7919;s.index=sceneId;s.family=sceneId%12;s.split=s.family<8?0:(s.family<10?1:2);
    std::mt19937 generator((unsigned)s.seed);std::uniform_real_distribution<double> distribution(0,1);
    auto random=[&]{return distribution(generator);};
    s.bpm=100+random()*90;const double seconds=mode==4?10.0:5.0;
    const int count=(int)(seconds*s.rate);s.main.resize((size_t)count);s.side.resize((size_t)count);
    const double bassHz=32+random()*160,kickHz=35+random()*100,tail=.045+random()*.32;
    const double bassAmp=.08+random()*.32,kickAmp=.12+random()*.72;
    const double beat=60/s.bpm;
    std::array<bool,64> pattern{};for(auto &step:pattern)step=random()>.25;pattern[0]=true;
    for(int i=0;i<count;++i)
    {
        const double t=i/s.rate,beatTime=std::fmod(t,beat);const int step=(int)(t/beat)%64;
        const double pitch=bassHz*std::pow(2.0,((step/4)%3==1?7.0:0.0)/12.0);
        double bass=std::sin(2*juce::MathConstants<double>::pi*pitch*t);
        // Family-specific synthesis excludes test-family waveform/decay combinations.
        if(s.family%3==1)bass+=.35*std::sin(6*juce::MathConstants<double>::pi*pitch*t);
        if(s.family%3==2)bass=std::tanh(2.5*bass+.2*std::sin(2*juce::MathConstants<double>::pi*pitch*2.03*t));
        const double bassEnvelope=.65+.35*std::sin(2*juce::MathConstants<double>::pi*(t/beat)*.5+.4);
        s.main[(size_t)i]=(float)(bassAmp*bass*bassEnvelope);
        if(pattern[(size_t)step])
        {
            const double integrated=kickHz*beatTime+(.6*beat)*(1-std::exp(-beatTime/(.01+tail*.08)));
            const double tonal=std::sin(2*juce::MathConstants<double>::pi*integrated)*std::exp(-beatTime/tail);
            const double noise=(random()*2-1)*std::exp(-beatTime/.004)*(.1+.3*(s.family%2));
            const double decay=s.family>=10?std::exp(-std::pow(beatTime/tail,1.5)):1;
            s.side[(size_t)i]=(float)(kickAmp*(tonal+noise)*decay);
        }
    }
    return s;
}
inline void set(Context &c,const std::string &id,float value)
{
    for(int i=0;i<c.frozenCount;++i)if(c.frozen[(size_t)i].id==id){c.frozen[(size_t)i].value=value;return;}
    if(c.frozenCount<(int)c.frozen.size())c.frozen[(size_t)c.frozenCount++]={id,value};
}
inline Context context(int mode,const Scene &scene)
{
    auto p=std::make_unique<OpenDuckProcessor>();
    // Every source family contains both INIT and valid edited factory contexts.
    // These are input contexts, never target labels: actual DSP search chooses labels.
    if((scene.index/12)%2)
    {
        std::mt19937 rng((unsigned)(scene.seed+mode*100003));std::uniform_real_distribution<float> unit(0,1);
        auto curve=p->getCurveSnapshot(mode);curve.shape=1+(int)(rng()%15);curve.amount=50+50*unit(rng);
        curve.phase=-15+30*unit(rng);curve.length=.4f+.6f*unit(rng);curve.tension=-60+120*unit(rng);
        curve.attack=.1f+39.9f*unit(rng);curve.release=35+415*unit(rng);p->setCurveSnapshot(mode,curve);
        p->setSectorParameter(mode,"character",unit(rng));p->setSectorParameter(mode,"smooth",10*unit(rng));
        if(mode==4)for(int step=0;step<16;++step){auto*parameter=p->state.getParameter("step"+juce::String(step));parameter->setValueNotifyingHost(unit(rng)>.2f?1.f:0.f);}
    }
    auto c=makeContext(*p,mode,1);
    c.sampleRate=scene.rate;c.bpm=scene.bpm;c.mix=100;c.startPpq=0;c.hostPositionAvailable=true;c.synced=true;
    c.trigger=mode==4?0:(scene.seed%2?1:0);
    set(c,"mix",100);set(c,"sync",1);set(c,OpenDuckProcessor::sectorParameterId("trigger",mode).toStdString(),(float)c.trigger);
    c.baseline.curve.modern3=true;
    return c;
}
inline Candidate randomCandidate(const Candidate &baseline,int mode,int trial,std::mt19937 &random,const Observation &observation,int bankRotation=0)
{
    std::uniform_real_distribution<float> u(0,1);auto values=encode(baseline,mode);
    values[0]=.35f+.65f*u(random);values[1]=.42f+.16f*u(random);values[2]=.3f+.7f*u(random);
    values[3]=u(random);values[4]=u(random)*.25f;values[5]=.05f+.85f*u(random);values[6]=u(random)*.4f;values[7]=u(random);
    values[8]=.2f+.8f*u(random);values[9]=u(random)*.7f;values[10]=u(random)*.5f;
    if(mode==2){values[11]=u(random);values[12]=u(random);}
    if(mode==4)for(int step=0;step<16;++step)values[(size_t)(11+step)]=(trial%3==0?observation.features[(size_t)(34+step)]>.06f:u(random)>.25f)?1.f:0.f;
    for(int n=0;n<16;++n)values[(size_t)(controlOutputs+n)]=n==(trial*5+mode*3+bankRotation)%16?1.f:0.f;
    return decode(values,mode,baseline);
}
}
