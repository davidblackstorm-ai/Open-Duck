#include "PluginProcessor.h"
#include "LocalAssistRender.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <thread>

namespace
{
int passed = 0, failed = 0, improvements = 0, applications = 0;
using State = odx::ai::State;
void check(bool ok, const juce::String &message)
{
    std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
    if (ok) ++passed; else ++failed;
}
void set(OpenDuckProcessor &p, const juce::String &id, float value)
{
    if (auto *parameter = p.state.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
bool close(float a, float b) { return std::abs(a - b) <= 1e-6f; }
bool sameCurve(const OpenDuckProcessor::CurveSnapshot &a, const OpenDuckProcessor::CurveSnapshot &b)
{
    if (a.count != b.count || a.shape != b.shape || a.grid != b.grid || a.presetId != b.presetId ||
        a.custom != b.custom || a.linear != b.linear || a.edit != b.edit || a.freeDraw != b.freeDraw ||
        a.band != b.band || a.legacy != b.legacy || a.modern3 != b.modern3) return false;
    for (auto pair : {std::pair<float,float>{a.phase,b.phase},{a.length,b.length},{a.tension,b.tension},
                      {a.amount,b.amount},{a.attack,b.attack},{a.release,b.release},{a.focus,b.focus},
                      {a.riseSlope,b.riseSlope}}) if (!close(pair.first,pair.second)) return false;
    for (int i = 0; i < a.count; ++i)
    {
        const auto &x=a.points[(size_t)i], &y=b.points[(size_t)i];
        if (!close(x.x,y.x) || !close(x.y,y.y) || x.segment!=y.segment || x.reversed!=y.reversed || !close(x.curvature,y.curvature)) return false;
    }
    return true;
}
struct Snapshot
{
    std::map<juce::String,float> parameters;
    std::array<OpenDuckProcessor::CurveSnapshot,6> curves;
    std::array<std::array<OpenDuckProcessor::CurveSnapshot,3>,6> bands;
    explicit Snapshot(OpenDuckProcessor &p)
    {
        for (auto *parameter:p.getParameters())
            if (auto *r=dynamic_cast<juce::RangedAudioParameter *>(parameter)) parameters[r->getParameterID()]=r->getValue();
        for (int m=0;m<6;++m) { curves[(size_t)m]=p.getCurveSnapshot(m); for(int b=0;b<3;++b) bands[(size_t)m][(size_t)b]=p.getBandCurveSnapshot(m,b); }
    }
    bool equals(OpenDuckProcessor &p) const
    {
        for (const auto &entry:parameters) { const auto *r=p.state.getParameter(entry.first); if(r==nullptr || !close(r->getValue(),entry.second)) return false; }
        for(int m=0;m<6;++m) { if(!sameCurve(curves[(size_t)m],p.getCurveSnapshot(m)))return false;for(int b=0;b<3;++b)if(!sameCurve(bands[(size_t)m][(size_t)b],p.getBandCurveSnapshot(m,b)))return false; }
        return true;
    }
    bool isolatedApply(OpenDuckProcessor &p,int mode,const odx::ai::Proposal &proposal) const
    {
        std::set<juce::String> allowed;
        for(int k=0;k<proposal.after.controlCount;++k) allowed.insert(juce::String(proposal.after.controls[(size_t)k].id));
        for(const auto *base:{"shape","phase","length","tension","amount","attack","release","grid","band","focus"}) allowed.insert(OpenDuckProcessor::curveParameterId(base,mode));
        allowed.insert(OpenDuckProcessor::sectorParameterId("riseSlope",mode));
        for(const auto &entry:parameters) if(allowed.find(entry.first)==allowed.end()) { auto *r=p.state.getParameter(entry.first);if(r==nullptr || !close(r->getValue(),entry.second))return false; }
        for(int m=0;m<6;++m) { if(m!=mode && !sameCurve(curves[(size_t)m],p.getCurveSnapshot(m)))return false;for(int b=0;b<3;++b)if(!sameCurve(bands[(size_t)m][(size_t)b],p.getBandCurveSnapshot(m,b)))return false; }
        return true;
    }
};
class HostClock final : public juce::AudioPlayHead
{
  public:
    double rate=8000, tempo=120;
    int64_t sample=0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;p.setBpm(tempo);p.setTimeInSamples(sample);p.setTimeInSeconds((double)sample/rate);
        p.setPpqPosition((double)sample/rate*tempo/60);p.setIsPlaying(true);return p;
    }
};
struct Fixture
{
    std::unique_ptr<OpenDuckProcessor> processor=std::make_unique<OpenDuckProcessor>();
    HostClock clock;
    int mode=0;
    bool sidechain=true, oppositeSidechain=false;
    explicit Fixture(int m,double rate=8000,int trigger=0) : mode(m)
    {
        clock.rate=rate;auto &p=*processor;
        auto layout=p.getBusesLayout();if(layout.inputBuses.size()>1)layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
        if(!p.setBusesLayout(layout))throw std::runtime_error("Stereo main/sidechain fixture layout rejected");
        p.setPlayHead(&clock);p.setRateAndBufferSizeDetails(rate,127);set(p,"sync",1);set(p,"mix",100);
        for(int i=0;i<6;++i){set(p,"on"+juce::String(i),i==m?1.0f:0.0f);set(p,"solo"+juce::String(i),0);}
        set(p,"selected",(float)((m+2)%6));p.setSectorParameter(m,"group",0);p.setSectorParameter(m,"trigger",(float)trigger);
        p.setSectorParameter(m,"smooth",1);p.setSectorParameter(m,"character",.5f);
        auto c=p.getCurveSnapshot(m);c.amount=0;c.phase=0;c.attack=10;c.release=150;c.custom=false;c.shape=0;c.count=0;c.modern3=true;
        p.setCurveSnapshot(m,c,"AI bridge baseline without attenuation");p.prepareToPlay(rate,127);
    }
    ~Fixture() { processor->cancelLocalAssist(mode);processor->setPlayHead(nullptr); }
    void feed(int samples)
    {
        constexpr double pi=juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> audio(4,samples);juce::MidiBuffer midi;
        for(int i=0;i<samples;++i)
        {
            const double t=(clock.sample+i)/clock.rate, beat=std::fmod(t,60/clock.tempo);
            const double kick=std::sin(2*pi*(51*beat+23*std::exp(-beat*32)))*std::exp(-beat*36);
            const float side=(float)(.85*kick+.18*std::sin(2*pi*1460*t)*std::exp(-beat*340));
            const float main=(float)(.25*std::sin(2*pi*55*t)+.10*std::sin(2*pi*110*t)+.07*std::sin(2*pi*830*t));
            audio.setSample(0,i,main);audio.setSample(1,i,main*.97f);audio.setSample(2,i,sidechain?side:0);audio.setSample(3,i,sidechain?side*(oppositeSidechain?-1.0f:.94f):0);
        }
        processor->processBlock(audio,midi);clock.sample+=samples;
    }
    void feedCount(int samples)
    {
        for(int done=0;done<samples;) { const int n=juce::jmin(127,samples-done);feed(n);done+=n; }
    }
};
bool busy(State s) { return s==State::capturing || s==State::analyzing; }
odx::ai::Status waitFor(Fixture &fixture,int timeoutSeconds=60)
{
    const auto start=std::chrono::steady_clock::now();int last=-1;
    for(;;)
    {
        auto s=fixture.processor->localAssistStatus(fixture.mode);
        if(!busy(s.state))return s;
        const auto seconds=(int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count();
        if(seconds>=timeoutSeconds){fixture.processor->cancelLocalAssist(fixture.mode);check(false,"real analysis worker completed within60seconds");return fixture.processor->localAssistStatus(fixture.mode);}
        if(seconds/5!=last){last=seconds/5;std::cout<<"WORKER mode="<<fixture.mode<<" elapsed="<<seconds<<"s progress="<<s.analysisProgress<<'\n';}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
bool capture(Fixture &fixture,int seconds,bool alreadyArmed=false)
{
    auto &p=*fixture.processor;const int count=juce::roundToInt(fixture.clock.rate*seconds);
    const bool started=alreadyArmed || p.startLocalAssistCapture(fixture.mode,seconds);check(started,"real capture started for sector"+juce::String(fixture.mode)+" at"+juce::String(fixture.clock.rate,0)+"Hz / "+juce::String(seconds)+"s");if(!started)return false;
    fixture.feedCount(count-1);const auto partial=p.localAssistStatus(fixture.mode);
    check(partial.state==State::capturing && std::abs(partial.capturedSeconds-(count-1)/fixture.clock.rate)<1e-9 && partial.captureProgress<1,"worker waits for the exact requested sample count rather than elapsed wall time");
    fixture.feed(1);return true;
}
bool measuredStrengthRoundTrip(Fixture &fixture,odx::ai::Proposal &proposal)
{
    auto &p=*fixture.processor;const int mode=fixture.mode;const Snapshot before(p);
    check(!p.applyLocalAssist(mode,.5f) && before.equals(p),
          "a50percent Strength that has not been rendered cannot apply an unverified blend");
    const bool requested=p.requestLocalAssistStrength(mode,.5f);
    check(requested,"the processor queues a real render of the exact50percent Strength");
    if(!requested)return false;
    // A worker may finish between API calls. This never-requested nearby value
    // must be refused both during that race and after a successful50% render.
    check(!p.applyLocalAssist(mode,.51f) && before.equals(p),
          "Apply refuses an unevaluated Strength while the exact-value request is being processed");
    const auto halfStatus=waitFor(fixture);odx::ai::Proposal half;
    const bool halfAvailable=p.localAssistProposal(mode,half);
    check(halfStatus.state==State::ready && halfAvailable && half.targetImproved
              && std::abs(half.evaluatedStrength-.5f)<=1e-5f && half.result.finite
              && sameCurve(half.after.curve,proposal.after.curve) && before.equals(p),
          "real50percent evaluation retains the full target and leaves every live parameter and curve unchanged");
    if(!halfAvailable)return false;
    const auto assessment=odx::ai::assessCandidate(half.baseline,half.result);
    check(half.improved==(assessment.admissible && half.result.objective<half.baseline.objective-1e-6),
          "partial Strength acceptance requires its own measured preservation guards and objective improvement");
    if(!half.improved)
        check(!p.applyLocalAssist(mode,.5f) && half.targetImproved && before.equals(p),
              "a rejected partial Strength cannot apply and does not discard the valid full target");
    const bool fullRequested=p.requestLocalAssistStrength(mode,1);
    check(fullRequested,"returning Strength to100percent queues its real recheck");
    if(!fullRequested)return false;
    const auto fullStatus=waitFor(fixture);odx::ai::Proposal full;
    const bool fullAvailable=p.localAssistProposal(mode,full);
    check(fullStatus.state==State::ready && fullAvailable && full.targetImproved && full.improved
              && std::abs(full.evaluatedStrength-1)<=1e-5f && before.equals(p),
          "the rechecked100percent result is exact and no Strength evaluation changes live audio state");
    if(fullAvailable)proposal=full;
    return fullAvailable;
}
juce::Component *findUi(juce::Component &root,const juce::String &name)
{
    if(root.getName()==name)return &root;
    for(auto *child:root.getChildren())if(auto *found=findUi(*child,name))return found;
    return nullptr;
}
bool queuedStrengthCancellation(Fixture &fixture,odx::ai::Proposal &proposal)
{
    auto &p=*fixture.processor;const int mode=fixture.mode,other=(mode+1)%6,originalView=(int)p.value("selected");
    const Snapshot before(p);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto *view=dynamic_cast<juce::Button *>(findUi(*editor,"mode"+juce::String(mode)));
    if(view&&view->onClick)view->onClick();
    auto *strength=dynamic_cast<juce::Slider *>(findUi(*editor,"assistStrength"));
    auto *cancel=dynamic_cast<juce::Button *>(findUi(*editor,"assistCancel"));
    auto *apply=dynamic_cast<juce::Button *>(findUi(*editor,"assistApply"));
    const bool controls=view&&view->onClick&&strength&&strength->isEnabled()&&cancel&&cancel->onClick&&apply;
    check(controls,"real ready-target editor exposes a usable Strength knob and cancellation controls");
    if(!controls)return false;
    const bool otherStarted=p.startLocalAssistCapture(other,5);
    check(otherStarted,"another sector owns the capture worker while the chosen sector retains its ready target");
    if(!otherStarted)return false;
    strength->setValue(.5,juce::sendNotificationSync);
    check(cancel->isEnabled()&&!apply->isEnabled(),"Cancel is enabled for a locally queued Strength even when another sector owns the worker");
    cancel->onClick();
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    view->onClick(); // Runs the same timer/service path after the debounce expired.
    const auto cancelled=p.localAssistStatus(mode),otherStatus=p.localAssistStatus(other);
    check((cancelled.state==State::stale||cancelled.state==State::cancelled)
              && !p.applyLocalAssist(mode,.5f)&&!p.requestLocalAssistStrength(mode,.5f)&&!apply->isEnabled(),
          "Cancel invalidates a ready target and prevents a delayed Strength retry or later Apply");
    check(otherStatus.state==State::capturing&&otherStatus.capturedSeconds==0,
          "cancelling the chosen queued sector does not cancel another sector's active capture");
    if(auto *original=dynamic_cast<juce::Button *>(findUi(*editor,"mode"+juce::String(originalView))))
        if(original->onClick)original->onClick();
    p.cancelLocalAssist(other);editor.reset();
    check(before.equals(p),"queued cancellation changes no APVTS values,curves or original editor selection");
    bool armed=false;
    for(int attempt=0;attempt<100&&!armed;++attempt)
    {armed=p.startLocalAssistCapture(mode,10);if(!armed)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    if(!capture(fixture,10,armed))return false;
    const auto freshStatus=waitFor(fixture);odx::ai::Proposal fresh;
    const bool freshAvailable=p.localAssistProposal(mode,fresh);
    check(freshStatus.state==State::ready&&freshAvailable&&fresh.targetImproved&&fresh.evaluatedStrength==1&&before.equals(p),
          "an explicit fresh capture after Cancel restores a measured target without any automatic audio changes");
    if(freshAvailable)proposal=fresh;
    return freshAvailable;
}
void analysisAndApply(int mode,int seconds)
{
    Fixture fixture(mode,8000);auto &p=*fixture.processor;const Snapshot baseline(p);
    if(!capture(fixture,seconds))return;
    const auto s=waitFor(fixture);check(baseline.equals(p),"ANALYZE leaves every APVTS value,sector curve and band curve unchanged");
    check(s.state==State::ready && s.mainPresent && s.sidechainPresent && s.validHits>=2 && std::abs(s.capturedSeconds-seconds)<1e-9,"trained worker evaluates actual main/SC audio with captured host PPQ");
    odx::ai::Proposal proposal;const bool available=p.localAssistProposal(mode,proposal);
    check(available && proposal.baseline.finite && proposal.result.finite && proposal.modelVersion!="untrained","proposal exposes finite metrics from the real DSP evaluator and a trained model version");
    if(!available)return;
    std::cout<<"METRICS mode="<<mode<<" capture="<<seconds<<"s baseline="<<proposal.baseline.objective<<" result="<<proposal.result.objective<<" improved="<<proposal.improved<<" hits="<<s.validHits<<'\n';
    check(proposal.improved==(proposal.result.objective<proposal.baseline.objective-1e-6),"improvement flag agrees with measured objective and is never forced");
    if(proposal.improved)++improvements;
    if(seconds==10 && proposal.targetImproved && !measuredStrengthRoundTrip(fixture,proposal))return;
    if(seconds==10 && mode==0 && proposal.targetImproved && !queuedStrengthCancellation(fixture,proposal))return;
    if(seconds==5 && mode==0)
    {
        const auto other=OpenDuckProcessor::sectorParameterId("smooth",1);set(p,other,7);
        odx::ai::Proposal retained;check(p.localAssistProposal(mode,retained) && p.localAssistStatus(mode).state==State::ready,"changing another sector does not stale this sector's proposal");
        auto changed=p.getCurveSnapshot(mode);changed.points[0]={0,.3f};changed.points[1]={1,.7f};changed.count=2;changed.custom=true;p.setCustomCurve(mode,changed,"Stale curve regression");
        const Snapshot afterEdit(p);check(p.localAssistStatus(mode).state==State::stale && !p.applyLocalAssist(mode) && afterEdit.equals(p),"editing the chosen curve invalidates the proposal and prevents stale Apply");return;
    }
    if(seconds==5 && mode==1)
    {
        p.setSectorParameter(mode,"character",.73f);const Snapshot afterEdit(p);
        check(p.localAssistStatus(mode).state==State::stale && !p.applyLocalAssist(mode) && afterEdit.equals(p),"changing this sector's DSP parameters invalidates the proposal without partial writes");return;
    }
    if(seconds==5 && mode==2)
    {
        fixture.clock.rate=16000;p.setRateAndBufferSizeDetails(16000,127);p.prepareToPlay(16000,127);const Snapshot afterRate(p);
        check(p.localAssistStatus(mode).state==State::stale && !p.applyLocalAssist(mode) && afterRate.equals(p),"audio sample-rate changes invalidate a captured proposal");return;
    }
    if(seconds==5 && mode==3)
    {
        fixture.clock.tempo=137;fixture.feed(127);const Snapshot afterTempo(p);
        check(p.localAssistStatus(mode).state==State::stale && !p.applyLocalAssist(mode) && afterTempo.equals(p),"host-tempo changes invalidate a timing-dependent captured proposal");return;
    }
    const Snapshot beforeApply(p);const bool applied=p.applyLocalAssist(mode,1);
    check(applied==proposal.improved,"Apply succeeds only for a real measured improvement");
    if(!proposal.improved){check(beforeApply.equals(p) && !p.canUndoLocalAssist(mode),"abstention keeps the complete live state unchanged and creates no Undo");return;}
    ++applications;check(beforeApply.isolatedApply(p,mode,proposal),"Apply changes only the chosen sector's approved curve/control subset");
    check(p.canUndoLocalAssist(mode) && p.undoLocalAssist(mode) && beforeApply.equals(p),"AI Undo restores the full pre-Apply APVTS and curve state");
    check(!p.canUndoLocalAssist(mode),"an AI Undo cannot be repeated after its state was restored");
}
void rejectionAndCancellation()
{
    Fixture fixture(0);auto &p=*fixture.processor;
    for(int mode=0;mode<6;++mode)
    {
        p.setSectorParameter(mode,"group",1);const Snapshot before(p);const bool started=p.startLocalAssistCapture(mode,5);
        const auto status=p.localAssistStatus(mode);check(!started && before.equals(p) && status.state==State::error && juce::String(status.message).containsIgnoreCase("Group"),"grouped sector refuses AI capture without touching shared or local state");p.setSectorParameter(mode,"group",0);
    }
    check(!p.startLocalAssistCapture(0,7),"capture accepts only the requested5or10second durations");
    check(p.startLocalAssistCapture(0,5),"capture can start after a rejected request");fixture.feed(127);p.cancelLocalAssist(0);const Snapshot cancelled(p);
    check(p.localAssistStatus(0).state==State::cancelled && !p.applyLocalAssist(0) && cancelled.equals(p),"Cancel stops a partial real capture and leaves no applicable proposal");
}
void insufficientCases()
{
    {
        Fixture fixture(0);fixture.sidechain=false;const Snapshot before(*fixture.processor);
        if(capture(fixture,5)){const auto status=waitFor(fixture);check(status.state==State::insufficient && !fixture.processor->applyLocalAssist(0) && before.equals(*fixture.processor),"missing sidechain audio causes explicit abstention without a fabricated proposal");}
    }
    {
        Fixture fixture(5,8000,2);const Snapshot before(*fixture.processor);
        if(capture(fixture,5)){const auto status=waitFor(fixture);check(status.state==State::insufficient && juce::String(status.message).containsIgnoreCase("MIDI") && !fixture.processor->applyLocalAssist(5) && before.equals(*fixture.processor),"MIDI-trigger analysis abstains when no recorded MIDI events exist");}
    }
}
void firstCapturedTempo()
{
    for(bool changeBeforeFirstStatus : {false,true})
    {
        Fixture fixture(0);fixture.clock.tempo=145;auto &p=*fixture.processor;const Snapshot before(p);
        const bool started=p.startLocalAssistCapture(0,5);
        check(started,"145BPM capture starts before the processor has received its first host block");
        if(!started)continue;
        // Deliberately do not call status/proposal during capture: the cached processor
        // BPM is120 at start, but first-sample metadata truthfully records145.
        fixture.feedCount(40000);
        if(changeBeforeFirstStatus){fixture.clock.tempo=137;fixture.feed(127);}
        const auto status=waitFor(fixture);
        if(changeBeforeFirstStatus)
            check(status.state==State::stale && !p.applyLocalAssist(0) && before.equals(p),
                  "changing145to137BPM after capture but before the first status read still rejects the stale proposal");
        else
        {
            odx::ai::Proposal proposal;
            check(status.state==State::ready && p.localAssistProposal(0,proposal) && before.equals(p),
                  "a stable first capture at145BPM is not invalidated by the initial120BPM cache");
        }
    }
}
void oppositeStereoSidechain()
{
    Fixture fixture(0);fixture.oppositeSidechain=true;auto &p=*fixture.processor;const Snapshot before(p);
    if(!capture(fixture,5))return;
    const auto status=waitFor(fixture);odx::ai::Proposal proposal;
    const bool available=p.localAssistProposal(0,proposal);
    check(status.state==State::ready && status.sidechainPresent && status.validHits>=2 && available && before.equals(p),
          "opposite-polarity stereo sidechain retains real kick hits and can be analysed without modifying live state");
    if(!available)return;
    check(proposal.baseline.finite && proposal.result.finite && proposal.improved==(proposal.result.objective<proposal.baseline.objective-1e-6),
          "opposite-polarity SC proposal reports finite real-DSP metrics and an honest improvement flag");
    const Snapshot beforeApply(p);const bool applied=p.applyLocalAssist(0);
    check(applied==proposal.improved,"opposite-polarity SC Apply still requires a measured improvement");
    if(applied)
        check(beforeApply.isolatedApply(p,0,proposal) && p.canUndoLocalAssist(0) && p.undoLocalAssist(0) && beforeApply.equals(p),
              "opposite-polarity SC Apply is isolated and Undo restores the exact pre-Apply state");
    else
        check(beforeApply.equals(p) && !p.canUndoLocalAssist(0),
              "opposite-polarity SC abstention leaves the full live state unchanged");
}
void freeTempoSidechain()
{
    Fixture fixture(0,8000,1);fixture.clock.tempo=145;auto &p=*fixture.processor;
    set(p,"sync",0);set(p,"freeBpm",120);const Snapshot before(p);
    if(!capture(fixture,5))return;
    const auto status=waitFor(fixture);odx::ai::Proposal proposal;
    check(status.state==State::ready && p.localAssistProposal(0,proposal) && status.sidechainPresent && status.validHits>=2 && before.equals(p),
          "Sidechain analysis with Sync OFF uses free120BPM despite host145BPM without false stale or APVTS edits");
}
void editorReopenDuringCapture()
{
    Fixture fixture(0);auto &p=*fixture.processor;const Snapshot before(p);
    // Fixture deliberately selects a different sector: the editor's visible view
    // must not control the processor-owned capture/proposal lifecycle.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    const bool started=p.startLocalAssistCapture(fixture.mode,5);
    check(started,"real5s AI capture starts while the editor shows another sector");
    if(!started)return;
    fixture.feedCount(8000);
    const auto partial=p.localAssistStatus(fixture.mode);
    editor.reset();
    check(partial.state==State::capturing && std::abs(partial.capturedSeconds-1)<1e-9 && before.equals(p),
          "closing the editor after1s preserves the live APVTS,curves and active processor-owned capture");
    fixture.feedCount(32000);
    const auto completed=waitFor(fixture);odx::ai::Proposal proposal;
    const bool available=p.localAssistProposal(fixture.mode,proposal);
    check(completed.state==State::ready && available && before.equals(p),
          "real audio capture and worker analysis finish with the editor closed without changing DSP state");
    editor.reset(p.createEditor());
    const auto reopened=p.localAssistStatus(fixture.mode);odx::ai::Proposal retained;
    const bool retainedAvailable=p.localAssistProposal(fixture.mode,retained);
    check(available && reopened.state==State::ready && retainedAvailable && before.equals(p)
              && proposal.generation==retained.generation && proposal.contextRevision==retained.contextRevision
              && sameCurve(proposal.before.curve,retained.before.curve)
              && sameCurve(proposal.after.curve,retained.after.curve),
          "reopening another sector's editor retains the completed real proposal without automatic INIT or parameter changes");
}
}
int main(int argc,char **argv)
{
    std::cout<<std::unitbuf;juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        Fixture probe(0);const auto status=probe.processor->localAssistStatus(0);
        check(status.modelsAvailable,"shipped trained models are available to the processor bridge");
        if(status.modelsAvailable)
        {
            for(int mode=0;mode<6;++mode)for(int seconds:{5,10})analysisAndApply(mode,seconds);
            rejectionAndCancellation();insufficientCases();firstCapturedTempo();oppositeStereoSidechain();freeTempoSidechain();editorReopenDuringCapture();
            check(improvements>0 && applications>0,"at least one amount-zero baseline obtains a measured improvement and a verified Apply/Undo round trip");
        }
        else std::cout<<"FAIL trained model generation is required before this integration suite can run\n";
    }
    catch(const std::exception &e){check(false,juce::String("exception: ")+e.what());}
    if(argc>1)
    {
        const juce::File folder(juce::String::fromUTF8(argv[1]));folder.createDirectory();
        folder.getChildFile("assist-bridge-summary.json").replaceWithText("{\"passed\":"+juce::String(passed)+",\"failed\":"+juce::String(failed)+",\"measuredImprovements\":"+juce::String(improvements)+",\"verifiedApplications\":"+juce::String(applications)+"}\n");
    }
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed==0?0:1;
}
