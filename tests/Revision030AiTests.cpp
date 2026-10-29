#include "LocalAssist.h"
#include "LocalAssistRender.h"
#include "NeuralModels.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
bool wait(odx::ai::Controller &controller,int mode)
{
    for(int attempt=0;attempt<600;++attempt){auto state=controller.status(mode).state;if(state!=odx::ai::State::capturing && state!=odx::ai::State::analyzing)return true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;
}
}
int main(int argc,char**argv)
{
#if !defined(ODX_QUALITY_ONLY)
    juce::ScopedJuceInitialiser_GUI init;
#endif
    bool allowUntrained=false;for(int i=1;i<argc;++i)if(std::string(argv[i])=="--allow-untrained")allowUntrained=true;
    int passed=0,failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';(ok?passed:failed)++;};
    if(allowUntrained)std::cout<<"Development capture-contract run: untrained models are permitted; no trained-model quality claim\n";
    odx::ai::Controller availability;const bool matchingModels=availability.available();
    check(matchingModels || allowUntrained,"Matching r2 trained-model release gate (or explicit development-only override)");
    constexpr int sr=8000,n=sr*5;
    std::vector<float> main((size_t)n),side((size_t)n),silence((size_t)n);
    for(int i=0;i<n;++i){const double time=i/(double)sr,local=std::fmod(time,.5);main[(size_t)i]=.2f*(float)std::sin(2*juce::MathConstants<double>::pi*73*time);side[(size_t)i]=.6f*(float)(std::sin(2*juce::MathConstants<double>::pi*80*local)*std::exp(-local/.035));}
    odx::ai::Context context;context.sampleRate=sr;context.bpm=120;context.revision=42;context.hostPositionAvailable=true;context.baseline.curve.amount=100;
    odx::ai::CaptureView view{main.data(),main.data(),side.data(),side.data(),n,sr,120};
    const auto observation=odx::ai::analyse(view,0,context);
    check(observation.mainPresent && observation.sidechainPresent && observation.validHits>=8,"Features measure real main/SC audio and separate transient hits");
    auto empty=view;empty.mainL=silence.data();empty.mainR=silence.data();empty.sideL=silence.data();empty.sideR=silence.data();
    check(!odx::ai::analyse(empty,0,context).mainPresent,"Silence is detected without fabricated measurements");
    std::vector<float> negativeMain=main,negativeSide=side;
    for(auto &sample:negativeMain)sample=-sample;for(auto &sample:negativeSide)sample=-sample;
    auto anti=view;anti.mainR=negativeMain.data();anti.sideR=negativeSide.data();
    const auto stereo=odx::ai::analyse(anti,0,context),oldCancelled=odx::ai::analyseAverageReference(anti,0,context);
    bool stereoFinite=true;for(float value:stereo.features)stereoFinite &= std::isfinite(value) && value>=0 && value<=1;
    check(stereo.analysedChannels==2 && stereo.mainPresent && stereo.sidechainPresent && stereo.validHits>=8 && stereoFinite &&
              !oldCancelled.mainPresent && !oldCancelled.sidechainPresent && std::abs(stereo.mainRms-observation.mainRms)<1e-9,
          "Antiphase main and sidechain retain measured energy, FFT features and real hits without mono cancellation");
    const auto antiDry=odx::ai::measure(anti,main.data(),negativeMain.data(),n);
    check(antiDry.analysedChannels==2 && antiDry.finite && std::abs(antiDry.overlap-1)<1e-5 && antiDry.preservation<1e-6,
          "Antiphase stereo dry output has unit overlap and no false preservation loss");
    auto oneSide=view;oneSide.mainR=silence.data();oneSide.sideR=silence.data();
    const auto single=odx::ai::analyse(oneSide,0,context);const auto singleDry=odx::ai::measure(oneSide,main.data(),silence.data(),n);
    check(single.analysedChannels==2 && single.mainPresent && single.sidechainPresent && singleDry.finite &&
              std::abs(single.mainRms-observation.mainRms/std::sqrt(2.0))<1e-9 && std::abs(singleDry.overlap-1)<1e-5,
          "A silent stereo channel preserves the active channel and correct stereo RMS");
    const auto reference=odx::ai::analyseAverageReference(view,0,context);const auto fast=odx::ai::analyse(view,0,context);
    check(fast.analysedChannels==1 && fast.features==reference.features,
          "Dual-mono analysis uses the exact frozen feature path");
    auto changed=view;changed.sideL=silence.data();changed.sideR=silence.data();
    check(!odx::ai::analyse(changed,0,context).sidechainPresent,"Absent sidechain is distinct from valid main audio");
    for(int featureRate:{8000,44100,48000,96000})
    {
        const int count=(int)(featureRate*1.1);std::vector<float> x((size_t)count),k((size_t)count);
        for(int i=0;i<count;++i){const double t=i/(double)featureRate,local=std::fmod(t,.25);x[(size_t)i]=.2f*(float)std::sin(2*juce::MathConstants<double>::pi*73*t);k[(size_t)i]=.6f*(float)(std::sin(2*juce::MathConstants<double>::pi*80*local)*std::exp(-local/.025));}
        odx::ai::CaptureView rateView{x.data(),x.data(),k.data(),k.data(),count,(double)featureRate,120};
        const auto measured=odx::ai::analyse(rateView,0,context);bool finite=true;
        for(float value:measured.features)finite&=std::isfinite(value) && value>=0 && value<=1;
        check(finite && measured.mainPresent && measured.sidechainPresent && measured.validHits>=4,"Actual features remain finite and source-aware at 8/44.1/48/96 kHz");
    }
    std::vector<float> invalid((size_t)n,std::numeric_limits<float>::quiet_NaN());auto notAudio=view;notAudio.mainL=invalid.data();notAudio.mainR=invalid.data();
    check(!odx::ai::analyse(notAudio,0,context).mainPresent,"Non-finite main data does not become valid audio features");
    odx::ai::Metrics metric=odx::ai::measure(view,main.data(),main.data(),n);
    check(metric.finite && std::abs(metric.overlap-1)<1e-5 && metric.preservation<1e-6,"Aligned dry baseline has unit overlap and zero preservation loss");
    auto muted=odx::ai::measure(view,silence.data(),silence.data(),n);
    check(muted.finite && muted.objective>metric.objective,"Muting the entire bass cannot win the objective");
    const auto safeMetric=odx::ai::measure(view,main.data(),main.data(),n,context,0);
    check(safeMetric.preservationMeasured && safeMetric.recoveryRegions>=2 && odx::ai::assessCandidate(safeMetric,safeMetric).admissible,
          "Real quiet intervals establish measured dry/band/recovery coverage");
    auto quieter=main;for(auto&v:quieter)v*=.7f;
    auto lowerScore=odx::ai::measure(view,quieter.data(),quieter.data(),n,context,0);lowerScore.objective=0;
    check(lowerScore.protectedRmsDb< -3 && !odx::ai::assessCandidate(safeMetric,lowerScore).admissible,
          "A better overlap score cannot override lost quiet-region energy");
    auto veryQuiet=main;for(auto&v:veryQuiet)v*=.1f;
    const auto badBaseline=odx::ai::measure(view,veryQuiet.data(),veryQuiet.data(),n,context,0);
    check(!odx::ai::assessCandidate(badBaseline,lowerScore).admissible,
          "An already attenuated baseline cannot excuse violation of absolute preservation floors");
    auto intentional=main;for(int i=0;i<n;++i)if(std::fmod(i/(double)sr,.5)<.060)intentional[(size_t)i]=0;
    const auto intendedMetric=odx::ai::measure(view,intentional.data(),intentional.data(),n,context,0);
    check(odx::ai::assessCandidate(safeMetric,intendedMetric).admissible && intentional[1]==0,
          "Full closure inside actual key events is allowed while quiet audio recovers");
    std::vector<float> harmonic((size_t)n),recoloured((size_t)n);
    for(int i=0;i<n;++i){const double t=i/(double)sr;harmonic[(size_t)i]=.15f*(float)std::sin(2*juce::MathConstants<double>::pi*73*t)+.10f*(float)std::sin(2*juce::MathConstants<double>::pi*1800*t);recoloured[(size_t)i]=(float)std::sqrt(.15*.15+.1*.1)*(float)std::sin(2*juce::MathConstants<double>::pi*73*t);}
    auto harmonicView=view;harmonicView.mainL=harmonicView.mainR=harmonic.data();
    const auto harmonicDry=odx::ai::measure(harmonicView,harmonic.data(),harmonic.data(),n,context,2);
    const auto timbreLoss=odx::ai::measure(harmonicView,recoloured.data(),recoloured.data(),n,context,2);
    check(std::abs(timbreLoss.protectedRmsDb)<.2 && !odx::ai::assessCandidate(harmonicDry,timbreLoss).admissible,
          "RMS-preserving removal of harmonics still fails protected-band acceptance");
    auto oneGapLost=main;for(int i=(int)(sr*.15);i<(int)(sr*.49);++i)oneGapLost[(size_t)i]=0;
    const auto recoveryLoss=odx::ai::measure(view,oneGapLost.data(),oneGapLost.data(),n,context,0);
    check(recoveryLoss.protectedRmsDb>-.6 && recoveryLoss.worstRecoveryDb< -1 && !odx::ai::assessCandidate(safeMetric,recoveryLoss).admissible,
          "One missing recovery interval cannot hide behind the clip-average energy");
    auto attacking=main;for(int i=0;i<n;++i){const double t=i/(double)sr,local=std::fmod(t,1.0);if(local>=.3&&local<.32)attacking[(size_t)i]+=.5f*(float)std::sin(2*juce::MathConstants<double>::pi*900*t);}
    auto attackView=view;attackView.mainL=attackView.mainR=attacking.data();
    const auto attackDry=odx::ai::measure(attackView,attacking.data(),attacking.data(),n,context,0);
    const auto attackLost=odx::ai::measure(attackView,main.data(),main.data(),n,context,0);
    check(attackDry.protectedAttacks>=3 && attackLost.worstAttackDb< -1 && !odx::ai::assessCandidate(attackDry,attackLost).admissible,
          "Measured main attacks outside key-event windows cannot be removed");
    std::vector<float> continuousKey((size_t)n);for(int i=0;i<n;++i)continuousKey[(size_t)i]=.6f*(float)std::sin(2*juce::MathConstants<double>::pi*80*i/sr);
    auto noGaps=view;noGaps.sideL=noGaps.sideR=continuousKey.data();
    check(!odx::ai::measure(noGaps,main.data(),main.data(),n,context,0).preservationMeasured,
          "Continuous key material without measured recovery gaps cannot invent preservation coverage");
    auto ghostKey=side;for(int i=0;i<n;++i)if(((int)(i/(sr*.5)))%2)ghostKey[(size_t)i]=0;
    auto ghostView=view;ghostView.sideL=ghostView.sideR=ghostKey.data();auto periodicIntent=context;
    periodicIntent.hasIntentProfile=true;periodicIntent.intentCycleSeconds=.5;
    for(size_t i=0;i<periodicIntent.intentGain.size();++i)periodicIntent.intentGain[i]=i/255.f<.12f?0.f:1.f;
    const auto ghostDry=odx::ai::measure(ghostView,main.data(),main.data(),n,periodicIntent,0);
    const auto ghostDuck=odx::ai::measure(ghostView,intentional.data(),intentional.data(),n,periodicIntent,0);
    check(odx::ai::assessCandidate(ghostDry,ghostDuck).admissible,
          "Frozen Repeat intent permits its real periodic ducks on beats without a key hit");
    periodicIntent.intentGain.fill(0);
    check(!odx::ai::measure(ghostView,main.data(),main.data(),n,periodicIntent,0).preservationMeasured,
          "An all-closed baseline intent cannot fabricate protected quiet coverage");
    const auto stereoQuality=odx::ai::measure(anti,main.data(),negativeMain.data(),n,context,0);
    check(stereoQuality.preservationMeasured && std::abs(stereoQuality.protectedRmsDb)<1e-9 && odx::ai::assessCandidate(stereoQuality,stereoQuality).admissible,
          "Stereo antiphase also satisfies the real preservation measurements without cancellation");
#if !defined(ODX_QUALITY_ONLY)
    for(int mode=0;mode<6;++mode)
    {
        auto proposal=odx::ai::decode(odx::ai::encode(context.baseline,mode),mode,context.baseline);
        bool finite=std::isfinite(proposal.curve.amount) && proposal.curve.amount>=0 && proposal.curve.amount<=100;
        for(int i=0;i<proposal.controlCount;++i)finite&=std::isfinite(proposal.controls[(size_t)i].value);
        check(finite,"Each mode has finite bounded decoded controls");
    }
    auto ranked=odx::ai::encode(context.baseline,0);ranked[odx::ai::controlOutputs+3]=.75f;ranked[odx::ai::controlOutputs+5]=.5f;
    const auto firstChoice=odx::ai::decode(odx::ai::refinementCandidate(ranked,0),0,context.baseline);
    const auto secondChoice=odx::ai::decode(odx::ai::refinementCandidate(ranked,7),0,context.baseline);
    check(firstChoice.curve.shape==0 && secondChoice.curve.shape==3 &&
              odx::ai::refinementCandidate(ranked,1)!=odx::ai::refinementCandidate(ranked,7),
          "Bounded refinement checks the second model-ranked shape instead of duplicating its amount candidate");
    auto before=context.baseline,after=before;after.curve.amount=25;
    check(odx::ai::Controller::blend(before,after,0).curve.amount==100 && odx::ai::Controller::blend(before,after,1).curve.amount==25,"Strength endpoints preserve exact baseline and proposed amounts");
    odx::ai::Controller controller;controller.prepare(sr);
    std::atomic<int> evaluations{0};std::atomic<bool> rejectStrength{false};
    // Isolate scheduling here; actual audio acceptance is tested above and by the
    // native parity renderer. Canned scores identify the decoded proposal's
    // explicit control payload, rather than assuming a trained model must lower
    // Amount (a valid model can predict full depth). These are not audio scores.
    controller.setEvaluator([&](int,const odx::ai::Candidate &candidate,const odx::ai::CaptureView&,const odx::ai::Context&frozen,odx::ai::Metrics &m,const std::atomic<bool>&cancel){++evaluations;m=safeMetric;m.finite=!cancel.load();m.objective=candidate.controlCount>frozen.baseline.controlCount?.5:1;if(rejectStrength.load())m.protectedRmsDb=-3;return m.finite;});
    check(!controller.start(-1,5,context) && !controller.start(0,6,context),"Invalid mode/duration is rejected");
    check(controller.start(0,5,context),"Actual five-second capture starts");
    controller.push(main.data(),main.data(),side.data(),side.data(),sr*2,.25,120);
    auto partial=controller.status(0);
    check(partial.state==odx::ai::State::capturing && std::abs(partial.captureProgress-.4f)<1e-6 && partial.capturedSeconds==2,"Capture progress comes from sample count, not elapsed UI time");
    check(partial.capturedBpm==120,"First real audio metadata is exposed immediately as capturedBpm");
    check(!controller.start(1,5,context),"A simultaneous job cannot overwrite the capture buffer");
    controller.push(main.data()+sr*2,main.data()+sr*2,side.data()+sr*2,side.data()+sr*2,sr*3,4.25,120);
    check(wait(controller,0),"Worker finishes a complete capture within bounded test timeout");
    odx::ai::Proposal proposal;
    if(matchingModels)
    {
        check(controller.proposal(0,proposal) && proposal.mode==0 && proposal.contextRevision==42,"Proposal retains mode and context generation");
        check(evaluations.load()>=2,"Trained prediction is checked through evaluator, never blindly applied");
        const auto fullTarget=proposal.after;const int strengthCalls=evaluations.load();
        check(proposal.targetImproved && proposal.evaluatedStrength==1 && controller.requestStrength(0,.5f),"A measured full target can queue real intermediate-strength verification");
        const bool halfReady=wait(controller,0)&&controller.proposal(0,proposal);
        check(halfReady && evaluations.load()==strengthCalls+1 && proposal.evaluatedStrength==.5f && proposal.targetImproved &&
                  std::abs(proposal.evaluatedCandidate.curve.amount-(proposal.before.curve.amount+fullTarget.curve.amount)*.5f)<1e-5 && proposal.after.curve.amount==fullTarget.curve.amount,
              "Strength uses one evaluator render, the exact blend, and retains its original full target");
        rejectStrength=true;check(controller.requestStrength(0,.25f),"A second strength can be evaluated from retained source audio");
        check(wait(controller,0)&&controller.proposal(0,proposal)&&proposal.targetImproved&&!proposal.improved&&proposal.evaluatedStrength==.25f&&proposal.after.curve.amount==fullTarget.curve.amount,
              "Rejected partial strength cannot apply and does not destroy the verified full target");
        rejectStrength=false;check(controller.requestStrength(0,1),"Full strength can be requested again after a rejected intermediate setting");
        check(wait(controller,0)&&controller.proposal(0,proposal)&&proposal.improved&&proposal.evaluatedStrength==1,
              "Returning to full strength performs a fresh successful guard verification");
        check(!controller.requestStrength(0,-.1f)&&!controller.requestStrength(0,1.1f),"Invalid strength cannot alter a retained proposal");
        controller.invalidate(0,43);check(controller.status(0).state==odx::ai::State::stale && !controller.proposal(0,proposal),"Changed context invalidates a ready proposal");
        check(!controller.requestStrength(0,.5f),"A stale capture cannot be revived by moving Strength");
    }
    bool untouched=true;for(int mode=1;mode<6;++mode)untouched&=controller.status(mode).state==odx::ai::State::idle;
    check(untouched,"Other five AI slot states remain unchanged");
    bool started=false;for(int attempt=0;attempt<100 && !started;++attempt){started=controller.start(1,10,context);if(!started)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    check(started,"Independent ten-second capture can start after completed job");controller.push(main.data(),main.data(),side.data(),side.data(),n);
    check(controller.status(1).state==odx::ai::State::capturing && std::abs(controller.status(1).captureProgress-.5f)<1e-6,"Ten-second capture is not completed after only five seconds");
    controller.cancel(1);check(controller.status(1).state==odx::ai::State::cancelled,"Cancellation is visible immediately without fake completion");
    auto startEventually=[&](int mode){for(int attempt=0;attempt<100;++attempt){if(controller.start(mode,5,context))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
    const int callsBefore=evaluations.load();check(startEventually(2),"Silence-guard capture starts after cancellation relinquishes buffer");
    controller.push(silence.data(),silence.data(),silence.data(),silence.data(),n,0,120);
    check(wait(controller,2) && controller.status(2).state==odx::ai::State::insufficient && evaluations.load()==callsBefore,"Silence abstains before model/evaluator use");
    check(startEventually(3),"Absent-SC guard capture starts");controller.push(main.data(),main.data(),silence.data(),silence.data(),n,0,120);
    check(wait(controller,3) && controller.status(3).state==odx::ai::State::insufficient && evaluations.load()==callsBefore,"Main without required sidechain abstains before inference");
    check(startEventually(4),"Stale-generation capture starts");controller.push(main.data(),main.data(),side.data(),side.data(),n,0,120);controller.invalidate(4,99);
    check(wait(controller,4) && controller.status(4).state==odx::ai::State::stale && !controller.proposal(4,proposal),"A generation changed during work cannot publish an applicable result");
    const int unsupportedCalls=evaluations.load();
    context.trigger=2;check(startEventually(5),"MIDI unsupported-context guard capture starts");
    controller.push(main.data(),main.data(),side.data(),side.data(),n,0,120);
    check(wait(controller,5) && controller.status(5).state==odx::ai::State::insufficient && evaluations.load()==unsupportedCalls,
          "MIDI analysis explicitly abstains without recorded events, even when audio is valid");
    odx::ai::Metrics unsupported;std::atomic<bool> notCancelled{false};
    check(!odx::ai::renderCandidate(0,context.baseline,view,context,unsupported,notCancelled),
          "Direct renderer rejects unsupported MIDI context rather than comparing silent triggers");
    context.trigger=0;context.synced=false;check(startEventually(5),"Free-running unsupported-context capture starts");
    controller.push(main.data(),main.data(),side.data(),side.data(),n,0,120);
    check(wait(controller,5) && controller.status(5).state==odx::ai::State::insufficient && evaluations.load()==unsupportedCalls,
          "Repeat analysis abstains when free-running phase cannot be reproduced");
    context.synced=true;check(startEventually(5),"Changed-tempo guard capture starts");
    controller.push(main.data(),main.data(),side.data(),side.data(),sr*2,0,120);
    controller.push(main.data()+sr*2,main.data()+sr*2,side.data()+sr*2,side.data()+sr*2,sr*3,4,145);
    check(wait(controller,5) && controller.status(5).state==odx::ai::State::insufficient && evaluations.load()==unsupportedCalls,
          "Changed tempo during real capture abstains before optimization");
    if(matchingModels)
    {
        odx::ai::Controller racing;racing.prepare(sr);std::atomic<bool> inside{false},release{false};
        racing.setEvaluator([&](int,const odx::ai::Candidate &,const odx::ai::CaptureView &,const odx::ai::Context &,
                               odx::ai::Metrics &m,const std::atomic<bool> &){inside=true;while(!release.load())std::this_thread::yield();m.finite=true;m.objective=.5;return true;});
        context.trigger=0;context.synced=true;racing.start(0,5,context);racing.push(main.data(),main.data(),side.data(),side.data(),n,0,120);
        for(int i=0;i<1000 && !inside.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool entered=inside.load();racing.cancel(0);release=true;odx::ai::Proposal cancelledProposal;
        check(entered && wait(racing,0) && racing.status(0).state==odx::ai::State::cancelled && !racing.proposal(0,cancelledProposal),
              "Cancellation while a real worker evaluation is blocked cannot publish an applicable Ready result");
        odx::ai::Controller cached;cached.prepare(sr);std::atomic<int> cachedCalls{0};std::atomic<bool> blockStrength{false},strengthEntered{false},releaseStrength{false};
        cached.setEvaluator([&](int,const odx::ai::Candidate&candidate,const odx::ai::CaptureView&audio,const odx::ai::Context&frozen,odx::ai::Metrics&m,const std::atomic<bool>&){
            ++cachedCalls;if(blockStrength.load()){strengthEntered=true;while(!releaseStrength.load())std::this_thread::yield();}
            m=safeMetric;m.finite=audio.samples==n && audio.sampleRate==sr && frozen.bpm==120;m.objective=candidate.controlCount>frozen.baseline.controlCount?.5:1;return m.finite;});
        bool sixCached=true;
        for(int mode=0;mode<6;++mode){context.revision=100+(unsigned)mode;sixCached &=cached.start(mode,5,context);cached.push(main.data(),main.data(),side.data(),side.data(),n,0,120);
            odx::ai::Proposal retained;sixCached &=wait(cached,mode)&&cached.proposal(mode,retained)&&retained.targetImproved;}
        for(int mode=0;mode<6;++mode){odx::ai::Proposal retained;sixCached &=cached.proposal(mode,retained)&&retained.mode==mode&&retained.contextRevision==100+(unsigned)mode;}
        check(sixCached,"Six independent source captures and full targets survive later captures in other slots");
        cached.cancel(4);odx::ai::Proposal dismissed;
        check(cached.status(4).state==odx::ai::State::cancelled&&!cached.proposal(4,dismissed)&&!cached.requestStrength(4,.5f),
              "Explicit Cancel dismisses an already Ready proposal and prevents cached Strength reuse");
        const int cachedBefore=cachedCalls.load();check(cached.requestStrength(0,.5f),"An older sector capture remains available for strength verification");
        odx::ai::Proposal cachedHalf;check(wait(cached,0)&&cached.proposal(0,cachedHalf)&&cachedCalls.load()==cachedBefore+1&&cachedHalf.evaluatedStrength==.5f,
              "Revisiting a sector reuses its own retained source in exactly one real evaluator call");
        blockStrength=true;check(cached.requestStrength(0,.75f),"Strength cancellation test queues a retained-source render");
        for(int i=0;i<1000&&!strengthEntered.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool strengthWasEntered=strengthEntered.load();cached.cancel(5);
        check(strengthWasEntered&&cached.status(5).state==odx::ai::State::cancelled&&!cached.proposal(5,dismissed)&&cached.status(0).state==odx::ai::State::analyzing,
              "Cancel of another Ready sector cannot interrupt the active Strength evaluator");
        cached.cancel(0);releaseStrength=true;
        bool cancelledStrength=strengthWasEntered&&wait(cached,0)&&cached.status(0).state==odx::ai::State::cancelled&&!cached.proposal(0,cachedHalf);
        for(int mode=1;mode<4;++mode){odx::ai::Proposal retained;cancelledStrength &=cached.proposal(mode,retained)&&retained.contextRevision==100+(unsigned)mode;}
        cancelledStrength &=cached.status(4).state==odx::ai::State::cancelled&&cached.status(5).state==odx::ai::State::cancelled;
        check(cancelledStrength,"Cancel of a strength render cannot publish Ready or alter unrelated slot states");
    }
#endif
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
