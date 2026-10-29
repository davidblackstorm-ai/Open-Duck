#include "LocalAssistRender.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <vector>

namespace odx::ai
{
namespace
{
void parameter(OpenDuckProcessor &p,const std::string &id,float value)
{
    if(auto *v=p.state.getParameter(juce::String(id)))
        v->setValueNotifyingHost(v->convertTo0to1(value));
}
class ClipPlayHead final:public juce::AudioPlayHead
{
  public:
    double rate=48000,bpm=120,startPpq=0;
    std::int64_t sample=0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;p.setBpm(bpm);p.setTimeInSamples(sample);p.setTimeInSeconds(sample/rate);
        p.setPpqPosition(startPpq+sample/rate*bpm/60);p.setIsPlaying(true);return p;
    }
};
}
Context makeContext(const OpenDuckProcessor &p,int mode,std::uint64_t revision)
{
    Context c;c.revision=revision;c.sampleRate=p.getSampleRate()>0?p.getSampleRate():48000;c.bpm=p.bpm.load();c.mix=p.value("mix");
    c.trigger=(int)p.value(OpenDuckProcessor::sectorParameterId("trigger",mode));c.synced=p.value("sync")>.5f;c.baseline.curve=p.getCurveSnapshot(mode);
    for(int band=0;band<3;++band)c.bandCurves[(size_t)band]=p.getBandCurveSnapshot(mode,band);
    // Host playheads are queried by processBlock only; first-sample metadata arrives
    // through Controller::push, never by reading a live playhead on the GUI thread.
    for(auto *parameter:p.getParameters())
        if(auto *r=dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            const auto key=r->getParameterID().toStdString();const float value=r->convertFrom0to1(r->getValue());
            if(c.frozenCount<(int)c.frozen.size())c.frozen[(size_t)c.frozenCount++]={key,value};
            const std::string suffix=std::to_string(mode);
            if(key.rfind("v3",0)==0 && key.size()>suffix.size() && key.compare(key.size()-suffix.size(),suffix.size(),suffix)==0)
            {
                // Only parameters the trained decoder may change are proposal fields.
                const bool allowed=key.rfind("v3Smooth",0)==0 || key.rfind("v3Character",0)==0 ||
                    key.rfind("v3LowAmount",0)==0 || key.rfind("v3MidAmount",0)==0 || key.rfind("v3HighAmount",0)==0 ||
                    key.rfind("v3Cross",0)==0 || key.rfind("v3Bands",0)==0 || key.rfind("v3Swing",0)==0 ||
                    key.rfind("v3Threshold",0)==0 || key.rfind("v3Hysteresis",0)==0 || key.rfind("v3Holdoff",0)==0;
                if(allowed && c.baseline.controlCount<(int)c.baseline.controls.size())c.baseline.controls[(size_t)c.baseline.controlCount++]={key,value};
            }
        }
    if(mode==4)for(int step=0;step<16;++step)c.baseline.controls[(size_t)c.baseline.controlCount++]={"step"+std::to_string(step),p.value(("step"+std::to_string(step)).c_str())};
    return c;
}
static bool renderImpl(int mode,const Candidate &candidate,const CaptureView &capture,const Context &context,
                     Metrics &metrics,const std::atomic<bool> &cancel,StereoRenderAudit *audit)
{
    if(mode<0 || mode>=6 || capture.samples<=0 || !capture.mainL || cancel.load())return false;
    // No recorded MIDI events are available in CaptureView; never render a false unity candidate.
    if(context.trigger==2)return false;
    auto p=std::make_unique<OpenDuckProcessor>();
    for(int i=0;i<context.frozenCount && i<(int)context.frozen.size();++i)
        if(context.frozen[(size_t)i].id.rfind("v3Group",0)!=0)parameter(*p,context.frozen[(size_t)i].id,context.frozen[(size_t)i].value);
    for(int i=0;i<6;++i){parameter(*p,"on"+std::to_string(i),i==mode?1.0f:0.0f);parameter(*p,"solo"+std::to_string(i),0);}
    parameter(*p,"mix",context.mix);parameter(*p,"bypass",0);
    for(int band=0;band<3;++band)p->setBandCurveSnapshot(mode,band,context.bandCurves[(size_t)band]);
    auto qualityContext=context;
    if(context.trigger==0){p->setCurveSnapshot(mode,context.baseline.curve);p->bpm.store((float)capture.bpm);
        qualityContext.intentCycleSeconds=p->cycleSeconds(mode);qualityContext.hasIntentProfile=qualityContext.intentCycleSeconds>0;
        for(size_t i=0;i<qualityContext.intentGain.size();++i)qualityContext.intentGain[i]=p->previewCurveGain(mode,i/255.f);}
    p->setCurveSnapshot(mode,candidate.curve);
    for(int i=0;i<candidate.controlCount && i<(int)candidate.controls.size();++i)parameter(*p,candidate.controls[(size_t)i].id,candidate.controls[(size_t)i].value);
    auto layout=p->getBusesLayout();
    if(layout.inputBuses.size()>1){layout.inputBuses.set(1,juce::AudioChannelSet::stereo());p->setBusesLayout(layout);}
    constexpr int block=256;
    ClipPlayHead play;play.rate=capture.sampleRate;play.bpm=capture.bpm;play.startPpq=context.startPpq;p->setPlayHead(&play);
    p->prepareToPlay(capture.sampleRate,block);
    const int latency=p->getLatencySamples(),warm=latency+2048;
    std::vector<float> left((size_t)capture.samples),right((size_t)capture.samples);
    juce::AudioBuffer<float> buffer(4,block);juce::MidiBuffer midi;
    const int total=warm+capture.samples+latency;
    for(int position=0;position<total;position+=block)
    {
        if(cancel.load())return false;
        const int n=std::min(block,total-position);buffer.clear();midi.clear();
        for(int i=0;i<n;++i)
        {
            const int source=position+i-warm;
            if(source>=0 && source<capture.samples)
            {
                buffer.setSample(0,i,capture.mainL[source]);buffer.setSample(1,i,capture.mainR?capture.mainR[source]:capture.mainL[source]);
                if(capture.sideL)buffer.setSample(2,i,capture.sideL[source]);
                if(capture.sideR)buffer.setSample(3,i,capture.sideR[source]);else if(capture.sideL)buffer.setSample(3,i,capture.sideL[source]);
            }
        }
        play.sample=position-warm;
        // A final short logical block is padded with zeros; only valid samples are collected.
        p->processBlock(buffer,midi);
        for(int i=0;i<n;++i)
        {
            const int target=position+i-warm-latency;
            if(target>=0 && target<capture.samples){left[(size_t)target]=buffer.getSample(0,i);right[(size_t)target]=buffer.getSample(1,i);}
        }
    }
    p->releaseResources();p->setPlayHead(nullptr);
    auto metricCapture=capture;
    if(context.trigger==3){metricCapture.sideL=capture.mainL;metricCapture.sideR=capture.mainR;}
    metrics=measure(metricCapture,left.data(),right.data(),capture.samples,qualityContext,mode);
    if(audit)
    {
        audit->measured=metrics;audit->averageReference=measureAverageReference(metricCapture,left.data(),right.data(),capture.samples);
        for(int i=0;i<capture.samples;++i)audit->maximumChannelDifference=std::max(audit->maximumChannelDifference,(double)std::abs(left[(size_t)i]-right[(size_t)i]));
    }
    return metrics.finite;
}
bool renderCandidate(int mode,const Candidate &candidate,const CaptureView &capture,const Context &context,
                     Metrics &metrics,const std::atomic<bool> &cancel)
{
    return renderImpl(mode,candidate,capture,context,metrics,cancel,nullptr);
}
bool auditCandidateStereo(int mode,const Candidate &candidate,const CaptureView &capture,const Context &context,
                          StereoRenderAudit &audit,const std::atomic<bool> &cancel)
{
    audit={};return renderImpl(mode,candidate,capture,context,audit.measured,cancel,&audit);
}
}
