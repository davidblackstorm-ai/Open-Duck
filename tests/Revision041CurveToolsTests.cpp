#include "PluginProcessor.h"
#include "TemplateGeometry.h"
#include "ScopeWaveformView.h"
#include "MeasuredCurveBank4.h"
#include "MeasuredTemplateAudio4.h"
#include <juce_cryptography/juce_cryptography.h>
#define SingleDuckEditor BaselineSingleDuckEditor
#include "fixtures/graphics-v041-baseline/SingleDuckEditor.h"
#undef SingleDuckEditor
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <fstream>

namespace {
int passed = 0, failed = 0;
juce::Array<juce::var> checks;
void check(bool ok, const std::string& text) {
    std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
    ok ? ++passed : ++failed;
    auto* entry = new juce::DynamicObject;
    entry->setProperty("name", juce::String(text));entry->setProperty("passed", ok);checks.add(juce::var(entry));
}
void set(OpenDuckProcessor& p, const char* id, float value) {
    auto* parameter = p.state.getParameter(id);
    if (!parameter) throw std::runtime_error(std::string("Missing parameter ") + id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
bool near(float a, float b, float tolerance = .0001f) { return std::abs(a - b) <= tolerance; }
float gain(const OpenDuckProcessor::CurveSnapshot& c, float x) { return OpenDuckProcessor::effectiveCurveSample(c, x, .5, 0, .5f); }
void geometry() {
    bool identity = true, slopeIdentity = true, finite = true, inverse = true, monotone = true, edges = true, width = true;
    double maxIdentity = 0, maxInverse = 0;
    for (int shape = 0; shape < 16; ++shape) {
        for (float position : {0.f, 50.f, 100.f}) for (float duration : {-50.f, 0.f, 50.f})
            for (float length : {.4f, 1.f}) {
                OpenDuckProcessor::CurveSnapshot c;c.template4 = true;c.shape = shape;c.position4 = position;c.duration4 = duration;c.length = length;
                for (int i = 0; i <= 1000; ++i) {
                    const float x = i / 1000.f;
                    const float raw = odx::template4::unit(.5f + (odx::template4::toSource(x,position,duration)-.5f)/length);
                    const float expected = OpenDuckProcessor::depthGain(odx::template4::factoryShape(raw,shape),100);
                    const double error = std::abs(gain(c,x)-expected);maxIdentity = std::max(maxIdentity,error);identity &= error < 1.e-7;
                    slopeIdentity &= odx::template4::toSlopeSource(x,shape,0) == x && odx::template4::fromSlopeSource(x,shape,0) == x;
                }
            }
        const auto anchors = odx::template4::slopeBounds(shape);
        for (float slope : {-100.f,-65.f,0.f,45.f,100.f}) {
            const auto moved = odx::template4::slopeTargetBounds(shape,slope);
            width &= near(moved.end-moved.start,anchors.end-anchors.start,1.e-6f);
            edges &= moved.start >= .00099f && moved.end <= .99901f
                && odx::template4::toSlopeSource(0,shape,slope) == 0 && odx::template4::toSlopeSource(1,shape,slope) == 1;
            float previous = -1;
            for (int i = 0; i <= 1000; ++i) {
                const float x = i/1000.f,y = odx::template4::toSlopeSource(x,shape,slope);
                const float roundtrip = odx::template4::fromSlopeSource(y,shape,slope);
                finite &= std::isfinite(y) && y >= 0 && y <= 1;monotone &= y >= previous;previous = y;
                const double error = std::abs(roundtrip-x);maxInverse = std::max(maxInverse,error);inverse &= error < .00001;
            }
        }
    }
    check(identity && slopeIdentity,"zero MOVE/SLOPE preserves the previous exact envelope law and both endpoints across16shapes");
    check(finite && monotone && edges && width && inverse,"SLOPE shifts its anchor window without changing width; mappings remain finite monotone and invertible at all extremes");
    bool circular = true;double maximumPhaseError = 0;int discontinuityBrackets = 0;
    for (float shift : {-49.f,-12.5f,12.5f,49.f}) for (int shape = 0; shape < 16; ++shape) {
        OpenDuckProcessor::CurveSnapshot a;a.template4 = true;a.shape = shape;auto b = a;b.moveCurve4 = shift;
        for (int i = 1; i < 1000; ++i) {
            const float x = i/1000.f,shifted = odx::template4::wrap(x+shift*.01f);
            const float mapped = odx::template4::movedCycle(shifted,shift);
            maximumPhaseError = std::max(maximumPhaseError,double(std::abs(mapped-x)));
            const float before = gain(a,x-2.e-6f),at = gain(a,x),after = gain(a,x+2.e-6f),actual = gain(b,shifted);
            const float low = std::min({before,at,after}),high = std::max({before,at,after});
            discontinuityBrackets += high-low>.01f?1:0;
            // Float addition/subtraction can land on the opposite side of an
            // exact step knot. Verify phase accuracy and its local gain bracket
            // instead of requiring a discontinuous function to be continuous.
            circular &= std::abs(mapped-x)<2.e-6f&&actual>=low-.00001f&&actual<=high+.00001f;
        }
    }
    check(circular,"MOVE is a cyclic horizontal offset independent of amplitude across16shapes");
    std::cout << "MEASURE zero-identity-max-error " << maxIdentity << " SLOPE-inverse-max-error " << maxInverse
              << " MOVE-phase-max-error "<<maximumPhaseError<<" step-knot-brackets "<<discontinuityBrackets<<std::endl;
}

void automaticViews() {
    using namespace odx::view;
    std::array<float,512> silence{},singleMin{},singleMax{},loopMin{},loopMax{},toneMin{},toneMax{};
    toneMin.fill(-.15f);toneMax.fill(.15f);
    // Bin actual generated PCM, rather than drawing an onset pattern. A single
    //80ms kick and four80ms kicks share the exact2second capture time axis.
    for(int sample=0;sample<96000;++sample){const int bin=sample*512/96000;
        auto kick=[](int age){if(age<0||age>=3840)return 0.f;const double t=age/48000.;return float(.8*std::exp(-t/.018)*std::sin(2*juce::MathConstants<double>::pi*55*t));};
        const float one=kick(sample-6000);float four=0;for(int hit=0;hit<4;++hit)four+=kick(sample-6000-hit*24000);
        singleMin[(size_t)bin]=std::min(singleMin[(size_t)bin],one);singleMax[(size_t)bin]=std::max(singleMax[(size_t)bin],one);
        loopMin[(size_t)bin]=std::min(loopMin[(size_t)bin],four);loopMax[(size_t)bin]=std::max(loopMax[(size_t)bin],four);}
    CurveViewSelector selector;
    auto observe=[&](uint64_t generation,bool complete,int first,int bins,const auto& low,const auto& high,CurveViewMode mode=CurveViewMode::automatic,int source=0){return selector.observe(mode,generation,complete,first,bins,low,high,2.,source);};
    const auto empty=observe(0,false,0,0,silence,silence);
    check(!empty.loop&&!empty.hasAudio&&empty.onsetBin<0,"AUTO starts with one curve and no invented audio before any valid capture");
    const auto one=observe(1,true,0,512,singleMin,singleMax);
    check(!one.loop&&one.hasAudio&&one.detectedOnsets==1,"AUTO chooses one curve for one actual PCM kick");
    const auto four=observe(2,true,0,512,loopMin,loopMax);
    check(four.loop&&four.hasAudio&&four.detectedOnsets==4,"AUTO chooses LOOP for a complete actual four-kick PCM capture");
    const auto partial=observe(3,false,0,256,singleMin,singleMax);
    const auto repeated=observe(2,true,0,512,singleMin,singleMax);
    const auto unknownPrefix=observe(3,true,70,512,singleMin,singleMax);
    check(partial.loop&&repeated.loop&&unknownPrefix.loop,"AUTO ignores partial frames repeated generations and unknown capture prefixes");
    const auto vote1=observe(3,true,0,512,singleMin,singleMax);
    const auto repaint=observe(3,true,0,512,singleMin,singleMax);
    const auto vote2=observe(4,true,0,512,singleMin,singleMax);
    check(vote1.loop&&repaint.loop&&!vote2.loop,"AUTO needs two independent single-hit frames to leave LOOP and never counts paints as evidence");
    observe(5,true,0,512,loopMin,loopMax);const auto held=observe(6,true,0,512,silence,silence);
    const auto sustained1=observe(7,true,0,512,toneMin,toneMax);const auto sustained2=observe(8,true,0,512,toneMin,toneMax);
    check(held.loop&&sustained1.loop&&!sustained2.loop&&sustained2.hasAudio&&sustained2.detectedOnsets==0,
          "silence holds AUTO stable while sustained audio selects one curve after fresh-frame hysteresis");
    const auto forceSingle=observe(9,true,0,512,loopMin,loopMax,CurveViewMode::single);
    const auto forceLoop=observe(10,true,0,512,singleMin,singleMax,CurveViewMode::loop);
    check(!forceSingle.loop&&forceLoop.loop,"explicit1CURVA and LOOP choices override automatic classification");
    const auto newSource=observe(0,false,0,0,silence,silence,CurveViewMode::automatic,1);
    check(!newSource.loop&&!newSource.hasAudio,"switching waveform source clears stale AUTO evidence");
    const auto single=curveViewWindow(false,2.,.5,0,160,0,512,.75,8);
    const auto loop=curveViewWindow(true,2.,.5,0,160,0,512);
    const auto zoomed=curveViewWindow(true,2.,.5,0,160,0,512,.25,2);
    bool physical=true;for(int i=0;i<=1000;++i){const double x=i/1000.;physical &= std::abs(single.displayForCaptureFraction(single.captureFractionAtDisplay(x))-x)<1.e-12;}
    check(single.valid&&physical&&std::abs(single.durationSeconds-.5)<1.e-12&&std::abs(single.startSeconds-.5)<1.e-12
              &&std::abs(single.captureZoom-4)<1.e-12&&std::abs(single.cycleAtDisplay(1)-single.cycleAtDisplay(0)-1)<1.e-12,
          "1CURVA keeps one exact DSP cycle and physical waveform times while ignoring legacy loop zoom");
    check(loop.valid&&std::abs(loop.durationSeconds-2)<1.e-12&&std::abs(loop.cycleAtDisplay(1)-loop.cycleAtDisplay(0)-4)<1.e-12
              &&std::abs(loop.ticksMs[4]-2000)<1.e-9&&std::abs(zoomed.startSeconds-.5)<1.e-12&&std::abs(zoomed.durationSeconds-1)<1.e-12,
          "LOOP preserves four complete physical cycles and its independent time-axis crop");
    const auto longCapture=curveViewWindow(false,16.,.5,0,0,0,512),shortCapture=curveViewWindow(false,.25,.5,0,0,0,512);
    check(std::abs(longCapture.captureZoom-32)<1.e-12&&std::abs(shortCapture.durationSeconds-.5)<1.e-12
              &&std::abs(shortCapture.captureFractionAtDisplay(1)-2)<1.e-12,
          "single-curve crop supports32x and missing margins without stretching short audio into fake samples");
    bool onsetAtCaptureEdge=true;
    for(double epsilon:{1.e-14,1.e-10,1.e-7}) for(double phase:{-epsilon,1.-epsilon}) {
        const auto edge=curveViewWindow(false,2.,.5,phase,0,0,512);
        const double measuredBinCentre=.5/512.;
        onsetAtCaptureEdge &= edge.valid&&edge.cycleAtStart==0&&std::abs(edge.durationSeconds-.5)<1.e-12
            &&edge.startSeconds>=0&&edge.startSeconds<2./512.
            &&edge.displayForCaptureFraction(measuredBinCentre)>0
            &&edge.displayForCaptureFraction(measuredBinCentre)<1;
    }
    check(onsetAtCaptureEdge,"onset bin0 with a phase just below a cycle boundary keeps the measured kick inside the canonical single-cycle crop");
}

void controlsAndState(const juce::File& root) {
    auto owned = std::make_unique<OpenDuckProcessor>();auto& p = *owned;
    set(p,"duckMix4",57);set(p,"duration4",-13);set(p,"curveTension0",24);
    p.shiftTemplateCurve(1,false);const float coarse = p.value("moveCurve4");p.resetTemplateCurveEdits();
    p.shiftTemplateCurve(1,true);const float fine = p.value("moveCurve4");
    check(near(coarse,6.25f) && near(fine,.390625f) && near(coarse/fine,16,.001f),"four-arrow API uses1/16cycle coarse and1/256cycle fine steps with ratio16");
    p.shiftTemplateCurve(-1,true);check(near(p.value("moveCurve4"),0),"opposite fine arrows exactly undo the movement");
    set(p,"moveCurve4",50);p.shiftTemplateCurve(1,false);
    check(near(p.value("moveCurve4"),-43.75f),"MOVE wraps coherently at its cyclic endpoint");
    set(p,"moveCurve4",12.5f);set(p,"slope4",41);set(p,"bandEnable4",1);set(p,"bandCutoff4",250);set(p,"bandListen4",1);
    p.resetTemplateCurveEdits();
    check(near(p.value("moveCurve4"),0) && near(p.value("slope4"),0)
              && near(p.value("duckMix4"),57) && near(p.value("position4"),50) && near(p.value("duration4"),-13) && near(p.value("curveTension0"),24)
              && p.value("bandEnable4") == 1 && near(p.value("bandCutoff4"),250) && p.value("bandListen4") == 1,
          "Reset restores MOVE and SLOPE only, preserving all knobs and BAND controls");
    set(p,"duckMix4",100);p.selectFactoryShape(0,0);set(p,"moveCurve4",12.5f);set(p,"slope4",40);const auto first = p.getCurveSnapshot(0);
    bool preview = true;const auto thumb = p.getTemplateShapePreview(0);
    for (int i = 0; i <= 1000; ++i) preview &= near(p.previewCurveGain(0,i/1000.f),gain(thumb,i/1000.f),.00001f);
    check(preview,"selected thumbnail and main graph use the same processed curve samples");
    p.selectFactoryShape(1,0);set(p,"moveCurve4",-6.25f);set(p,"slope4",-30);
    const auto firstThumb = p.getTemplateShapePreview(0);bool retained = true;
    for (int i = 0; i <= 1000; ++i) retained &= near(gain(first,i/1000.f),gain(firstThumb,i/1000.f),.00001f);
    p.selectFactoryShape(0,0);
    check(retained && near(p.value("moveCurve4"),12.5f) && near(p.value("slope4"),40),"each wave retains its own MOVE/SLOPE and inactive thumbnail while selecting other waves");
    p.selectFactoryShape(1,0);juce::MemoryBlock state;p.getStateInformation(state);
    auto other = std::make_unique<OpenDuckProcessor>();other->setStateInformation(state.getData(),int(state.getSize()));
    check(near(other->value("moveCurve4"),-6.25f) && near(other->value("slope4"),-30)
              && other->value("bandEnable4") == 1 && near(other->value("bandCutoff4"),250) && other->value("bandListen4") == 1,
          "new MOVE SLOPE and BAND state restores in a fresh instance");
    other->selectFactoryShape(0,0);
    check(near(other->value("moveCurve4"),12.5f) && near(other->value("slope4"),40),"per-wave edits survive serialization and restore");
    other->resetTemplateCurveEdits();
    check(near(p.value("moveCurve4"),-6.25f) && near(p.value("slope4"),-30),"independent instances never share curve-edit state");
    bool fallback = true;
    for (const char* filename : {"legacy-v0.1.0-init.odpx","legacy-v0.2.0-init.odpx","legacy-v0.4.0-init.odpx"}) {
        juce::MemoryBlock old;const bool loaded = root.getChildFile("tests/fixtures").getChildFile(filename).loadFileAsData(old);
        if (!loaded) {fallback = false;continue;}
        set(*other,"moveCurve4",25);set(*other,"slope4",70);set(*other,"bandEnable4",1);set(*other,"bandCutoff4",700);set(*other,"bandListen4",1);
        // The authentic0.2fixture was exported through an independent VST3
        // host, whose outer container stores the untouched component chunk.
        auto xml=juce::AudioProcessor::getXmlFromBinary(old.getData(),int(old.getSize()));
        if(xml&&xml->hasTagName("VST3PluginState")){
            auto* component=xml->getChildByName("IComponent");juce::MemoryBlock inner;
            if(!component||!inner.fromBase64Encoding(component->getAllSubText())){fallback=false;continue;}
            old=inner;xml=juce::AudioProcessor::getXmlFromBinary(old.getData(),int(old.getSize()));
        }
        if(!xml||!xml->hasTagName("OpenDuckState")){fallback=false;continue;}
        other->setStateInformation(old.getData(),int(old.getSize()));
        std::cout<<"MEASURE historical-fallback "<<filename<<" move="<<other->value("moveCurve4")<<" slope="<<other->value("slope4")
                 <<" band="<<other->value("bandEnable4")<<" cutoff="<<other->value("bandCutoff4")<<" listen="<<other->value("bandListen4")<<std::endl;
        fallback &= other->value("moveCurve4") == 0 && other->value("slope4") == 0 && other->value("bandEnable4") == 0
            && near(other->value("bandCutoff4"),160) && other->value("bandListen4") == 0 && other->value("duckMix4")==100
            &&other->value("profileModel4")==0&&other->value("mirrorH4")==0&&other->value("mirrorV4")==0;
    }
    check(fallback,"historical states missing new parameters reset them to zero off160Hz and off without stale values");
}

struct Clock final : juce::AudioPlayHead {
    int64_t sample = 0;
    double tempo=120,rate=48000;
    juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setBpm(tempo);p.setPpqPosition(sample/rate*tempo/60);p.setTimeInSamples(sample);p.setIsPlaying(true);return p;}
};
struct AudioResult { std::vector<float> samples;double low=0,high=0,maxDryError=0,maxStep=0;bool finite=true;int latency=0; };
float audioInput(int64_t sample,bool dc) {
    if(sample<0)return 0;if(dc)return .2f;
    return float(.05*std::sin(2*juce::MathConstants<double>::pi*80*sample/48000.)+.05*std::sin(2*juce::MathConstants<double>::pi*4000*sample/48000.));
}
AudioResult render(bool band,bool listen,bool bypass,bool dc=false,float move=0,float slope=0,bool automate=false) {
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);
    set(p,"profileModel4",0);set(p,"duckMix4",100);set(p,"position4",50);set(p,"duration4",0);set(p,"curveTension0",0);set(p,"bypass",bypass?1.f:0.f);
    set(p,"moveCurve4",move);set(p,"slope4",slope);set(p,"bandEnable4",band?1.f:0.f);set(p,"bandCutoff4",250);set(p,"bandListen4",listen?1.f:0.f);
    p.prepareToPlay(48000,256);AudioResult result;result.latency=p.getLatencySamples();result.samples.reserve(48000*4);float previous=0;
    for(int at=0;at<48000*4;at+=256) {
        if(automate&&at%4096==0){set(p,"moveCurve4",float((at/4096)%15-7)*6.25f);set(p,"slope4",float((at/4096)%11-5)*20);set(p,"bandCutoff4",80+float((at/4096)%12)*100);}
        juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
        for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)block.setSample(ch,i,audioInput(at+i,dc));
        clock.sample=at;p.processBlock(block,midi);
        for(int i=0;i<256;++i){const float value=block.getSample(0,i);result.finite &= std::isfinite(value)&&std::abs(value)<1;
            result.maxDryError=std::max(result.maxDryError,double(std::abs(value-audioInput(at+i-result.latency,dc))));
            if(at+i>result.latency+48000)result.maxStep=std::max(result.maxStep,double(std::abs(value-previous)));previous=value;result.samples.push_back(value);}
    }
    for(double frequency:{80.,4000.}) {double real=0,imaginary=0;const int begin=48000*3,end=48000*4;
        for(int n=begin;n<end;++n){const double angle=2*juce::MathConstants<double>::pi*frequency*(n-result.latency)/48000.;real+=result.samples[(size_t)n]*std::cos(angle);imaginary+=result.samples[(size_t)n]*std::sin(angle);}
        const double amplitude=2*std::hypot(real,imaginary)/(end-begin);(frequency==80?result.low:result.high)=amplitude;}
    return result;
}
void audio() {
    const auto full=render(false,false,false),band=render(true,false,false),listen=render(true,true,false),dry=render(true,true,true);
    std::cout<<"MEASURE carrier amplitudes full80="<<full.low<<" full4k="<<full.high<<" band80="<<band.low<<" band4k="<<band.high<<" listen80="<<listen.low<<" listen4k="<<listen.high<<" dry-error="<<dry.maxDryError<<std::endl;
    check(full.finite&&band.finite&&full.high<.04&&band.low<.043&&std::abs(band.high-.05)<.0015,
          "actual BAND audio attenuates80Hz while the4kHz carrier remains at its unattenuated input amplitude");
    check(listen.finite&&listen.low>.0075&&listen.high<.0015,"BAND headphone audition isolates the processed low band");
    check(dry.finite&&dry.maxDryError<1.e-7,"Power off preserves exact latency-aligned dry even with BAND and audition enabled");
    const auto moved=render(false,false,false,true,12.5f,40);
    OpenDuckProcessor::CurveSnapshot curve;curve.template4=true;curve.position4=50;curve.moveCurve4=12.5f;curve.slope4=40;
    double maxError=0,expectedSmooth=1;const double smoothing=std::exp(-1./48.);
    for(int n=0;n<48000*4;++n){const float phase=odx::template4::wrap((n-moved.latency)/24000.f);const float target=gain(curve,phase);
        expectedSmooth=target+(expectedSmooth-target)*smoothing;
        if(n>moved.latency+48000)maxError=std::max(maxError,double(std::abs(moved.samples[(size_t)n]-.2f*expectedSmooth)));}
    std::cout<<"MEASURE moved-SLOPE preview/audio-max-error "<<maxError<<" maximum-step "<<moved.maxStep<<std::endl;
    check(moved.finite&&maxError<.015&&moved.maxStep<.03,"actual MOVE/SLOPE audio follows its visible envelope and declared1ms causal smoothing, including cyclic wraps");
    const auto automated=render(true,false,false,false,0,0,true);
    check(automated.finite&&automated.maxStep<.08,"automated MOVE SLOPE and BAND cutoff remain finite with bounded sample transitions");
}

void measuredProfiles(const juce::File& root) {
    const auto fixture=root.getChildFile("tests/fixtures/measured4/reference-120bpm-f32.bin");
    auto stream=fixture.createInputStream();if(!stream)throw std::runtime_error("Missing independent measured profile golden PCM");
    char magic[8]{};stream->read(magic,8);const int waves=stream->readInt(),period=stream->readInt(),rate=stream->readInt();
    const float bpm=stream->readFloat(),input=stream->readFloat();
    if(std::string(magic,7)!="ODXREF4"||waves!=16||period!=24000||rate!=48000||bpm!=120)throw std::runtime_error("Invalid independent reference PCM header");
    bool fit=true,linear=true,finite=true,overshoot=false,audioMatches=true;double fitMax=0,audioMax=0;float minimum=1,maximum=0;
    for(int shape=0;shape<waves;++shape){std::vector<float> reference((size_t)period);for(auto& value:reference)value=stream->readFloat();
        for(int n=0;n<period;++n){const float actual=odx::measured4::sample(shape,double(n)/period),expected=reference[(size_t)n]/input;
            const double error=std::abs(actual-expected);fitMax=std::max(fitMax,error);fit &= error<3.e-7;
            minimum=std::min(minimum,actual);maximum=std::max(maximum,actual);
            if(n%7==0)for(float tension:{-100.f,-25.f,0.f,25.f,100.f})for(bool h:{false,true})for(bool v:{false,true})
                finite &= std::isfinite(odx::measured4::sample(shape,double(n)/period,tension,h,v));}
        OpenDuckProcessor::CurveSnapshot c;c.template4=true;c.profileModel4=1;c.position4=50;c.shape=shape;
        for(float depth:{0.f,25.f,50.f,75.f,100.f})for(int n=0;n<=2048;++n){const float x=n/2048.f;auto full=c;full.duckMix4=100;c.duckMix4=depth;
            const float base=gain(full,x),actual=gain(c,x);linear &= std::abs(actual-(1+(base-1)*depth*.01f))<2.e-7f;}
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"antiClick4",0);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"position4",50);set(p,"duration4",0);set(p,"moveCurve4",0);set(p,"slope4",0);set(p,"curveTension0",0);set(p,"bandEnable4",0);set(p,"mirrorH4",0);set(p,"mirrorV4",0);p.selectFactoryShape(shape,0);p.prepareToPlay(rate,256);
        const int latency=p.getLatencySamples(),start=rate*3,end=start+period;
        double waveError=0;
        for(int at=0;at<end;at+=256){const int count=std::min(256,end-at);juce::AudioBuffer<float> block(2,count);juce::MidiBuffer midi;
            for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i)block.setSample(ch,i,input);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<count;++i)if(at+i>=start){const int source=(at+i-latency)%period;const double error=std::abs(block.getSample(0,i)-reference[(size_t)source]);waveError=std::max(waveError,error);}}
        audioMax=std::max(audioMax,waveError);audioMatches &= waveError<5.e-8;
        std::cout<<"MEASURE measured-reference shape="<<shape+1<<" static-output-max-error="<<waveError<<" latency="<<latency<<std::endl;
    }
    overshoot=minimum<-.05f&&maximum>1.05f;
    std::cout<<"MEASURE measured-bank max-gain-error="<<fitMax<<" actual-output-max-error="<<audioMax<<" min="<<minimum<<" max="<<maximum<<std::endl;
    check(fit,"all16 measured Catmull profiles match independent held-out120BPM reference PCM within3e-7 gain");
    check(linear&&finite&&overshoot,"measured profiles retain signed periodic overshoot, finite CURVE/mirrors and the0/25/50/75/100 MIX depth law");
    check(audioMatches,"actual static SYNC audio reproduces all16 independent reference profiles after reported latency without legacy1ms envelope blur");
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;
    const bool fresh=p.value("profileModel4")==1;p.selectFactoryShape(2,0);set(p,"mirrorH4",1);set(p,"mirrorV4",1);p.selectFactoryShape(6,0);p.selectFactoryShape(2,0);
    juce::MemoryBlock saved;p.getStateInformation(saved);auto restored=std::make_unique<OpenDuckProcessor>();restored->setStateInformation(saved.getData(),int(saved.getSize()));
    check(fresh&&restored->value("profileModel4")==1&&restored->value("mirrorH4")==1&&restored->value("mirrorV4")==1,
          "fresh measured profile version and exact per-wave mirror flags survive selection and state roundtrip");
}

void measuredBandAndAnticlick(const juce::File& root) {
    auto stream=root.getChildFile("tests/fixtures/measured4/reference-band-impulses-f32.bin").createInputStream();
    if(!stream)throw std::runtime_error("Missing independent BAND impulse golden");char magic[8]{};stream->read(magic,8);
    const int cases=stream->readInt(),rate=stream->readInt();if(std::string(magic,8)!="ODXBAND4"||cases!=6||rate!=48000)throw std::runtime_error("Invalid BAND golden header");
    bool helperMatches=true,audioMatches=true,fullNull=true;double helperMax=0,audioMax=0;
    for(int test=0;test<cases;++test){const int cutoff=stream->readInt(),solo=stream->readInt(),count=stream->readInt();std::vector<float> input((size_t)count),reference((size_t)count);
        odx::audio4::LowpassTpt lowpass;lowpass.prepare(rate,cutoff);
        double helperError=0;
        for(int n=0;n<count;++n){input[(size_t)n]=stream->readFloat();reference[(size_t)n]=stream->readFloat();lowpass.advance();const float low=lowpass.process(0,input[(size_t)n]);const float actual=solo?low:low+(input[(size_t)n]-low);
            helperError=std::max(helperError,double(std::abs(actual-reference[(size_t)n])));if(!solo)fullNull &= actual==input[(size_t)n];}
        helperMax=std::max(helperMax,helperError);helperMatches &= helperError<3.e-8;
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",0);set(p,"bandEnable4",1);set(p,"bandCutoff4",float(cutoff));set(p,"bandListen4",float(solo));p.prepareToPlay(rate,256);
        double waveError=0;for(int at=0;at<count;at+=256){const int samples=std::min(256,count-at);juce::AudioBuffer<float> block(2,samples);juce::MidiBuffer midi;
            for(int ch=0;ch<2;++ch)for(int i=0;i<samples;++i)block.setSample(ch,i,input[(size_t)(at+i)]);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<samples;++i){waveError=std::max(waveError,double(std::abs(block.getSample(0,i)-reference[(size_t)(at+i)])));if(!solo)fullNull &= block.getSample(0,i)==input[(size_t)(at+i)];}}
        audioMax=std::max(audioMax,waveError);audioMatches &= waveError<3.e-8&&p.getLatencySamples()==0;
        std::cout<<"MEASURE BAND-reference cutoff="<<cutoff<<" solo="<<solo<<" helper-error="<<helperError<<" actual-output-error="<<waveError<<std::endl;
    }
    check(helperMatches&&audioMatches,"measured BAND TPT helper and actual zero-latency audio match independent20/320/5120Hz low-solo impulse PCM");
    check(fullNull,"measured MIX0 full BAND reconstructs the original impulse sample-exactly at every tested cutoff");
    struct Event{int64_t sample;float delta;};std::vector<Event> events;
    for(int n=600;n<24000;n+=37)events.push_back({n,float((n/37)%7-3)*.03f});
    events.push_back({24050,.25f});events.push_back({24050,-.125f});
    bool exact=true,finite=true;double maxError=0,maxClick=0;
    for(int window:{0,1,289,769}){odx::audio4::AnticipatedGain smoother;smoother.prepare(events.size()+8);smoother.reset(window);for(const auto& event:events)smoother.push(event.sample,event.delta);
        for(int64_t n=0;n<25000;++n){double expected=0;if(window>0)for(const auto& event:events){const double distance=double(event.sample-n);if(distance>0&&distance<=window){const double u=distance/window;expected+=event.delta*(1-3*u*u+2*u*u*u);}}
            const float actual=smoother.correction(n);const double error=std::abs(actual-expected);maxError=std::max(maxError,error);exact &= error<2.e-6;finite &= std::isfinite(actual);}
        smoother.reset(window);if(window>1){smoother.push(1000,-1);float previous=1;
            for(int n=0;n<1500;++n){const float raw=n<1000?1.f:0.f,value=raw+smoother.correction(n);maxClick=std::max(maxClick,double(std::abs(value-previous)));previous=value;}}}
    std::cout<<"MEASURE anticipated-cubic brute-force-max-error="<<maxError<<" isolated-jump-max-sample-step="<<maxClick<<" BAND-max-output-error="<<audioMax<<std::endl;
    check(exact&&finite,"bounded anticipated-gain queue matches independent brute-force cubic corrections through dense and simultaneous retriggers over25000 samples");
    check(maxClick<.006,"independent6ms cubic anti-click bounds a unity gain jump below0.006 per sample without changing the raw steady profile");
}

void measuredLatency() {
    bool correct=true,dry=true;
    for(double sampleRate:{44100.,48000.,96000.})for(int trigger:{0,1,2})for(int anti:{0,1}){
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=sampleRate;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",0);set(p,"antiClick4",float(anti));p.setSectorParameter(0,"trigger",float(trigger));p.prepareToPlay(sampleRate,256);
        const int expected=trigger==0?0:(trigger==1?int(std::floor(sampleRate*.010)):0)+(anti?int(std::floor(sampleRate*.006))+1:0);
        correct &= p.getLatencySamples()==expected;int actualImpulse=-1;
        for(int at=0;at<4096;at+=256){juce::AudioBuffer<float> block(2,256);block.clear();juce::MidiBuffer midi;
            if(at==0)for(int ch=0;ch<2;++ch)block.setSample(ch,17,.15f);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<256;++i){const float value=block.getSample(0,i);if(value!=0&&actualImpulse<0)actualImpulse=at+i;
                dry &= value==((at+i)==17+expected?.15f:0.f);}}
        correct &= actualImpulse==17+expected;
        std::cout<<"MEASURE model1-latency sr="<<sampleRate<<" trigger="<<trigger<<" antiClick="<<anti<<" published="<<p.getLatencySamples()<<" expected="<<expected<<" impulse="<<actualImpulse<<std::endl;
    }
    check(correct&&dry,"model1 publishes and applies exact SYNC MIDI AUDIO latency with anti-click on/off at44.1/48/96kHz while MIX0 stays sample-exact dry");
}

void measuredOneShot() {
    constexpr std::array<float,16> defaults{{.984375f,.984375f,.984375f,.984375f,.984375f,.984375f,.984375f,.984375f,.895f,.79f,.984375f,.984375f,1.f,1.f,1.f,.984375f}};
    bool holds=true,idle=true,ends=true;double maximumError=0;
    for(int shape=0;shape<16;++shape){auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",0);p.setSectorParameter(0,"trigger",2);p.setSectorParameter(0,"loop",0);p.selectFactoryShape(shape,0);
        const float end=p.value("v3End0");ends &= near(end,defaults[(size_t)shape],1.e-6f);p.prepareToPlay(48000,256);
        const auto curve=p.getCurveSnapshot(0);const float expected=.15f*odx::measured4::sampleEndpoint(shape,end,curve.tension,curve.mirrorH4,curve.mirrorV4,curve.slope4);const int triggerAt=1024;
        double error=0;
        for(int at=0;at<48000;at+=256){const int count=std::min(256,48000-at);juce::AudioBuffer<float> block(2,count);juce::MidiBuffer midi;
            for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i)block.setSample(ch,i,.15f);if(at==triggerAt)midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<count;++i){if(at+i<triggerAt)idle &= block.getSample(0,i)==.15f;if(at+i>triggerAt+24500)error=std::max(error,double(std::abs(block.getSample(0,i)-expected)));}}
        holds &= error<5.e-7;maximumError=std::max(maximumError,error);
    }
    check(ends&&idle&&holds,"all16 measured one-shot families stay dry before a real MIDI event and hold their selected END gain indefinitely after it");
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;p.selectFactoryShape(0,0);p.beginTemplateEdit4("END prima forma");p.setSectorParameter(0,"end",.35f);p.commitTemplateEdit4();p.selectFactoryShape(8,0);p.setSectorParameter(0,"end",.62f);p.selectFactoryShape(0,0);
    bool remembered=near(p.value("v3End0"),.35f);juce::MemoryBlock state;p.getStateInformation(state);auto restored=std::make_unique<OpenDuckProcessor>();restored->setStateInformation(state.getData(),int(state.getSize()));restored->selectFactoryShape(8,0);
    remembered &= near(restored->value("v3End0"),.62f);const bool undone=p.undoTemplateEdit4();
    check(remembered&&undone&&near(p.value("v3End0"),defaults[0]),"END edits belong to each selected waveform, survive state restore and return exactly through full Undo");
    std::cout<<"MEASURE measured-one-shot held-end maximum-output-error="<<maximumError<<std::endl;
}

void measuredSlope(const juce::File& root) {
    auto stream=root.getChildFile("tests/fixtures/measured4/reference-slope-seven-f32.bin").createInputStream();if(!stream)throw std::runtime_error("Missing independent seven-SLOPE PCM golden");
    char magic[8]{};stream->read(magic,8);const int cases=stream->readInt(),period=stream->readInt(),rate=stream->readInt();const float bpm=stream->readFloat(),input=stream->readFloat();
    if(std::string(magic,8)!="ODXSLOP4"||cases!=7||period!=8192||rate!=48000||bpm!=351.5625f)throw std::runtime_error("Invalid seven-SLOPE golden header");
    bool matches=true,audioMatches=true;double maxGainError=0,maxAudioError=0;
    for(int test=0;test<cases;++test){const int delta=stream->readInt();const float slope=float(double(delta)/4014.*100.);std::vector<float> reference((size_t)period);
        for(int n=0;n<period;++n){reference[(size_t)n]=stream->readFloat();const double error=std::abs(odx::measured4::sample(0,double(n)/period,0,false,false,slope)-reference[(size_t)n]/input);maxGainError=std::max(maxGainError,error);matches &= error<3.e-7;}
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=rate;clock.tempo=bpm;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"antiClick4",0);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"slope4",slope);p.prepareToPlay(rate,256);double error=0;
        for(int at=0;at<65536+period;at+=256){juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)block.setSample(ch,i,input);clock.sample=at;p.processBlock(block,midi);
            if(at>=65536)for(int i=0;i<256;++i)error=std::max(error,double(std::abs(block.getSample(0,i)-reference[(size_t)(at+i-65536)])));}
        maxAudioError=std::max(maxAudioError,error);audioMatches &= error<5.e-8&&p.getLatencySamples()==0;
        std::cout<<"MEASURE saved-SLOPE case="<<test+1<<" delta="<<delta<<" slope="<<slope<<" actual-output-max-error="<<error<<std::endl;
    }
    bool finite=true,mapping=true;float minimum=1,maximum=0;
    for(int shape=0;shape<16;++shape)for(float slope:{-100.f,-50.f,0.f,50.f,100.f}){
        OpenDuckProcessor::CurveSnapshot c;c.template4=true;c.profileModel4=1;c.position4=50;c.shape=shape;c.slope4=slope;
        for(int n=0;n<=2048;++n){const float x=n/2048.f,value=odx::measured4::sample(shape,x,0,false,false,slope);finite &= std::isfinite(value)&&value>=-.125001f&&value<=1.125001f;
            minimum=std::min(minimum,value);maximum=std::max(maximum,value);mapping &= OpenDuckProcessor::curveTimeToRaw(c,x,.5)==x&&OpenDuckProcessor::rawCurveTimeToCycle(c,x,.5)==x;}}
    std::cout<<"MEASURE saved-SLOPE gain-max-error="<<maxGainError<<" actual-output-max-error="<<maxAudioError<<" all-wave-extreme-range="<<minimum<<".."<<maximum<<std::endl;
    check(matches&&audioMatches,"measured Wave1 SLOPE matches all seven independent saved geometries in its sampler and actual zero-latency SYNC audio");
    check(finite&&mapping,"all16 SLOPE extremes stay finite within cubic overshoot bounds and move internal spline controls without warping the source-time map");
}

void measuredLiveEdits() {
    bool preserved=true,released=true,routingEdges=true;double steadyError=0;
    for(bool tone:{false,true}){auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);
        set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",0);p.setSectorParameter(0,"trigger",2);p.setSectorParameter(0,"loop",2);p.setSectorParameter(0,"speedMode",1);p.setSectorParameter(0,"hz",40);p.prepareToPlay(48000,256);
        const double period=p.cycleSeconds(0)*48000.;preserved &= near(p.value("v3Hz0"),40);
        const int64_t noteAt=1024;float previous=0;std::array<double,2> jumps{};
        auto input=[&](int64_t n){return tone?float(.15*std::sin(2*juce::MathConstants<double>::pi*997*n/48000.)):.15f;};
        for(int at=0;at<18432;at+=256){if(at==4096||at==6144){set(p,"antiClick4",at==4096?1.f:0.f);juce::MessageManager::getInstance()->runDispatchLoopUntil(85);preserved &= p.getLatencySamples()==(at==4096?289:0);}
            juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)block.setSample(ch,i,input(at+i));
            if(at==noteAt)midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);if(at==8192)midi.addEvent(juce::MidiMessage::noteOff(1,36),0);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<256;++i){const int n=at+i;const float value=block.getSample(0,i);if(n==4096||n==6144)jumps[(size_t)(n==4096?0:1)]=std::abs(value-previous);previous=value;
                if((n>=4608&&n<6144)||(n>=6656&&n<8192)){const int latency=p.getLatencySamples();const float raw=odx::measured4::sample(0,double(n-latency-noteAt)/period);
                    const float expected=input(n-latency)*(p.value("antiClick4")>.5f?std::clamp(raw,0.f,1.f):raw);const double error=std::abs(value-expected);steadyError=std::max(steadyError,error);preserved &= error<1.e-6;}
                if(n>=16384)released &= std::abs(value-input(n))<1.e-6;}}
        std::cout<<"MEASURE live-anti-click "<<(tone?"tone997Hz":"DC")<<" on-boundary-step="<<jumps[0]<<" off-boundary-step="<<jumps[1]<<" steady-error="<<steadyError<<std::endl;
        for(double jump:jumps)routingEdges &= jump<(tone?.03:.003);
    }
    check(preserved&&released,"live MIDI anti-click changes preserve active held note age and release state through both published PDC changes for DC and997Hz audio");
    check(routingEdges,"live PDC changes bound DC steps below0.003 and997Hz steps below0.03 including the input sine's natural derivative");
    bool rapid=true;double rapidMaximum=0;
    for(bool tone:{false,true}){auto instance=std::make_unique<OpenDuckProcessor>();auto& p=*instance;Clock clock;p.setPlayHead(&clock);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",0);
        p.setSectorParameter(0,"trigger",2);p.setSectorParameter(0,"loop",2);p.setSectorParameter(0,"speedMode",1);p.setSectorParameter(0,"hz",40);p.prepareToPlay(48000,32);const double period=p.cycleSeconds(0)*48000.;float previous=0;double jumps=0;
        auto input=[&](int64_t n){return tone?float(.15*std::sin(2*juce::MathConstants<double>::pi*997*n/48000.)):.15f;};
        for(int at=0;at<8192;at+=32){const bool flip=at>=4096&&at<=4320;if(flip){set(p,"antiClick4",((at-4096)/32)%2==0?1.f:0.f);juce::MessageManager::getInstance()->runDispatchLoopUntil(45);}
            juce::AudioBuffer<float> block(2,32);juce::MidiBuffer midi;for(int ch=0;ch<2;++ch)for(int i=0;i<32;++i)block.setSample(ch,i,input(at+i));if(at==1024)midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<32;++i){const int n=at+i;const float value=block.getSample(0,i);if(flip&&i==0){const double step=std::abs(value-previous);jumps=std::max(jumps,step);rapid &= step<(tone?.03:.003);}previous=value;
                rapid &= std::isfinite(value);if(n>=6144)rapid &= std::abs(value-input(n)*odx::measured4::sample(0,double(n-1024)/period))<1.e-6;}}
        rapidMaximum=std::max(rapidMaximum,jumps);std::cout<<"MEASURE rapid-PDC-flips "<<(tone?"tone997Hz":"DC")<<" eight-flips/32samples maximum-boundary-step="<<jumps<<std::endl;}
    check(rapid,"rapid PDC reversals every32 samples retain continuous crossfade weight and restore the exact held-loop audio after settling");
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;p.setPlayHead(&clock);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",0);
    p.setSectorParameter(0,"trigger",2);p.setSectorParameter(0,"loop",0);p.setSectorParameter(0,"speedMode",1);p.setSectorParameter(0,"hz",40);p.setSectorParameter(0,"end",.8f);p.prepareToPlay(48000,256);
    std::array<double,2> firstStep{},windowStep{};float previous=0;
    for(int at=0;at<8192;at+=256){if(at==4096)p.setSectorParameter(0,"end",.07f);if(at==6144)p.setSectorParameter(0,"loop",1);
        juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)block.setSample(ch,i,.15f);if(at==1024)midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);clock.sample=at;p.processBlock(block,midi);
        for(int i=0;i<256;++i){const int n=at+i;const float value=block.getSample(0,i);const double step=std::abs(value-previous);previous=value;
            if(n==4096||n==6144)firstStep[(size_t)(n==4096?0:1)]=step;
            if(n>=4096&&n<4192)windowStep[0]=std::max(windowStep[0],step);if(n>=6144&&n<6240)windowStep[1]=std::max(windowStep[1],step);}}
    std::cout<<"MEASURE live-END first-step="<<firstStep[0]<<" max2ms-step="<<windowStep[0]<<" live-LOOP first-step="<<firstStep[1]<<" max2ms-step="<<windowStep[1]<<std::endl;
    check(firstStep[0]<1.e-7&&firstStep[1]<1.e-7&&windowStep[0]<.003&&windowStep[1]<.003,
          "live END and LOOP changes during a held one-shot start continuously and spread their gain change across the declared2ms edit ramp");
}

void clickSafetyAndSidechain16() {
    struct Metrics {double step=0,minimum=10,maximum=-10;bool bounded=true,finite=true;};
    auto render=[](double rate,int shape,double hz,bool anti,bool tone,bool edit) {
        Metrics result;auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=rate;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",anti?1.f:0.f);set(p,"bandEnable4",0);p.selectFactoryShape(shape,0);
        p.setSectorParameter(0,"trigger",0);p.setSectorParameter(0,"speedMode",1);p.setSectorParameter(0,"hz",float(hz));p.prepareToPlay(rate,64);
        const int total=int(std::ceil(rate*(edit?.95:std::max(.2,3./hz))));int nextEdit=0;float previous=0;
        constexpr std::array<const char*,7> ids{{"duration4","duration4","curveTension0","curveTension0","duckMix4","duckMix4","moveCurve4"}};
        constexpr std::array<float,7> values{{30,-30,80,-80,0,100,22}};
        for(int at=0;at<total;at+=64){if(edit&&nextEdit<7&&at>=int(rate*(.100+.100*nextEdit))){set(p,ids[size_t(nextEdit)],values[size_t(nextEdit)]);++nextEdit;}
            const int count=std::min(64,total-at);juce::AudioBuffer<float> block(2,count);juce::MidiBuffer midi;
            for(int i=0;i<count;++i){const int n=at+i;block.setSample(0,i,tone?float(.15*std::sin(2*juce::MathConstants<double>::pi*997*n/rate)):.15f);
                block.setSample(1,i,(int(n/(rate*.08))&1)?-1.f:1.f);}
            clock.sample=at;p.processBlock(block,midi);
            for(int i=0;i<count;++i){const int n=at+i;const float left=block.getSample(0,i),right=block.getSample(1,i);const float sign=(int(n/(rate*.08))&1)?-1.f:1.f;
                if(n>0)result.step=std::max(result.step,double(std::abs(left-previous)));previous=left;
                result.finite &= std::isfinite(left)&&std::isfinite(right);const double coefficient=right*sign;
                result.minimum=std::min(result.minimum,coefficient);result.maximum=std::max(result.maximum,coefficient);
                result.bounded &= coefficient>=-1.e-7&&coefficient<=1.0000001;}
        }
        result.finite &= p.getLatencySamples()==0;return result;
    };
    bool protectedCycles=true,fullScale=true,edits=true;double protectedStep=0,rawStep=0,toneStep=0,editDcStep=0;
    for(double rate:{44100.,48000.})for(double hz:{2.,40.})for(int shape=0;shape<16;++shape){const auto m=render(rate,shape,hz,true,false,false);
        protectedStep=std::max(protectedStep,m.step);protectedCycles &= m.finite&&m.step<.0032;fullScale &= m.bounded;}
    for(double rate:{44100.,48000.})for(int shape:{0,10}){rawStep=std::max(rawStep,render(rate,shape,40,false,false,false).step);
        for(bool tone:{false,true}){const auto m=render(rate,shape,40,true,tone,true);edits &= m.finite&&m.bounded&&m.step<(tone?.03:.0032);
            if(tone)toneStep=std::max(toneStep,m.step);else editDcStep=std::max(editDcStep,m.step);}}
    check(protectedCycles&&rawStep>.03&&protectedStep<rawStep*.25,"anti-click ON bounds actual SYNC cycle seams across all16 profiles at44.1/48kHz while raw OFF retains the measured discontinuity");
    check(fullScale,"protected full-band gain stays0..1 for positive and negative full-scale input across16 profiles without hard-clipping the input signal");
    check(edits,"actual SYNC PHASE CURVE MIX and MOVE edits keep DC steps below0.0032 and997Hz steps below0.03 with zero published latency");
    auto unity=std::make_unique<OpenDuckProcessor>();set(*unity,"bypass",0);set(*unity,"duckMix4",0);set(*unity,"antiClick4",1);unity->prepareToPlay(48000,256);
    bool noOutputClip=true;for(int block=0;block<4;++block){juce::AudioBuffer<float> signal(2,256);juce::MidiBuffer midi;
        for(int i=0;i<256;++i){signal.setSample(0,i,1.25f);signal.setSample(1,i,-1.25f);}unity->processBlock(signal,midi);
        for(int i=0;i<256;++i)noOutputClip &= signal.getSample(0,i)==1.25f&&signal.getSample(1,i)==-1.25f;}
    check(noOutputClip,"MIX0 preserves input above unity exactly with protection ON proving gain limiting does not hard-clip the audio output");
    std::cout<<"MEASURE R16 SYNC protected-max-step="<<protectedStep<<" raw-max-step="<<rawStep<<" edit-DC-step="<<editDcStep<<" edit997-step="<<toneStep<<std::endl;

    bool buses=true,validKicks=true,noFallback=true;int scenarios=0;
    for(double rate:{44100.,48000.})for(const auto layout:std::array<std::array<int,2>,4>{{{{1,2}},{{2,1}},{{2,2}},{{1,1}}}}){
        for(bool hasKick:{false,true}){auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=rate;clock.tempo=138;p.setPlayHead(&clock);
            set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",100);set(p,"antiClick4",1);p.selectFactoryShape(10,0);
            p.setSectorParameter(0,"trigger",1);p.setSectorParameter(0,"threshold",-36);auto config=p.getBusesLayout();
            config.inputBuses.set(0,layout[0]==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());
            config.outputBuses.set(0,layout[0]==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());
            config.inputBuses.set(1,layout[1]==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());
            buses &= p.setBusesLayout(config);p.prepareToPlay(rate,257);bool earlyObserved=false;float peak=0,detector=0;
            for(int at=0;at<int(rate*1.8);at+=257){const int count=std::min(257,int(rate*1.8)-at);juce::AudioBuffer<float> block(layout[0]+layout[1],count);block.clear();juce::MidiBuffer midi;
                for(int i=0;i<count;++i){const double time=(at+i)/rate;for(int ch=0;ch<layout[0];++ch)block.setSample(ch,i,time<.3?0.f:.15f);
                    float side=0;if(hasKick)for(double event:{.100,.370,1.080}){const double age=time-event;if(age>=0&&age<.08)side+=float(.055*std::exp(-age/.016)*std::sin(2*juce::MathConstants<double>::pi*55*age));}
                    block.setSample(layout[0]+layout[1]-1,i,side);}
                clock.sample=at;p.processBlock(block,midi);peak=std::max(peak,p.templateSidechainPeak4.load());detector=std::max(detector,p.templateDetectorPeak4.load());
                if(!earlyObserved&&at+count>=int(rate*.24)){earlyObserved=true;validKicks &= !hasKick||p.templateTriggerCount4.load()==1;}
            }
            const auto count=p.templateTriggerCount4.load();buses &= p.templateSidechainEnabled4.load();
            if(hasKick)validKicks &= count==3&&peak>.025f&&detector>.015f;else noFallback &= count==0&&peak==0&&detector==0;
            std::cout<<"MEASURE R16 direct-SC sr="<<rate<<" MAIN="<<layout[0]<<" SC="<<layout[1]<<" kick="<<hasKick<<" count="<<count<<" peak="<<peak<<" detector="<<detector<<std::endl;++scenarios;
        }
    }
    check(buses&&validKicks&&scenarios==16,"real bipolar quiet SC kicks reach the detector and trigger three times for every mono/stereo arrangement including right-only stereo SC with mono MAIN");
    check(noFallback,"silent external SC never triggers from MAIN and a real kick can trigger while MAIN is completely silent");
}

juce::Component* named(juce::Component& parent,const juce::String& name) {
    if(parent.getName()==name)return &parent;
    for(int i=0;i<parent.getNumChildComponents();++i)if(auto* found=named(*parent.getChildComponent(i),name))return found;
    return nullptr;
}
juce::MouseEvent mouse(juce::Component& component,juce::Point<float> position,bool pressed=false,
                       juce::Point<float> down={-1,-1},bool dragged=false,bool popup=false,bool fine=false) {
    if(down.x<0)down=position;const auto time=juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),position,
        juce::ModifierKeys((pressed?(popup?juce::ModifierKeys::rightButtonModifier:juce::ModifierKeys::leftButtonModifier):0)
            |(fine?juce::ModifierKeys::shiftModifier:0)),1,0,0,0,0,
        &component,&component,time,down,time,1,dragged};
}
void hover(juce::Component& graph,bool inside) {
    const auto point=inside?juce::Point<float>(graph.getWidth()*.5f,graph.getHeight()*.5f):juce::Point<float>(-20,-20);
    if(inside){graph.mouseEnter(mouse(graph,point));graph.mouseMove(mouse(graph,point));}
    else graph.mouseExit(mouse(graph,point));
}
std::vector<float> hostValues(OpenDuckProcessor& p) {
    std::vector<float> values;for(auto* parameter:p.getParameters())values.push_back(parameter->getValue());return values;
}
void fluidKnobGestures() {
    bool precision=true,independent=true,stationary=true,limits=true,undo=true;double maximumError=0;
    for(const char* id:{"duckMix4","duration4","curveTension0"}) {
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;
        auto* parameter=p.state.getParameter(id);parameter->setValueNotifyingHost(.5f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1728,912);editor->setVisible(true);
        auto* slider=dynamic_cast<juce::Slider*>(named(*editor,id));if(!slider)throw std::runtime_error("Missing native knob");
        const auto before=hostValues(p);const int index=parameter->getParameterIndex();
        const juce::Point<float> down(slider->getWidth()*.5f,slider->getHeight()*.5f);auto at=down;double expected=.5;
        slider->mouseDown(mouse(*slider,at,true,down));
        for(int fine=0;fine<2;++fine){const float stopped=parameter->getValue();
            slider->mouseDrag(mouse(*slider,at,true,down,true,false,fine!=0));stationary &= parameter->getValue()==stopped;
            for(int step=0;step<12;++step){at.y-=1;expected+=(fine?.1:1.)/1200.;slider->mouseDrag(mouse(*slider,at,true,down,true,false,fine!=0));
                const double error=std::abs(parameter->getValue()-expected);maximumError=std::max(maximumError,error);precision &= error<.00001;}
            const float horizontal=parameter->getValue();at.x+=100;slider->mouseDrag(mouse(*slider,at,true,down,true,false,fine!=0));stationary &= parameter->getValue()==horizontal;}
        const float stopped=parameter->getValue();slider->mouseDrag(mouse(*slider,at,true,down,true));stationary &= parameter->getValue()==stopped;
        slider->mouseUp(mouse(*slider,at,false,down,true));const auto after=hostValues(p);
        for(size_t i=0;i<before.size();++i)if(int(i)!=index)independent &= before[i]==after[i];
        undo &= p.canUndoTemplateEdit4()&&p.undoTemplateEdit4()&&hostValues(p)==before&&!p.canUndoTemplateEdit4();
        slider->mouseDown(mouse(*slider,down,true,down));slider->mouseDrag(mouse(*slider,down+juce::Point<float>(0,-2400),true,down,true));limits &= parameter->getValue()==1;
        slider->mouseDrag(mouse(*slider,down+juce::Point<float>(0,2400),true,down,true));limits &= parameter->getValue()==0;
        slider->mouseUp(mouse(*slider,down+juce::Point<float>(0,2400),false,down,true));undo &= p.undoTemplateEdit4()&&hostValues(p)==before&&!p.canUndoTemplateEdit4();
    }
    check(precision&&independent,"all three native knobs apply exact incremental1/1200 normal and1/12000 Shift steps without changing other host parameters");
    check(stationary&&limits,"native knob X motion and stationary Shift transitions cause no jump while vertical drags reach both exact endpoints");
    check(undo,"each complete native knob drag including precision changes records exactly one full Undo gesture");
    std::cout<<"MEASURE native-knob maximum-normalized-drag-error="<<maximumError<<std::endl;
}
void mixAndUndo() {
    bool linear=true,timing=true,extremes=true;double maxError=0;
    for(int shape=0;shape<16;++shape)for(float move:{-12.5f,0.f,18.75f})for(float slope:{-65.f,0.f,45.f}){
        OpenDuckProcessor::CurveSnapshot full;full.template4=true;full.shape=shape;full.position4=50;full.moveCurve4=move;full.slope4=slope;
        for(float depth:{0.f,25.f,50.f,75.f,100.f}){auto c=full;c.duckMix4=depth;
            for(int i=0;i<=512;++i){const float x=i/512.f,raw=gain(full,x),actual=gain(c,x),expected=1+(raw-1)*depth*.01f;
                maxError=std::max(maxError,double(std::abs(actual-expected)));linear &= std::abs(actual-expected)<2.e-7f;
                timing &= OpenDuckProcessor::curveTimeToRaw(c,x,.5)==OpenDuckProcessor::curveTimeToRaw(full,x,.5);
                extremes &= depth!=0||actual==1;extremes &= depth!=100||actual==raw;}}}
    check(linear&&timing&&extremes,"MIX0/25/50/75/100 follows exact depth interpolation while preserving MOVE SLOPE and source timing across16 shapes");
    std::cout<<"MEASURE MIX-depth maximum-law-error "<<maxError<<std::endl;
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;
    p.beginTemplateEdit4("No change");p.commitTemplateEdit4();
    check(!p.canUndoTemplateEdit4(),"no-op edit creates no Undo checkpoint");
    auto state=[&](){juce::MemoryBlock block;p.getStateInformation(block);return block;};
    struct Checkpoint{std::vector<float> parameters;juce::String memories,label;};std::vector<Checkpoint> history;
    auto memories=[&](){const auto block=state();auto xml=juce::AudioProcessor::getXmlFromBinary(block.getData(),int(block.getSize()));auto* child=xml?xml->getChildByName("ShapeMemories3"):nullptr;return child?child->toString():juce::String{};};
    auto edit=[&](const juce::String& label,auto action){history.push_back({hostValues(p),memories(),label});p.beginTemplateEdit4(label);action();p.commitTemplateEdit4();};
    edit("Preset",[&]{p.applyTemplatePreset(12);});
    edit("Wave",[&]{p.selectFactoryShape(3,0);});
    edit("Knobs",[&]{set(p,"duckMix4",65);set(p,"duration4",-17);set(p,"curveTension0",22);});
    edit("MOVE",[&]{p.shiftTemplateCurve(1,false);});
    edit("SLOPE",[&]{set(p,"slope4",47);});
    edit("BAND",[&]{set(p,"bandEnable4",1);set(p,"bandCutoff4",320);set(p,"bandListen4",1);});
    bool restores=true,labels=true;
    while(!history.empty()){const auto expected=history.back();history.pop_back();labels &= p.getTemplateUndoLabel4()==expected.label;restores &= p.undoTemplateEdit4();
        const auto actual=hostValues(p);restores &= actual.size()==expected.parameters.size();for(size_t i=0;i<actual.size();++i)restores &= std::abs(actual[i]-expected.parameters[i])<1.e-7f;
        restores &= memories()==expected.memories;}
    check(restores&&labels&&!p.canUndoTemplateEdit4(),"sequential Undo restores preset shape all knobs MOVE SLOPE BAND and per-wave memories in reverse order");
    set(p,"duckMix4",37);set(p,"slope4",13);
    check(!p.canUndoTemplateEdit4(),"host automation alone creates no manual-edit Undo history");
    p.beginTemplateEdit4("Outer gesture");p.beginTemplateEdit4("Inner gesture");set(p,"duckMix4",48);p.commitTemplateEdit4();set(p,"slope4",14);p.commitTemplateEdit4();
    check(p.getTemplateUndoLabel4()=="Outer gesture"&&p.undoTemplateEdit4()&&near(p.value("duckMix4"),37)&&near(p.value("slope4"),13)&&!p.canUndoTemplateEdit4(),"nested gestures coalesce into one complete Undo checkpoint");
    struct Listener final:juce::AudioProcessorParameter::Listener {int values=0,starts=0,ends=0;float last=-1;
        void parameterValueChanged(int,float value)override{++values;last=value;}void parameterGestureChanged(int,bool starting)override{starting?++starts:++ends;}} listener;
    auto* mix=p.state.getParameter("duckMix4");edit("Host recall",[&]{set(p,"duckMix4",71);});mix->addListener(&listener);const bool notifiedUndo=p.undoTemplateEdit4();mix->removeListener(&listener);
    check(notifiedUndo&&listener.values>0&&listener.starts==1&&listener.ends==1&&std::abs(listener.last-.37f)<1.e-6f,"Undo notifies the host of the restored MIX value inside a balanced gesture");
    for(int i=1;i<=20;++i){p.beginTemplateEdit4("Bounded edit");set(p,"duckMix4",float(i));p.commitTemplateEdit4();}int undoCount=0;while(p.undoTemplateEdit4())++undoCount;
    check(undoCount==16&&near(p.value("duckMix4"),4),"Undo retains exactly the latest16 complete edit checkpoints");
    auto independent=std::make_unique<OpenDuckProcessor>();p.beginTemplateEdit4("Thread guard");set(p,"duckMix4",80);p.commitTemplateEdit4();bool workerUndo=true,workerCanUndo=true;
    std::thread worker([&]{workerCanUndo=p.canUndoTemplateEdit4();workerUndo=p.undoTemplateEdit4();p.beginTemplateEdit4("Invalid worker edit");p.commitTemplateEdit4();});worker.join();
    check(!workerUndo&&!workerCanUndo&&p.getTemplateUndoLabel4()=="Thread guard"&&!independent->canUndoTemplateEdit4(),"non-UI calls cannot mutate history and independent instances never share Undo");
    {std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());}const bool survived=p.canUndoTemplateEdit4();const auto saved=state();p.setStateInformation(saved.getData(),int(saved.getSize()));
    check(survived&&!p.canUndoTemplateEdit4(),"Undo survives closing its editor and a project state load clears stale history");
    bool invariant=true;
    for(int shape=0;shape<16;++shape){set(p,"duckMix4",0);const auto fullThumb=p.getTemplateShapePreview(shape);set(p,"duckMix4",65);const auto partialThumb=p.getTemplateShapePreview(shape);
        for(int i=0;i<=512;++i)invariant &= gain(fullThumb,i/512.f)==gain(partialThumb,i/512.f);
        invariant &= fullThumb.duckMix4==100&&partialThumb.duckMix4==100;}
    check(invariant,"all16 wave thumbnails retain their full profile when global MIX depth changes");
}
std::vector<juce::Component*> all(juce::Component& parent) {
    std::vector<juce::Component*> result{&parent};for(int i=0;i<parent.getNumChildComponents();++i){auto children=all(*parent.getChildComponent(i));result.insert(result.end(),children.begin(),children.end());}return result;
}
bool rendered(const juce::Component* component,const juce::Component* root) {
    for(auto* current=component;current;current=current->getParentComponent()){if(!current->isVisible())return false;if(current==root)return true;}return false;
}
int cyanRegions(const juce::Image& image) {
    const float scale=image.getWidth()/1728.f;std::vector<int> columns;
    for(int x=juce::roundToInt(581*scale);x<juce::roundToInt(1648*scale);++x){int votes=0;
        for(float designY:{390.f,405.f,420.f}){const auto c=image.getPixelAt(x,juce::roundToInt(designY*scale));
            if(c.getRed()<18&&c.getGreen()>22&&c.getGreen()<95&&c.getGreen()>c.getRed()+12&&c.getBlue()>c.getRed()+18)++votes;}
        if(votes>=2)columns.push_back(x);}
    int runs=0;if(columns.empty())return 0;int start=columns[0],previous=start;
    // The neutral Soft Duck SLOPE window occupies133.6design pixels in a4beat
    // view. Repeated blue-curve glow can join into13pixel AA fragments at small
    // sizes; requiring at least half the real window excludes those fragments.
    for(size_t i=1;i<=columns.size();++i){if(i==columns.size()||columns[i]-previous>juce::jmax(4,int(std::ceil(5*scale)))){if(previous-start>60*scale)++runs;if(i<columns.size())start=columns[i];}if(i<columns.size())previous=columns[i];}
    return runs;
}
int differences(const juce::Image& a,const juce::Image& b) {
    if(a.getWidth()!=b.getWidth()||a.getHeight()!=b.getHeight())return -1;int total=0;
    for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)total += a.getPixelAt(x,y)!=b.getPixelAt(x,y)?1:0;return total;
}
void save(const juce::Image& image,const juce::File& path) {
    auto stream=path.createOutputStream();
    if(!stream||!stream->setPosition(0)||stream->truncate().failed())throw std::runtime_error("Cannot replace preview");
    juce::PNGImageFormat png;if(!png.writeImageToStream(image,*stream))throw std::runtime_error("Cannot encode preview");
}
void previewReplacement(const juce::File& output) {
    const auto path=output.getChildFile("native-png-replacement-proof.png");
    juce::Image previous(juce::Image::RGB,128,64,true),current(juce::Image::RGB,16,12,true);
    for(int y=0;y<previous.getHeight();++y)for(int x=0;x<previous.getWidth();++x)
        previous.setPixelAt(x,y,juce::Colour(juce::uint8(x*17+y*3),juce::uint8(x*5+y*29),juce::uint8(x*31+y*7)));
    current.clear(current.getBounds(),juce::Colour(0xff1874b9));
    save(previous,path);const auto previousBytes=path.getSize();save(current,path);
    const auto decoded=juce::ImageFileFormat::loadFrom(path);
    juce::PNGImageFormat png;juce::MemoryOutputStream expected;png.writeImageToStream(current,expected);
    juce::MemoryBlock disk;const bool read=path.loadFileAsData(disk);
    const auto expectedHash=juce::SHA256(expected.getData(),expected.getDataSize()).toHexString();
    const auto actualHash=juce::SHA256(disk.getData(),disk.getSize()).toHexString();
    check(read&&previousBytes>path.getSize()&&size_t(path.getSize())==expected.getDataSize()
        &&expectedHash==actualHash&&differences(current,decoded)==0,
        "repeated native PNG export replaces old image and trailing bytes with exact current pixels dimensions and encoded hash");
    std::cout<<"MEASURE PNG replacement previous-bytes="<<previousBytes<<" current-bytes="<<path.getSize()
        <<" current-sha256="<<actualHash<<std::endl;
}
std::string functionBytes(const juce::File& source) {
    juce::MemoryBlock data;if(!source.loadFileAsData(data))throw std::runtime_error("Missing frozen or current editor source");
    const std::string bytes(static_cast<const char*>(data.getData()),data.getSize());const auto begin=bytes.find("void SingleDuckEditor::Look::drawRotarySlider");
    const auto end=bytes.find("void SingleDuckEditor::Look::drawButtonBackground",begin);
    if(begin==std::string::npos||end==std::string::npos)throw std::runtime_error("Rotary renderer function not found");return bytes.substr(begin,end-begin);
}
void graphics(const juce::File& root,const juce::File& output) {
    const auto frozen=functionBytes(root.getChildFile("tests/fixtures/graphics-v041-baseline/SingleDuckEditor.cpp"));
    const auto current=functionBytes(root.getChildFile("Source/SingleDuckEditor.cpp"));
    std::cout<<"MEASURE approved-rotary-function-SHA256 "<<juce::SHA256(frozen.data(),frozen.size()).toHexString()<<std::endl;
    check(frozen==current,"approved rotary renderer function remains byte-for-byte identical to its frozen baseline");
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto baseline=std::make_unique<BaselineSingleDuckEditor>(p);
    editor->setSize(1296,684);baseline->setSize(1296,684);editor->setVisible(true);baseline->setVisible(true);
    const char* ids[]{"duckMix4","duration4","curveTension0"};const char* baselineIds[]{"position4","duration4","curveTension0"};bool bounds=true,pixels=true;int inspected=0;
    for(float u:{0.f,.5f,1.f}) {
        for(int i=0;i<3;++i){auto* slider=dynamic_cast<juce::Slider*>(named(*editor,ids[i]));if(!slider)throw std::runtime_error("Missing native knob");const float value=float(slider->getMinimum()+u*(slider->getMaximum()-slider->getMinimum()));set(p,ids[i],value);set(p,baselineIds[i],value);}
        const auto live=editor->createComponentSnapshot(editor->getLocalBounds(),true),old=baseline->createComponentSnapshot(baseline->getLocalBounds(),true);
        for(int i=0;i<3;++i){const char* id=ids[i];auto* knob=named(*editor,id);auto* original=named(*baseline,baselineIds[i]);bounds &= knob&&original&&knob->getBounds()==original->getBounds();if(!knob||!original)continue;
            auto* liveSlider=dynamic_cast<juce::Slider*>(knob);auto* oldSlider=dynamic_cast<juce::Slider*>(original);
            const double expected=liveSlider->getMinimum()+u*(liveSlider->getMaximum()-liveSlider->getMinimum());
            bounds &= std::abs(liveSlider->getValue()-expected)<.0001&&std::abs(oldSlider->getValue()-expected)<.0001;
            const auto a=live.getClippedImage(knob->getBounds()),b=old.getClippedImage(original->getBounds());const int count=differences(a,b);pixels &= count==0;inspected+=a.getWidth()*a.getHeight();
            std::cout<<"MEASURE approved-knob "<<id<<" normalized="<<u<<" differing-pixels="<<count<<std::endl;}
        save(live,output.getChildFile("concept-knobs-"+juce::String(u,1).replaceCharacter('.','_')+"-75pct.png"));
    }
    check(bounds&&pixels&&inspected>200000,"all3native knobs match approved baseline pixels exactly at min midpoint and max with unchanged bounds");
    set(p,"duckMix4",100);set(p,"position4",50);set(p,"duration4",0);set(p,"curveTension0",0);set(p,"moveCurve4",0);set(p,"slope4",0);
    bool missingEdit=true,missingAnalysis=true,arrows=true,controls=true;
    for(auto* component:all(*editor))if(auto* button=dynamic_cast<juce::Button*>(component))if(rendered(button,editor.get())){
        missingEdit &= !button->getButtonText().containsIgnoreCase("EDIT POINT")&&!button->getName().containsIgnoreCase("editPoints");
        const auto text=button->getButtonText();missingAnalysis &= !text.containsIgnoreCase("ANALISI")&&!text.containsIgnoreCase("ANALYZE")&&text!="AI"&&text!="MANUAL"&&text!="5s"&&text!="10s";}
    for(int i=0;i<4;++i){auto* button=dynamic_cast<juce::TextButton*>(named(*editor,"moveCurveArrow4_"+juce::String(i)));arrows &= button&&button->isVisible();if(button){set(p,"moveCurve4",0);button->onClick();const float expected=i==0?-6.25f:i==1?-.390625f:i==2?.390625f:6.25f;arrows &= near(p.value("moveCurve4"),expected);}}
    for(const char* id:{"bandEnable4","bandCutoff4","bandListen4","undoCurve4"})controls &= named(*editor,id)!=nullptr;
    check(missingEdit&&missingAnalysis&&arrows&&controls,"native concept removes Edit Points and all analysis entries while exposing4functional arrows Undo and BAND");
    auto* undo=dynamic_cast<juce::TextButton*>(named(*editor,"undoCurve4"));const float oldMove=p.value("moveCurve4"),oldSlope=p.value("slope4");p.beginTemplateEdit4("UI Undo test");set(p,"moveCurve4",12);set(p,"slope4",35);p.commitTemplateEdit4();editor->createComponentSnapshot(editor->getLocalBounds(),true);if(undo)undo->onClick();
    check(undo&&near(p.value("moveCurve4"),oldMove)&&near(p.value("slope4"),oldSlope),"native circular Undo click restores the complete preceding curve edit");
    auto custom=p.getCurveSnapshot(0);custom.custom=true;custom.shape=16;custom.count=4;custom.points[0]={0,0,0};custom.points[1]={.3f,.15f,0};custom.points[2]={.65f,.8f,0};custom.points[3]={1,1,0};p.setCurveSnapshot(0,custom,"QA custom legacy state");
    p.setEditPointsEnabled(true,0);const auto editOn=editor->createComponentSnapshot(editor->getLocalBounds(),true);
    p.setEditPointsEnabled(false,0);const auto editOff=editor->createComponentSnapshot(editor->getLocalBounds(),true);
    const auto graphArea=juce::Rectangle<int>(386,183,856,225);
    check(differences(editOn.getClippedImage(graphArea),editOff.getClippedImage(graphArea))==0,"legacy edit flags never expose point nodes or change the new native graph");
    p.selectFactoryShape(0,0);set(p,"position4",50);set(p,"moveCurve4",0);set(p,"slope4",0);
    for(int width:{864,1296,1728}) {
        editor->setSize(width,juce::roundToInt(width*912.f/1728));const float scale=width/1728.f;
        auto* graph=named(*editor,"duckGraph4");if(!graph)throw std::runtime_error("Missing native graph");hover(*graph,true);
        bool within=true;for(auto* component:all(*editor))if(rendered(component,editor.get()))within &= editor->getLocalArea(component,component->getLocalBounds()).getIntersection(editor->getLocalBounds())==editor->getLocalArea(component,component->getLocalBounds());
        const auto shot=editor->createComponentSnapshot(editor->getLocalBounds(),true);int brandInk=0;
        const auto brand=juce::Rectangle<int>(juce::roundToInt(1193*scale),juce::roundToInt(51*scale),juce::roundToInt(376*scale),juce::roundToInt(58*scale));
        for(int y=brand.getY();y<brand.getBottom();++y)for(int x=brand.getX();x<brand.getRight();++x){const auto c=shot.getPixelAt(x,y);if(c.getRed()>100&&c.getGreen()>110&&c.getBlue()>110)++brandInk;}
        auto* preset=named(*editor,"preset4");const auto expected=juce::Rectangle<int>(juce::roundToInt(627*scale),juce::roundToInt(48*scale),juce::roundToInt(440*scale),juce::roundToInt(73*scale));
        const auto bandBounds=juce::Rectangle<int>(juce::roundToInt(996*scale),juce::roundToInt(770*scale),juce::roundToInt(657*scale),juce::roundToInt(67*scale));bool bandInside=true;
        for(const char* id:{"bandEnable4","bandCutoff4","bandListen4"}){auto* component=named(*editor,id);bandInside &= component&&bandBounds.contains(component->getBounds());}
        const int regions=cyanRegions(shot);std::cout<<"MEASURE native cyan-SLOPE-region-count width"<<width<<" "<<regions<<std::endl;
        check(within&&preset&&preset->getBounds()==expected&&regions==1&&bandInside&&brandInk>int(100*scale),"native concept preserves bounds preset brand one cyan SLOPE region and expanded BAND width"+std::to_string(width));
        save(shot,output.getChildFile("concept-final-"+juce::String(width)+".png"));
        hover(*graph,false);
    }
    p.state.state.setProperty("templateAiView",true,nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> oldAiState(p.createEditor());oldAiState->setVisible(true);bool noOldAnalysis=true;
    for(auto* component:all(*oldAiState))if(auto* button=dynamic_cast<juce::Button*>(component))if(rendered(button,oldAiState.get())){
        const auto text=button->getButtonText();noOldAnalysis &= !text.containsIgnoreCase("ANALISI")&&!text.containsIgnoreCase("ANALYZE")&&text!="AI"&&text!="MANUAL"&&text!="5s"&&text!="10s";}
    check(noOldAnalysis,"old AI-view state never reintroduces analysis controls in the native concept");
}

void mouseBehavior(const juce::File& output) {
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;
    p.state.state.setProperty("templateSlopeGuide4",true,nullptr);p.setInputScopeBeats(1);set(p,"position4",50);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1728,912);editor->setVisible(true);
    auto* graph=named(*editor,"duckGraph4");if(!graph)throw std::runtime_error("Missing native graph mouse target");
    auto picture=[&](){return editor->createComponentSnapshot(editor->getLocalBounds(),true);};
    auto regionCount=[&](){picture();return int(graph->getProperties().getWithDefault("slopeRegionCount4",-1));};
    const auto initial=picture();
    check(cyanRegions(initial)==0&&regionCount()==0,"SLOPE starts hidden even when a legacy saved always-on flag is true");
    const auto beforeHover=hostValues(p);hover(*graph,true);const auto entered=picture();
    check(cyanRegions(entered)==1&&regionCount()==1&&hostValues(p)==beforeHover,"real JUCE mouse entry reveals one SLOPE guide without changing any host parameter");
    bool guideAligned=true;
    for(float slope:{-100.f,0.f,100.f}){set(p,"slope4",slope);const auto shot=picture();const auto bounds=odx::template4::slopeTargetBounds(0,slope,1);
        for(float boundary:{bounds.start,bounds.end}){const int expected=juce::roundToInt(580+1069*boundary);int rows=0;
            for(int y=300;y<470;y+=10){bool line=false;for(int x=std::max(580,expected-3);x<=std::min(1649,expected+3);++x){const auto c=shot.getPixelAt(x,y);line |= c.getRed()<50&&c.getGreen()>130&&c.getBlue()>130;}rows+=line?1:0;}guideAligned &= rows>=3;}
        guideAligned &= int(graph->getProperties().getWithDefault("slopeRegionCount4",0))==1&&p.value("moveCurve4")==0;}set(p,"slope4",0);
    check(guideAligned&&hostValues(p)==beforeHover,"measured SLOPE guide follows its calibrated internal-control target window at both extremes without changing MOVE or any knob");
    save(entered,output.getChildFile("concept-R5-mouse-enter.png"));
    hover(*graph,false);
    check(regionCount()==0&&hostValues(p)==beforeHover,"real JUCE mouse exit hides SLOPE without changing any host parameter");
    const auto slopeBounds=odx::template4::slopeBounds(0,1);
    const juce::Point<float> centre(65+1069*(slopeBounds.start+slopeBounds.end)*.5f,150),outside(float(graph->getWidth()+40),float(graph->getHeight()+30));
    hover(*graph,true);const auto beforeSlope=hostValues(p);
    graph->mouseDown(mouse(*graph,centre,true));graph->mouseDrag(mouse(*graph,centre+juce::Point<float>(60,0),true,centre,true));
    const auto slopeIndex=p.state.getParameter("slope4")->getParameterIndex();const auto afterSlope=hostValues(p);bool onlySlope=true;
    for(size_t i=0;i<beforeSlope.size();++i)if(int(i)!=slopeIndex)onlySlope &= beforeSlope[i]==afterSlope[i];
    check(p.value("slope4")>1&&near(p.value("moveCurve4"),0)&&onlySlope,"mouse drag inside the cyan region changes SLOPE alone, preserving MOVE and all three knobs");
    graph->mouseExit(mouse(*graph,outside,true,centre,true));const bool held=regionCount()==1;
    const float previousSlope=p.value("slope4");graph->mouseDrag(mouse(*graph,outside,true,centre,true));
    check(held&&regionCount()==1&&p.value("slope4")>=previousSlope,"SLOPE drag retains mouse capture and its guide while moving outside the graph");
    graph->mouseUp(mouse(*graph,outside,true,centre,true));
    check(regionCount()==0,"releasing the captured drag outside the graph immediately hides SLOPE");
    p.resetTemplateCurveEdits();const juce::Point<float> outsideSlope(65+1069*.8f,150);hover(*graph,true);const auto beforeMove=hostValues(p);
    graph->mouseDown(mouse(*graph,outsideSlope,true));graph->mouseDrag(mouse(*graph,outsideSlope+juce::Point<float>(40,0),true,outsideSlope,true));
    const auto moveIndex=p.state.getParameter("moveCurve4")->getParameterIndex();const auto afterMove=hostValues(p);bool onlyMove=true;
    for(size_t i=0;i<beforeMove.size();++i)if(int(i)!=moveIndex)onlyMove &= beforeMove[i]==afterMove[i];
    check(p.value("moveCurve4")>1&&near(p.value("slope4"),0)&&onlyMove,"mouse drag outside the cyan region changes MOVE alone with no knob or SLOPE cross-talk");
    graph->mouseUp(mouse(*graph,outsideSlope+juce::Point<float>(40,0),true,outsideSlope,true));hover(*graph,false);
    p.resetTemplateCurveEdits();set(p,"mirrorH4",1);hover(*graph,true);const auto mirrorBefore=hostValues(p);const juce::Point<float> mirroredCentre(65+1069*(1-(slopeBounds.start+slopeBounds.end)*.5f),150);
    graph->mouseDown(mouse(*graph,mirroredCentre,true));graph->mouseDrag(mouse(*graph,mirroredCentre+juce::Point<float>(40,0),true,mirroredCentre,true));graph->mouseUp(mouse(*graph,mirroredCentre+juce::Point<float>(40,0),true,mirroredCentre,true));
    bool mirrorOnly=true;const auto mirrorAfter=hostValues(p);for(size_t i=0;i<mirrorBefore.size();++i)if(int(i)!=slopeIndex)mirrorOnly &= mirrorBefore[i]==mirrorAfter[i];
    check(mirrorOnly&&p.value("slope4")<0&&near(p.value("moveCurve4"),0),"MIRROR H reflects the measured SLOPE hit window and reverses its drag direction without editing other controls");
    p.resetTemplateCurveEdits();set(p,"mirrorH4",0);hover(*graph,false);
    const auto beforeBrand=hostValues(p);const auto brandBeforeImage=picture();auto* brand=editor->getComponentAt(1381,80);bool nonInteractive=brand&&dynamic_cast<juce::Button*>(brand)==nullptr&&dynamic_cast<juce::Slider*>(brand)==nullptr&&dynamic_cast<juce::ComboBox*>(brand)==nullptr;
    if(brand){const auto point=brand->getLocalPoint(editor.get(),juce::Point<int>(1381,80)).toFloat();brand->mouseDown(mouse(*brand,point,true));brand->mouseUp(mouse(*brand,point,true));
        brand->mouseDown(mouse(*brand,point,true,{-1,-1},false,true));brand->mouseUp(mouse(*brand,point,true,{-1,-1},false,true));}
    std::cout<<"MEASURE brand-hit "<<(brand==editor.get()?"editor":brand?brand->getName().toStdString():"null")<<" parameter-differences "<<(hostValues(p)==beforeBrand?0:1)<<std::endl;
    check(nonInteractive&&hostValues(p)==beforeBrand&&differences(brandBeforeImage,picture())==0,"BLACK CIRCUIT badge ignores left and right clicks, changing no host parameter or visible panel");
    p.resetTemplateCurveEdits();save(picture(),output.getChildFile("concept-R5-brand-idle.png"));
}

void miniSettings(const juce::File& output) {
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;p.prepareToPlay(48000,256);p.state.state.setProperty("templateCurveView4",2,nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1728,912);editor->setVisible(true);
    editor->mouseDown(mouse(*editor,{480,850},true,{-1,-1},false,true));
    auto* anti=dynamic_cast<juce::TextButton*>(named(*editor,"antiClick4"));auto* loop=dynamic_cast<juce::TextButton*>(named(*editor,"triggerLoop4"));
    if(!anti||!loop)throw std::runtime_error("Missing real mini trigger/anti-click controls");auto* mini=anti->getParentComponent();
    std::vector<juce::Slider*> settings;for(auto* child:mini->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))settings.push_back(slider);
    std::sort(settings.begin(),settings.end(),[](const auto* a,const auto* b){return a->getY()<b->getY();});if(settings.size()!=4)throw std::runtime_error("Unexpected detector mini slider count");auto* end=settings[3];
    auto choose=[&](const juce::String& text){for(auto* child:all(*editor))if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==text){button->onClick();return;}throw std::runtime_error("Missing trigger button");};
    auto tick=[](){juce::MessageManager::getInstance()->runDispatchLoopUntil(85);};
    const bool syncDisabled=rendered(anti,editor.get())&&anti->isEnabled()&&!loop->isEnabled()&&!end->isEnabled();
    choose("MIDI");tick();const bool midiEnabled=anti->isEnabled()&&loop->isEnabled()&&end->isEnabled()&&p.getLatencySamples()==289;
    const float smoothing=p.value("v3Smooth0"),beforeEnd=p.value("v3End0");if(end->onDragStart)end->onDragStart();end->setValue(.37,juce::sendNotificationSync);if(end->onDragEnd)end->onDragEnd();
    const bool edited=near(p.value("v3End0"),.37f)&&p.value("v3Smooth0")==smoothing&&p.canUndoTemplateEdit4();const bool undoEnd=p.undoTemplateEdit4();tick();
    check(syncDisabled&&midiEnabled&&edited&&undoEnd&&near(p.value("v3End0"),beforeEnd),"native mini disables SYNC trigger tools and exposes undoable one-shot END in MIDI while leaving legacy smoothing untouched");
    save(editor->createComponentSnapshot(editor->getLocalBounds(),true),output.getChildFile("concept-mini-MIDI-one-shot-1728.png"));
    const int viewBefore=int(p.state.state.getProperty("templateCurveView4",-1));loop->onClick();tick();const bool independent=p.value("v3Loop0")==1&&!end->isEnabled()&&int(p.state.state.getProperty("templateCurveView4",-1))==viewBefore;
    const bool undone=p.undoTemplateEdit4();tick();
    check(independent&&undone&&p.value("v3Loop0")==0&&end->isEnabled(),"native LOOP/ONE SHOT mini controls audio looping independently of graph view and restores through Undo");
    anti->onClick();tick();const bool zero=p.value("antiClick4")==0&&p.getLatencySamples()==0;const bool undoAnti=p.undoTemplateEdit4();tick();
    check(zero&&undoAnti&&p.value("antiClick4")==1&&p.getLatencySamples()==289,"native anti-click toggle publishes actual MIDI PDC and Undo restores its latency setting");
    bool layout=true;for(int width:{864,1296,1728}){editor->setSize(width,width*912/1728);const auto shot=editor->createComponentSnapshot(editor->getLocalBounds(),true);
        std::vector<juce::Component*> controls;for(auto* child:mini->getChildren())if(child->isVisible()){layout &= mini->getLocalBounds().contains(child->getBounds());controls.push_back(child);}
        for(size_t a=0;a<controls.size();++a)for(size_t b=a+1;b<controls.size();++b)layout &= !controls[a]->getBounds().intersects(controls[b]->getBounds());
        save(shot,output.getChildFile("concept-mini-"+juce::String(width)+".png"));}
    check(layout,"native detector mini rows trigger controls END and latency label fit without overlaps at three UI sizes");
}

void conceptDemos(const juce::File& root,const juce::File& output) {
    const auto file=root.getChildFile("tests/fixtures/audio/LAB-Kick-145.wav");juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(),true));
    if(!reader)throw std::runtime_error("Missing real kick fixture for one-cycle concept");
    juce::AudioBuffer<float> fixture(2,int(reader->lengthInSamples));reader->read(&fixture,0,fixture.getNumSamples(),0,true,true);
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.tempo=145;clock.rate=reader->sampleRate;
    p.setPlayHead(&clock);p.setInputScopeBeats(1);p.prepareToPlay(clock.rate,257);
    for(int n=0;n<int(clock.rate*3);n+=257){const int count=std::min(257,int(clock.rate*3)-n);juce::AudioBuffer<float> block(2,count);juce::MidiBuffer midi;
        for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i)block.setSample(ch,i,fixture.getSample(ch,(n+i)%fixture.getNumSamples()));clock.sample=n;p.processBlock(block,midi);}
    const auto frame=p.getInputScopeFrame();
    check(frame.complete&&p.value("duckMix4")==0&&p.value("position4")==50&&p.value("bypass")==1&&p.getInputScopeBeats()==1,
          "one-cycle concept uses real kick audio and only changes the view while retaining neutral INIT");
    auto marker=[](juce::Component& editor){auto* graph=named(editor,"duckGraph4");return graph&&(bool)graph->getProperties().getWithDefault("onsetVisible4",false);};
    auto snapshot=[&](const char* filename,bool showHover=false){std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1728,912);
        if(showHover)if(auto* graph=named(*editor,"duckGraph4"))hover(*graph,true);
        save(editor->createComponentSnapshot(editor->getLocalBounds(),true),output.getChildFile(filename));};
    snapshot("concept-one-cycle.png");
    const auto onset=odx::view::isolateSingleHit(frame.mainMin,frame.mainMax,frame.periodSeconds,frame.bins);
    const std::array<float,5> parameterBefore{{p.value("position4"),p.value("duration4"),p.value("curveTension0"),p.value("moveCurve4"),p.value("slope4")}};
    bool guidePair=true,realMarker=true;juce::Image onsetGutter;
    p.state.state.setProperty("templateSlopeGuide4",true,nullptr);
    for(bool guide:{false,true}) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(1728,912);
        if(guide)if(auto* graph=named(*editor,"duckGraph4"))hover(*graph,true);
        const auto shot=editor->createComponentSnapshot(editor->getLocalBounds(),true);const int regions=cyanRegions(shot);
        guidePair &= regions==(guide?1:0);
        auto* graph=named(*editor,"duckGraph4");realMarker &= onset.onsetBin>=frame.firstBin&&marker(*editor);
        if(graph){const float expectedX=580+onset.onsetBin*1069/512.f;
            const float actualX=graph->getX()+(float)graph->getProperties().getWithDefault("onsetX4",-1000.f);
            const int actualBin=(int)graph->getProperties().getWithDefault("onsetBin4",-1);int whiteLineRows=0;
            for(int y:{310,340,410,470}){bool white=false;for(int x=juce::roundToInt(expectedX)-2;x<=juce::roundToInt(expectedX)+2;++x){const auto c=shot.getPixelAt(x,y);white |= c.getRed()>40&&c.getGreen()>50&&c.getBlue()>50;}whiteLineRows += white?1:0;}
            realMarker &= actualBin==onset.onsetBin&&std::abs(actualX-expectedX)<1&&whiteLineRows>=3;
            guidePair &= (int)graph->getProperties().getWithDefault("slopeRegionCount4",-1)==(guide?1:0);
            std::cout<<"MEASURE R5 real-onset-bin "<<onset.onsetBin<<" expected-x "<<expectedX<<" actual-x "<<actualX<<" white-line-rows "<<whiteLineRows<<std::endl;}
        onsetGutter=shot.getClippedImage({515,249,60,259});
        save(shot,output.getChildFile(guide?"concept-R5-hover.png":"concept-R5-idle.png"));
        if(guide)if(auto* graph=named(*editor,"duckGraph4")){hover(*graph,false);const auto left=editor->createComponentSnapshot(editor->getLocalBounds(),true);
            guidePair &= cyanRegions(left)==0;save(left,output.getChildFile("concept-R5-exit.png"));}
    }
    check(guidePair,"native real-audio R5 SLOPE guide is hidden at rest, visible only on mouse hover and hidden after exit");
    const auto after=p.getInputScopeFrame();const std::array<float,5> parameterAfter{{p.value("position4"),p.value("duration4"),p.value("curveTension0"),p.value("moveCurve4"),p.value("slope4")}};
    juce::MemoryBlock guideState;p.getStateInformation(guideState);auto restored=std::make_unique<OpenDuckProcessor>();restored->setStateInformation(guideState.getData(),int(guideState.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> restoredEditor(restored->createEditor());restoredEditor->setSize(1728,912);const auto restoredShot=restoredEditor->createComponentSnapshot(restoredEditor->getLocalBounds(),true);
    check(parameterBefore==parameterAfter&&frame.mainMin==after.mainMin&&frame.mainMax==after.mainMax&&cyanRegions(restoredShot)==0,
          "mouse hover leaves audio parameters and captured PCM unchanged and never persists an always-on guide");
    auto blank=std::make_unique<OpenDuckProcessor>();
    std::unique_ptr<juce::AudioProcessorEditor> blankEditor(blank->createEditor());blankEditor->setSize(1728,912);const auto blankShot=blankEditor->createComponentSnapshot(blankEditor->getLocalBounds(),true);
    check(realMarker&&differences(onsetGutter,blankShot.getClippedImage({515,249,60,259}))==0,
          "native INIZIO marker follows the real kick onset with painted white line and leaves percentage-label pixels unchanged");
    check(!marker(*blankEditor),"native INIZIO marker stays absent before any real audio arrives");
    Clock toneClock;blank->setPlayHead(&toneClock);blank->setInputScopeBeats(1);blank->prepareToPlay(48000,256);
    for(int n=0;n<48000*3;n+=256){juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
        for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i)block.setSample(ch,i,float(.2*std::sin(2*juce::MathConstants<double>::pi*997*(n+i)/48000.)));toneClock.sample=n;blank->processBlock(block,midi);}
    std::unique_ptr<juce::AudioProcessorEditor> toneEditor(blank->createEditor());toneEditor->setSize(1728,912);toneEditor->createComponentSnapshot(toneEditor->getLocalBounds(),true);
    check(!marker(*toneEditor),"continuous real tone never creates a fake kick-onset marker");
    set(p,"moveCurve4",18.75f);set(p,"slope4",45);snapshot("concept-shifted-slope.png",true);
    p.resetTemplateCurveEdits();set(p,"bandEnable4",1);set(p,"duckMix4",65);set(p,"position4",50);set(p,"bypass",0);
    set(p,"bandCutoff4",160);snapshot("concept-band-on-160Hz.png");set(p,"bandCutoff4",320);snapshot("concept-band-on-320Hz.png");
    check(true,"native one-cycle shifted-SLOPE and BAND160/320Hz concept previews exported");
}

void viewConcepts(const juce::File& root,const juce::File& output) {
    juce::WavAudioFormat wav;const auto file=root.getChildFile("tests/fixtures/audio/LAB-Kick-145.wav");
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(),true));
    if(!reader)throw std::runtime_error("Missing actual PCM for paired views");
    juce::AudioBuffer<float> fixture(2,int(reader->lengthInSamples));reader->read(&fixture,0,fixture.getNumSamples(),0,true,true);
    auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=reader->sampleRate;clock.tempo=145;
    p.setPlayHead(&clock);p.prepareToPlay(clock.rate,257);
    for(int n=0;n<int(clock.rate*6);n+=257){const int count=std::min(257,int(clock.rate*6)-n);juce::AudioBuffer<float> block(2,count);juce::MidiBuffer midi;
        for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i)block.setSample(ch,i,fixture.getSample(ch,(n+i)%fixture.getNumSamples()));clock.sample=n;p.processBlock(block,midi);}
    const auto frame=p.getInputScopeFrame();const auto hit=odx::view::isolateSingleHit(frame.mainMin,frame.mainMax,frame.periodSeconds,frame.bins);
    check(frame.complete&&hit.detectedOnsets==4&&frame.firstBin==0,"paired concepts share one complete capture with four actual kick onsets");
    p.beginTemplateEdit4("MIX 65%");set(p,"duckMix4",65);set(p,"bypass",0);p.commitTemplateEdit4();
    const auto parameters=hostValues(p);bool exclusive=true,physical=true,pcm=true,undoReady=true,persistent=true,visibleWave=true,visibleOnset=true;
    for(int width:{1728,864}){
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(width,width*912/1728);editor->setVisible(true);
        auto* graph=named(*editor,"duckGraph4");if(!graph)throw std::runtime_error("Missing graph for view modes");
        for(int mode:{0,1,2}){auto* button=dynamic_cast<juce::TextButton*>(named(*editor,"curveView4_"+juce::String(mode)));if(!button)throw std::runtime_error("Missing native view selector");button->onClick();
            const auto picture=editor->createComponentSnapshot(editor->getLocalBounds(),true);int active=0;
            for(int other=0;other<3;++other){auto* b=dynamic_cast<juce::TextButton*>(named(*editor,"curveView4_"+juce::String(other)));exclusive &= b!=nullptr;if(b){active+=b->getToggleState()?1:0;exclusive &= b->getToggleState()==(mode==other);}}
            exclusive &= active==1;const auto& props=graph->getProperties();const bool loop=bool(props.getWithDefault("curveViewEffectiveLoop4",false));
            const double cycles=double(props.getWithDefault("viewCycleCount4",-1.)),seconds=double(props.getWithDefault("viewDurationSeconds4",-1.));
            physical &= int(props.getWithDefault("curveViewRequested4",-1))==mode&&loop==(mode!=1)&&std::abs(cycles-(mode==1?1.:4.))<1.e-5
                &&std::abs(seconds-(mode==1?60./145.:240./145.))<1.e-7;
            if(mode==1)physical &= double(props.getWithDefault("viewCycleStart4",-1.))==0;
            auto* undo=dynamic_cast<juce::TextButton*>(named(*editor,"undoCurve4"));undoReady &= undo&&undo->isEnabled();
            const auto after=p.getInputScopeFrame();pcm &= after.generation==frame.generation&&after.mainMin==frame.mainMin&&after.mainMax==frame.mainMax&&hostValues(p)==parameters;
            // Observe the actual neutral-grey PCM pixels around the waveform
            // centre, away from captions, controls and the onset marker at the
            // plot edge. Grid/cyan curve pixels cannot satisfy this test. A
            // valid capture alone does not prove its chosen crop is visible.
            const double scale=width/1728.;int greyPixels=0,greyColumns=0;
            for(int x=int(std::ceil(590*scale));x<int(1639*scale);++x){int column=0;
                for(int y=int(std::ceil(335*scale));y<int(423*scale);++y){const auto c=picture.getPixelAt(x,y);
                    if(c.getRed()>=60&&std::abs(int(c.getRed())-int(c.getGreen()))<=25
                        &&std::abs(int(c.getRed())-int(c.getBlue()))<=30)++column;}
                greyPixels+=column;greyColumns+=column>0?1:0;}
            visibleWave &= greyPixels>=width/20&&greyColumns>=width/60;
            visibleOnset &= bool(props.getWithDefault("onsetVisible4",false))
                &&int(props.getWithDefault("onsetBin4",-1))==hit.onsetBin;
            std::cout<<"MEASURE native-view width="<<width<<" requested="<<mode<<" loop="<<loop<<" cycles="<<cycles<<" seconds="<<seconds
                <<" capture-phase="<<frame.curvePhaseStart<<" view-start="<<double(props.getWithDefault("viewStartSeconds4",-1000.))
                <<" grey-wave-pixels="<<greyPixels<<" grey-wave-columns="<<greyColumns<<std::endl;
            if(mode!=0)save(picture,output.getChildFile("concept-MIX65-"+juce::String(mode==1?"single":"loop")+"-"+juce::String(width)+".png"));
            if(width==1728){hover(*graph,true);const auto withHover=editor->createComponentSnapshot(editor->getLocalBounds(),true);physical &= int(graph->getProperties().getWithDefault("slopeRegionCount4",0))==1;
                if(mode!=0)save(withHover,output.getChildFile("concept-MIX65-"+juce::String(mode==1?"single":"loop")+"-hover.png"));hover(*graph,false);}
        }
        juce::MemoryBlock saved;p.getStateInformation(saved);auto restored=std::make_unique<OpenDuckProcessor>();restored->setStateInformation(saved.getData(),int(saved.getSize()));
        std::unique_ptr<juce::AudioProcessorEditor> fresh(restored->createEditor());fresh->setVisible(true);fresh->createComponentSnapshot(fresh->getLocalBounds(),true);
        auto* loopButton=dynamic_cast<juce::TextButton*>(named(*fresh,"curveView4_2"));persistent &= int(restored->state.state.getProperty("templateCurveView4",-1))==2&&loopButton&&loopButton->getToggleState();
    }
    check(exclusive&&persistent,"AUTO1CURVA LOOP native selector is mutually exclusive and restores its saved explicit mode");
    check(physical,"native AUTO LOOP and canonical1CURVA keep exact physical axes one versus four cycles and one hover SLOPE region at both sizes");
    check(visibleWave,"actual four-kick PCM paints a visible grey waveform in AUTO SINGLE and LOOP native crops at both sizes");
    check(visibleOnset,"actual bin0 onset marker remains visible at the left plot edge in SINGLE and LOOP despite phase rounding");
    check(pcm&&undoReady,"switching display modes preserves every host parameter and original PCM while circular Undo remains available");
    std::array<float,2> moveDeltas{};bool slopeDrag=true,moveDrag=true;
    std::unique_ptr<juce::AudioProcessorEditor> interactive(p.createEditor());interactive->setSize(1728,912);interactive->setVisible(true);
    auto* graph=named(*interactive,"duckGraph4");
    for(int mode:{1,2}){
        auto* button=dynamic_cast<juce::TextButton*>(named(*interactive,"curveView4_"+juce::String(mode)));button->onClick();hover(*graph,true);
        const auto shot=interactive->createComponentSnapshot(interactive->getLocalBounds(),true);int start=-1,previous=-1,bestStart=-1,bestEnd=-1;
        for(int x=581;x<=1649;++x){const auto c=shot.getPixelAt(std::min(x,1648),405);const bool shade=x<1649&&c.getRed()<18&&c.getGreen()>22&&c.getGreen()<95&&c.getGreen()>c.getRed()+12&&c.getBlue()>c.getRed()+18;
            if(shade){if(start<0)start=x;previous=x;}else if(start>=0&&x-previous>5){if(previous-start>bestEnd-bestStart){bestStart=start;bestEnd=previous;}start=-1;}}
        if(start>=0&&previous-start>bestEnd-bestStart){bestStart=start;bestEnd=previous;}
        slopeDrag &= bestEnd-bestStart>60;const juce::Point<float> centre((bestStart+bestEnd)*.5f-graph->getX(),150);
        const auto before=hostValues(p);graph->mouseDown(mouse(*graph,centre,true));graph->mouseDrag(mouse(*graph,centre+juce::Point<float>(20,0),true,centre,true));graph->mouseUp(mouse(*graph,centre+juce::Point<float>(20,0),true,centre,true));
        const auto after=hostValues(p);const int slopeIndex=p.state.getParameter("slope4")->getParameterIndex();
        for(size_t i=0;i<before.size();++i)if(int(i)!=slopeIndex)slopeDrag &= before[i]==after[i];
        slopeDrag &= p.value("slope4")>0&&p.undoTemplateEdit4();interactive->createComponentSnapshot(interactive->getLocalBounds(),true);
        const juce::Point<float> outsideSlope(80,150);const auto beforeMove=hostValues(p);const float moveBefore=p.value("moveCurve4");
        graph->mouseDown(mouse(*graph,outsideSlope,true));graph->mouseDrag(mouse(*graph,outsideSlope+juce::Point<float>(40,0),true,outsideSlope,true));graph->mouseUp(mouse(*graph,outsideSlope+juce::Point<float>(40,0),true,outsideSlope,true));
        const auto afterMove=hostValues(p);const int moveIndex=p.state.getParameter("moveCurve4")->getParameterIndex();
        for(size_t i=0;i<beforeMove.size();++i)if(int(i)!=moveIndex)moveDrag &= beforeMove[i]==afterMove[i];
        moveDeltas[(size_t)(mode-1)]=p.value("moveCurve4")-moveBefore;moveDrag &= p.undoTemplateEdit4();hover(*graph,false);
    }
    check(slopeDrag&&moveDrag&&moveDeltas[0]>0&&std::abs(moveDeltas[1]/moveDeltas[0]-4)<.001f,
          "real native drags edit SLOPE alone and scale MOVE by the exact displayed one-versus-four-cycle axis");
    const auto margin=odx::view::curveViewWindow(false,.25,.5,.9,0,0,512);
    check(margin.cycleAtStart==0&&std::abs(margin.startSeconds+.45)<1.e-12&&margin.captureFractionAtDisplay(0)<0&&margin.captureFractionAtDisplay(1)>0,
          "canonical single-cycle geometry exposes unknown leading margins without wrapping or inventing captured audio");
}

void triggeredTimelineViews(const juce::File& output) {
    // The oracle is the actual rendered PCM on a non-zero constant MAIN.
    // SC is a different signal, with deliberately non-musical trigger times.
    // No envelope formula is copied into this test.
    constexpr double rate=48000.;constexpr float input=.15f;
    constexpr std::array<double,3> eventTimes{{.100,.370,1.080}};
    bool telemetry=true,triggerTelemetry=true,timeAxis=true,origins=true,idle=true,held=true,nativeLoop=true,nativeSingle=true;
    double maximumGainError=0,maximumOriginError=0;int comparedSamples=0,triggerSamples=0,comparedPixels=0;
    for(double tempo:{120.,138.})for(int trigger:{1,2})for(int anti:{0,1}) {
        auto owned=std::make_unique<OpenDuckProcessor>();auto& p=*owned;Clock clock;clock.rate=rate;clock.tempo=tempo;p.setPlayHead(&clock);
        set(p,"profileModel4",1);set(p,"bypass",0);set(p,"duckMix4",65);set(p,"antiClick4",float(anti));set(p,"bandEnable4",0);
        p.setSectorParameter(0,"trigger",float(trigger));p.setSectorParameter(0,"loop",0);p.setSectorParameter(0,"speed",2);p.selectFactoryShape(10,0);
        auto buses=p.getBusesLayout();buses.inputBuses.set(1,juce::AudioChannelSet::stereo());
        if(!p.setBusesLayout(buses))throw std::runtime_error("Timeline test needs the real external SC bus");
        p.setInputScopeBeats(4);p.prepareToPlay(rate,257);const int latency=p.getLatencySamples();
        // The fixture's external tempo and explicit quarter-note setting are
        // the timing oracle. Processor BPM is not delivered until processBlock.
        const double cycle=60./tempo,period=240./tempo;
        std::vector<float> rendered;int64_t sample=0;std::array<int64_t,3> eventSamples{};
        for(size_t i=0;i<eventTimes.size();++i)eventSamples[i]=int64_t(std::llround(eventTimes[i]*rate));
        auto feedTo=[&](int64_t end) {
            while(sample<end){const int count=int(std::min<int64_t>(257,end-sample));juce::AudioBuffer<float> block(4,count);juce::MidiBuffer midi;
                for(int i=0;i<count;++i){const int64_t at=sample+i;float side=0;
                    if(trigger==1)for(int64_t event:eventSamples){const int64_t age=at-event;if(age>=0&&age<3840){const double t=age/rate;
                        side+=float(.9*std::exp(-t/.008)*std::sin(2*juce::MathConstants<double>::pi*55*t));}}
                    block.setSample(0,i,input);block.setSample(1,i,input);block.setSample(2,i,side);block.setSample(3,i,side);}
                if(trigger==2)for(int64_t event:eventSamples)if(event>=sample&&event<sample+count)
                    midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),int(event-sample));
                clock.sample=sample;p.processBlock(block,midi);
                for(int i=0;i<count;++i)rendered.push_back(block.getSample(0,i));sample+=count;}
        };
        feedTo(int64_t(std::ceil(period*rate))+latency-1);
        auto examine=[&](const OpenDuckProcessor::ScopeFrame& frame,bool afterLastHit) {
            telemetry &= frame.complete&&frame.firstBin==0&&frame.bins==512&&frame.hasEnvelopeTimeline;
            timeAxis &= std::abs(frame.envelopeCycleSeconds-cycle)<1.e-9;
            const double epoch=frame.startPpq*60/tempo;double lastTime=-1,holdMin=10,holdMax=-10;
            std::vector<double> triggerOrigins;
            for(int bin=frame.firstBin;bin<frame.bins;++bin){const size_t k=size_t(bin);
                telemetry &= frame.envelopeValid[k]!=0;
                if(!frame.envelopeValid[k])continue;
                const double t=frame.envelopeTimeSeconds[k],absolute=epoch+t;
                const int64_t source=int64_t(std::llround(absolute*rate)),outputSample=source+latency;
                timeAxis &= std::isfinite(t)&&t>lastTime&&t>=0&&t<=frame.periodSeconds+1.e-6;lastTime=t;
                if(outputSample<0||outputSample>=int64_t(rendered.size())){telemetry=false;continue;}
                const double actual=rendered[size_t(outputSample)]/input;
                const double error=std::abs(actual-frame.envelopeGain[k]);maximumGainError=std::max(maximumGainError,error);
                telemetry &= std::isfinite(frame.envelopeGain[k])&&std::isfinite(frame.envelopePhase[k])&&error<2.e-6;++comparedSamples;
                if(absolute<.080){idle &= frame.envelopeActive[k]==0&&std::abs(actual-1)<1.e-7;}
                if(frame.envelopeActive[k]) {
                    const double origin=absolute-double(frame.envelopePhase[k])*cycle;
                    if(triggerOrigins.empty()||std::abs(origin-triggerOrigins.back())>.00002)triggerOrigins.push_back(origin);
                    if(absolute>eventTimes.back()+cycle*1.15){holdMin=std::min(holdMin,actual);holdMax=std::max(holdMax,actual);}
                }
            }
            if(!afterLastHit){origins &= triggerOrigins.size()==eventTimes.size();
                for(size_t i=0;i<std::min(triggerOrigins.size(),eventTimes.size());++i){const double error=triggerOrigins[i]-eventTimes[i];
                    maximumOriginError=std::max(maximumOriginError,std::abs(error));
                    origins &= trigger==2?std::abs(error)<.00001:error>=-.00003&&error<.020;}}
            else held &= triggerOrigins.size()==1&&frame.envelopePhase[511]>2&&holdMin<.9&&holdMax-holdMin<1.e-6;
            std::cout<<"MEASURE input-timeline BPM="<<tempo<<" trigger="<<trigger<<" anti="<<anti<<" held="<<afterLastHit
                <<" complete="<<frame.complete<<" valid-bins="<<frame.bins<<" origins="<<triggerOrigins.size()<<" hold-range="<<holdMin<<".."<<holdMax;
            for(size_t i=0;i<std::min<size_t>(6,triggerOrigins.size());++i)std::cout<<" origin="<<triggerOrigins[i];std::cout<<std::endl;
        };
        auto snapshot=[&](bool afterLastHit) {
            for(int width:{1728,864})for(int mode:{1,2}) {
                const auto frame=mode==1?p.getScopeFrame(0):p.getInputScopeFrame(true);
                const double epoch=mode==1?double(frame.triggerSample)/rate:frame.startPpq*60/tempo;
                if(mode==1){triggerTelemetry &= frame.hasEnvelopeTimeline&&frame.envelopeTriggerCapture&&frame.envelopeValid[511]!=0
                        &&frame.bins<512&&frame.mainMin[511]==0&&frame.mainMax[511]==0&&frame.sideMin[511]==0&&frame.sideMax[511]==0;
                    for(size_t k=0;k<512;++k)if(frame.envelopeValid[k]) {
                        const double t=frame.envelopeTimeSeconds[k];const int64_t n=int64_t(std::llround((epoch+t)*rate))+latency;
                        if(n<0||n>=int64_t(rendered.size())){triggerTelemetry=false;continue;}
                        const double error=std::abs(rendered[size_t(n)]/input-frame.envelopeGain[k]);maximumGainError=std::max(maximumGainError,error);
                        triggerTelemetry &= error<2.e-6&&std::abs(t-double(frame.envelopePhase[k])*cycle)<1.e-6;++triggerSamples;
                    }}
                p.state.state.setProperty("templateCurveView4",mode,nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());editor->setSize(width,width*912/1728);editor->setVisible(true);
                auto* graph=named(*editor,"duckGraph4");if(!graph)throw std::runtime_error("Missing native telemetry graph");
                const auto picture=editor->createComponentSnapshot(editor->getLocalBounds(),true);const auto& props=graph->getProperties();
                const double begin=double(props.getWithDefault("viewStartSeconds4",-1000.)),duration=double(props.getWithDefault("viewDurationSeconds4",-1.));
                const double scale=width/1728.;int points=0,hits=0;
                for(int bin=3;bin<509;bin+=7){const size_t k=size_t(bin);if(!frame.envelopeValid[k])continue;
                    const double t=frame.envelopeTimeSeconds[k],display=(t-begin)/duration;if(display<.015||display>.985)continue;
                    const int64_t n=int64_t(std::llround((epoch+t)*rate))+latency;
                    if(n<0||n>=int64_t(rendered.size()))continue;const double actual=rendered[size_t(n)]/input;
                    const int x=juce::roundToInt((580+1069*display)*scale),y=juce::roundToInt((508-259*actual)*scale),radius=int(std::ceil(4*scale));bool found=false;
                    for(int yy=std::max(0,y-radius);yy<=std::min(picture.getHeight()-1,y+radius);++yy)
                        for(int xx=std::max(0,x-radius);xx<=std::min(picture.getWidth()-1,x+radius);++xx){const auto c=picture.getPixelAt(xx,yy);
                            found |= c.getRed()<80&&c.getGreen()>140&&c.getBlue()>160&&c.getBlue()>c.getRed()+90;}
                    ++points;hits+=found?1:0;
                }
                comparedPixels+=points;
                const bool timeline=bool(props.getWithDefault("envelopeTimelineUsed4",false));
                const int timelinePoints=int(props.getWithDefault("envelopeTimelinePointCount4",0));
                if(mode==2)nativeLoop &= timeline&&timelinePoints>=500&&!bool(props.getWithDefault("envelopeTriggerCapture4",true))
                    &&std::abs(begin)<1.e-8&&std::abs(duration-period)<1.e-7&&points>=65&&hits>=points*.95
                    &&(!afterLastHit||double(props.getWithDefault("envelopeMaxRawPhase4",0.f))>2);
                else {nativeSingle &= timeline&&timelinePoints>=100&&bool(props.getWithDefault("envelopeTriggerCapture4",false))
                    &&int64_t(props.getWithDefault("envelopeTriggerSample4",juce::var(juce::int64(-1))))==int64_t(frame.triggerSample)
                    &&std::abs(begin)<1.e-8&&std::abs(duration-cycle)<1.e-7&&points>=65&&hits>=points*.95;
                    const double origin=double(frame.triggerSample)/rate,error=origin-eventTimes.back();
                    nativeSingle &= trigger==2?std::abs(error)<.00001:error>=-.00003&&error<.020;}
                std::cout<<"MEASURE actual-trigger-view BPM="<<tempo<<" trigger="<<trigger<<" anti="<<anti<<" held="<<afterLastHit<<" width="<<width
                    <<" mode="<<mode<<" begin="<<begin<<" seconds="<<duration<<" PCM-pixel-hits="<<hits<<"/"<<points<<std::endl;
                if(tempo==138&&anti==1&&width==1728)save(picture,output.getChildFile("actual-"+juce::String(trigger==1?"AUDIO":"MIDI")
                    +juce::String(afterLastHit?"-held-":"-irregular-")+juce::String(mode==1?"single":"loop")+".png"));
            }
        };
        snapshot(false);feedTo(int64_t(std::ceil(period*rate))+latency+514);examine(p.getInputScopeFrame(),false);
        feedTo(int64_t(std::ceil(period*rate*2))+latency-1);snapshot(true);
        feedTo(int64_t(std::ceil(period*rate*2))+latency+514);const auto second=p.getInputScopeFrame();examine(second,true);
        std::cout<<"MEASURE actual-trigger-telemetry BPM="<<tempo<<" trigger="<<trigger<<" anti="<<anti<<" PDC="<<latency
            <<" last-phase="<<second.envelopePhase[511]<<" max-gain-vs-PCM="<<maximumGainError<<std::endl;
    }
    check(telemetry&&timeAxis&&comparedSamples>=8000,"AUDIO and MIDI per-bin envelope telemetry matches independently rendered MAIN PCM at each last-sample timestamp and declared PDC");
    check(triggerTelemetry&&triggerSamples>=8000,"trigger-relative SINGLE telemetry including its real held-END point matches actual AUDIO and MIDI output without extending waveform bins");
    check(origins&&idle,"three irregular actual SC or MIDI triggers retain their real origins while silent SC remains dry and inactive before the first event");
    check(held,"ONE SHOT keeps its measured END gain and unwrapped age across a silent second bar without any periodic restart");
    check(nativeLoop&&comparedPixels>=1000,"native AUDIO and MIDI LOOP blue pixels follow the actual irregular PCM envelope and held tail at120/138BPM and both UI sizes");
    check(nativeSingle,"native triggered SINGLE anchors to the selected real trigger capture and paints its physical cycle without synthetic periodic phase");
    std::cout<<"MEASURE triggered-timeline sample-comparisons="<<comparedSamples<<" trigger-sample-comparisons="<<triggerSamples<<" native-pixel-samples="<<comparedPixels
        <<" maximum-gain-error="<<maximumGainError<<" maximum-origin-error-seconds="<<maximumOriginError<<std::endl;
}
}

int main(int argc,char** argv) {
    std::cout<<std::unitbuf<<std::setprecision(9);juce::ScopedJuceInitialiser_GUI gui;
    const auto root=argc>1?juce::File(juce::String::fromUTF8(argv[1])):juce::File::getCurrentWorkingDirectory();
    const auto output=argc>2?juce::File(juce::String::fromUTF8(argv[2])):root.getChildFile("work/revision041/CurveTools");output.createDirectory();
    try{geometry();automaticViews();mixAndUndo();fluidKnobGestures();controlsAndState(root);audio();measuredProfiles(root);measuredBandAndAnticlick(root);measuredLatency();measuredOneShot();measuredSlope(root);measuredLiveEdits();clickSafetyAndSidechain16();previewReplacement(output);graphics(root,output);mouseBehavior(output);miniSettings(output);conceptDemos(root,output);viewConcepts(root,output);triggeredTimelineViews(output);}catch(const std::exception& error){check(false,std::string("exception: ")+error.what());}
    auto* report=new juce::DynamicObject;report->setProperty("scope","actual processor DSP plus native concept UI; frozen native baseline compiled under an isolated class name; no DAW or user media");report->setProperty("passed",passed);report->setProperty("failed",failed);report->setProperty("checks",checks);
    output.getChildFile("CurveTools-results.json").replaceWithText(juce::JSON::toString(juce::var(report),true));
    std::cout<<"RESULT Revision041-CurveTools "<<passed<<" passed, "<<failed<<" failed"<<std::endl;return failed?1:0;
}
