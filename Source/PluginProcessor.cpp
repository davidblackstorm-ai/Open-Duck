#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SingleDuckEditor.h"
#include "TemplateGeometry.h"
#include "MeasuredCurveBank4.h"
#include "MeasuredOneShot4.h"

namespace {
bool templateEditOnMessageThread4() {
    if(auto* manager=juce::MessageManager::getInstanceWithoutCreating())
        return manager->isThisTheMessageThread();
    return false;
}
float measuredTemplateGain4(const odx::CurveSnapshot& s,double cycle) {
    const double x=odx::template4::curveSourcePrecise(cycle,s.position4,s.duration4,s.length,
        s.shape,s.moveCurve4,s.slope4,s.profileModel4);
    const float env=odx::measured4::sample(s.shape,x,s.tension,s.mirrorH4,s.mirrorV4,s.slope4);
    const float floor=OpenDuckProcessor::depthGain(0,s.amount);
    const float base=floor==0?env:floor+(1-floor)*env;
    const float mix=odx::clamp(s.duckMix4/100);
    return mix>=1?base:mix<=0?1.f:1+(base-1)*mix;
}
float measuredTemplateOneShotGain4(const odx::CurveSnapshot& s,double physicalCycle,
        double phaseStep,double endPhase,float swingPercent) {
    const auto warp=[swingPercent](double cycle) {
        if(swingPercent==0)return cycle;
        const double split=.5+double(swingPercent)*.005;
        return cycle<split?.5*cycle/split:.5+.5*(cycle-split)/(1-split);
    };
    const auto base=[&](double cycle){return measuredTemplateGain4(s,warp(cycle));};
    const auto endpoint=[&](double end) {
        const double x=odx::template4::curveSourcePrecise(warp(end),s.position4,s.duration4,s.length,
            s.shape,s.moveCurve4,s.slope4,s.profileModel4);
        const float env=odx::measured4::sampleEndpoint(s.shape,x,s.tension,s.mirrorH4,s.mirrorV4,s.slope4);
        const float floor=OpenDuckProcessor::depthGain(0,s.amount);
        const float raw=floor==0?env:floor+(1-floor)*env;
        const float mix=odx::clamp(s.duckMix4/100);
        return mix>=1?raw:mix<=0?1.f:1+(raw-1)*mix;
    };
    return odx::measured4::applyOneShotTail(physicalCycle,phaseStep,endPhase,base,endpoint);
}
// All revision-3 IDs are appended; these indices are internal, never host parameter indices.
enum R3 { speedMode3, speed3, hz3, snap3, snapEnabled3, smooth3, trigger3, loop3, end3,
    endHold3, loopStart3, loopEnd3, midiChannel3, noteLow3, noteHigh3, velocityLow3,
    velocityHigh3, velocityDepth3, locked3, detectorLow3, detectorHigh3, threshold3,
    hysteresis3, holdoff3, detectorAttack3, detectorRelease3, listen3, lookahead3, offset3,
    bands3, linked3, slope3, phaseMode3, crossLow3, crossHigh3, lowAmount3, midAmount3,
    highAmount3, lowSolo3, midSolo3, highSolo3, group3, riseSlope3, output3, swing3,
    character3, runStopped3, r3Count };
struct Spec3 { const char* base; float low, high, initial; const char* choices = nullptr; };
const std::array<Spec3, r3Count> specs3{{
    {"speedMode",0,1,0,"Sync|Hz"},
    {"speed",0,19,2,"1/1|1/2|1/4|1/8|1/16|1/32|1/64|2 bars|4 bars|1/8 D|1/16 D|1/32 D|1/8 T|1/16 T|1/32 T|1/4 T|3 beats|6 beats|5 beats|7 beats"},
    {"hz",.05f,40,2},{"snap",0,6,3,"1|2|4|8|16|32|64"}, {"snapEnabled",0,1,1},
    {"smooth",0,30,1},{"trigger",0,3,0,"Repeat|Sidechain|MIDI|Input"},
    {"loop",0,2,0,"One shot|Loop|Sustain"},{"end",.01f,1,1},{"endHold",0,1,0},
    {"loopStart",0,.95f,0},{"loopEnd",.05f,1,1},{"midiChannel",0,16,0},
    {"noteLow",0,127,0},{"noteHigh",0,127,127},{"velocityLow",1,127,1},
    {"velocityHigh",1,127,127},{"velocityDepth",0,1,0},{"locked",0,1,0},
    {"detectorLow",20,20000,20},{"detectorHigh",20,20000,20000},
    {"threshold",-60,0,-24},{"hysteresis",1,18,3},{"holdoff",0,500,30},
    {"detectorAttack",.1f,20,1},{"detectorRelease",1,500,20},{"listen",0,1,0},
    {"lookahead",0,15,0},{"offset",-200,200,0},
    {"bands",0,2,0,"Full range|2 bands|3 bands"},{"linked",0,1,1},
    {"slope",0,2,1,"12 dB/oct|24 dB/oct|48 dB/oct"},
    {"phaseMode",0,1,1,"Minimum phase|Linear phase"},
    {"crossLow",20,20000,160},{"crossHigh",20,20000,2500},
    {"lowAmount",0,100,100},{"midAmount",0,100,100},{"highAmount",0,100,100},
    {"lowSolo",0,1,0},{"midSolo",0,1,0},{"highSolo",0,1,0},
    {"group",0,4,0,"Off|A|B|C|D"},{"riseSlope",.25f,4,1},
    {"output",-24,12,0},{"swing",0,75,0},{"character",0,1,.5f},{"runStopped",0,1,0}
}};
const double speedBeats3[] = {4,2,1,.5,.25,.125,.0625,8,16,.75,.375,.1875,1./3,1./6,1./12,2./3,3,6,5,7};
// Public one-shot endpoint settings measured from the installed reference.
// These are explicit fresh/preset defaults, never an epsilon applied to saved
// legacy state. SYNC continues over the entire periodic curve.
constexpr std::array<float,16> templateOneShotEndPhase4{{
    .984375f,.984375f,.984375f,.984375f,.984375f,.984375f,.984375f,.984375f,
    .895f,.79f,.984375f,.984375f,1.f,1.f,1.f,.984375f}};
juce::SpinLock groupLock3;
std::array<std::array<odx::CurveSnapshot,6>,4> groupShapes3;
std::array<std::array<bool,6>,4> groupValid3{};
void copyShape3(odx::CurveSnapshot& to,const odx::CurveSnapshot& from) {
    to.points=from.points;to.count=from.count;to.custom=from.custom;to.shape=from.shape;
    to.linear=from.linear;
}
float wrap3(float x) { x-=std::floor(x);return x; }
juce::ValueTree shapeTree3(const odx::CurveSnapshot&s,const char*type){
    juce::ValueTree tree(type);
    tree.setProperty("shape",s.shape,nullptr);tree.setProperty("custom",s.custom,nullptr);tree.setProperty("linear",s.linear,nullptr);
    tree.setProperty("phase",s.phase,nullptr);tree.setProperty("length",s.length,nullptr);tree.setProperty("tension",s.tension,nullptr);
    tree.setProperty("amount",s.amount,nullptr);tree.setProperty("attack",s.attack,nullptr);tree.setProperty("release",s.release,nullptr);
    tree.setProperty("riseSlope",s.riseSlope,nullptr);
    tree.setProperty("edit",s.edit,nullptr);tree.setProperty("freeDraw",s.freeDraw,nullptr);tree.setProperty("count",s.count,nullptr);
    for(int i=0;i<s.count;++i){const auto&p=s.points[(size_t)i];juce::ValueTree point("Point");
        point.setProperty("x",p.x,nullptr);point.setProperty("y",p.y,nullptr);point.setProperty("segment",p.segment,nullptr);point.setProperty("curvature",p.curvature,nullptr);point.setProperty("reversed",p.reversed,nullptr);tree.addChild(point,-1,nullptr);}
    return tree;
}
bool readShapeTree3(const juce::ValueTree&tree,odx::CurveSnapshot&s){
    s.shape=(int)tree.getProperty("shape",0);s.custom=(bool)tree.getProperty("custom",false);s.linear=(bool)tree.getProperty("linear",false);
    s.edit=(bool)tree.getProperty("edit",false);s.freeDraw=(bool)tree.getProperty("freeDraw",false);s.count=(int)tree.getProperty("count",tree.getNumChildren());
    auto read=[&](const char*name,float initial,float low,float high,float&out){out=(float)tree.getProperty(name,initial);return std::isfinite(out)&&out>=low&&out<=high;};
    if(s.shape<0||s.shape>16||s.count<0||s.count>256||s.count!=tree.getNumChildren()||(s.custom&&s.count<2)||
        !read("phase",0,-50,50,s.phase)||!read("length",1,.05f,1,s.length)||!read("tension",0,-100,100,s.tension)||
        !read("amount",100,0,100,s.amount)||!read("attack",10,.1f,100,s.attack)||!read("release",150,5,2000,s.release)||!read("riseSlope",1,.25f,4,s.riseSlope))return false;
    for(int i=0;i<s.count;++i){const auto point=tree.getChild(i);auto&p=s.points[(size_t)i];p.segment=(int)point.getProperty("segment",0);
        if(!point.hasType("Point")||p.segment<0||p.segment>5)return false;
        p.x=(float)point.getProperty("x",-1);p.y=(float)point.getProperty("y",-1);p.curvature=(float)point.getProperty("curvature",0);
        p.reversed=(bool)point.getProperty("reversed",false);
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.curvature)||p.x<0||p.x>1||p.y<0||p.y>1||std::abs(p.curvature)>1||(i&&p.x<=s.points[(size_t)(i-1)].x))return false;
    }s.modern3=true;return true;
}
}

juce::String OpenDuckProcessor::sectorParameterId(const char* base,int mode) {
    const juce::String s(base);return "v3"+s.substring(0,1).toUpperCase()+s.substring(1)+juce::String(juce::jlimit(0,5,mode));
}
bool OpenDuckProcessor::usesRevision3(int mode) const {
    return (revision3Mask.load(std::memory_order_relaxed)&(1u<<(unsigned)resolveMode(mode)))!=0;
}
void OpenDuckProcessor::upgradeToRevision3(){
    undo.beginNewTransaction("Upgrade audio engine to 0.3 serial routing");
    revision3Mask.store(63);modernMask.store(63);
    {const juce::SpinLock::ScopedLockType lock(curveLock);for(auto&c:curves)c.modern3=true;}
    state.state.setProperty("audioRouting","serial-0.3",nullptr);
    publishRouting4();
}
void OpenDuckProcessor::setSectorParameter(int mode,const char* base,float v) {
    auto* p=state.getParameter(sectorParameterId(base,resolveMode(mode)));
    if(p&&std::isfinite(v)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}
}
float OpenDuckProcessor::depthGain(float envelope,float amount) {
    if(!std::isfinite(envelope)||!std::isfinite(amount))return 1;
    const float depth=odx::clamp(amount/100);
    const float floor=depth==1?0:std::pow(10.0f,-3*depth);
    // Amount sets the closed floor. The drawn envelope remains an amplitude
    // envelope, so a nearly recovered shape does not retain a large dB cut.
    return floor+(1-floor)*odx::clamp(envelope);
}
float OpenDuckProcessor::gainToEnvelope(float gain,float amount) {
    if(!std::isfinite(gain)||!std::isfinite(amount)||amount<=.0001f)return 1;
    const float floor=depthGain(0,amount);
    return odx::clamp((gain-floor)/juce::jmax(1e-7f,1-floor));
}
double OpenDuckProcessor::cycleSeconds(int mode) const {
    mode=resolveMode(mode);
    if(!usesRevision3(mode)){const double old[]={1,.5,.25,1.5,.75,1./3,1./6};return old[juce::jlimit(0,6,getCurveSnapshot(mode).grid)]*60/juce::jmax(20.f,bpm.load());}
    const auto& p=refs3[(size_t)mode];
    return p[speedMode3]->load()>.5f?1/juce::jmax(.05f,p[hz3]->load()):
        speedBeats3[juce::jlimit(0,19,(int)p[speed3]->load())]*60/juce::jmax(20.f,bpm.load());
}
float OpenDuckProcessor::curveTimeToRaw(const CurveSnapshot&s,float cycle,double seconds) {
    if (s.template4) return odx::template4::curveSource(cycle,s.position4,s.duration4,s.length,s.shape,s.moveCurve4,s.slope4,s.profileModel4);
    if(!std::isfinite(cycle)||!std::isfinite(seconds)||seconds<=0)return 0;
    const float local=cycle==1&&s.phase==0?1:wrap3(cycle-s.phase/100);
    float a=juce::jmax(.0005f,s.attack/float(seconds*1000)),r=juce::jmax(.0005f,s.release/float(seconds*1000));
    if(a+r>.98f){const float scale=.98f/(a+r);a*=scale;r*=scale;}
    const float raw=local<a?.14f*local/a:local<a+r?.14f+.43f*(local-a)/r:.57f+.43f*(local-a-r)/(1-a-r);
    return std::pow(odx::clamp(raw),juce::jmax(.0025f,s.length)/juce::jmax(.0625f,s.riseSlope));
}
float OpenDuckProcessor::rawCurveTimeToCycle(const CurveSnapshot&s,float x,double seconds) {
    if (s.template4) return odx::template4::curveDisplay(x,s.position4,s.duration4,s.length,s.shape,s.moveCurve4,s.slope4,s.profileModel4);
    if(!std::isfinite(x)||!std::isfinite(seconds)||seconds<=0)return 0;
    float a=juce::jmax(.0005f,s.attack/float(seconds*1000)),r=juce::jmax(.0005f,s.release/float(seconds*1000));
    if(a+r>.98f){const float scale=.98f/(a+r);a*=scale;r*=scale;}
    x=std::pow(odx::clamp(x),juce::jmax(.0625f,s.riseSlope)/juce::jmax(.0025f,s.length));
    const float at=x<.14f?a*x/.14f:x<.57f?a+r*(x-.14f)/.43f:a+r+(1-a-r)*(x-.57f)/.43f;
    return at==1&&s.phase==0?1:wrap3(at+s.phase/100);
}
float OpenDuckProcessor::effectiveCurveSample(const CurveSnapshot&s,float cycle,double seconds,int mode,float character) {
    if(s.template4&&s.profileModel4==1&&!s.custom)return measuredTemplateGain4(s,double(cycle));
    if (s.template4) { auto raw=s; raw.phase=0; raw.length=1; raw.riseSlope=1;
        const float x=curveTimeToRaw(s,cycle,seconds);
        raw.template4=false;
        const float env=s.custom?sampleCurve(raw,x):std::pow(odx::template4::factoryShape(x,s.shape),std::exp2(-s.tension/70));
        const float baseGain=depthGain(env,s.amount);
        const float mix=odx::clamp(s.duckMix4/100);
        return mix>=1?baseGain:mix<=0?1.f:1+(baseGain-1)*mix; }
    if(!s.modern3)return 1-s.amount/100*(1-sampleCurve(s,cycle));
    auto raw=s;raw.phase=0;raw.amount=100;raw.riseSlope=1;raw.length=1;
    const float x=curveTimeToRaw(s,cycle,seconds);
    float env=sampleCurve(raw,x);
    if(mode==3){auto soft=raw;soft.custom=false;soft.shape=0;
        // Morph the recovery curvature while preserving explicit closure/unity endpoints.
        // A linear blend with Soft Duck would reopen an intentionally flat zero plateau.
        env=std::pow(env,std::exp2((.5f-sampleCurve(soft,x))*character*2));}
    else env=std::pow(env,std::pow(2.f,(character-.5f)*1.5f));
    return depthGain(env,s.amount);
}
float OpenDuckProcessor::previewCurveGain(int mode,float cycle,int band) const{
    mode=resolveMode(mode);auto s=getEffectiveCurveSnapshot(mode,band);
    if(!s.modern3)return 1-s.amount/100*(1-sampleCurve(s,cycle));
    const auto&p=refs3[(size_t)mode];const double seconds=cycleSeconds(mode);
    const float shiftedValue=cycle+(p[offset3]->load()+p[lookahead3]->load())*.001f/float(seconds);
    const bool measuredOneShot=s.template4&&s.profileModel4==1&&p[trigger3]->load()>.5f&&int(p[loop3]->load())==0;
    if(measuredOneShot&&shiftedValue<0)return 1;
    const float shifted=measuredOneShot?juce::jmin(shiftedValue,p[end3]->load()):
        shiftedValue==1?1:wrap3(shiftedValue);
    const float swing=p[swing3]->load()/100,split=.5f+swing*.5f;
    const float at=shifted<split?.5f*shifted/split:.5f+.5f*(shifted-split)/(1-split);
    if(band>=0&&band<3){s.amount*=p[(size_t)(lowAmount3+band)]->load()/100;
        if(mode==2){const char*ids[]={"bandLow","bandMid","bandHigh"};s.amount*=value(ids[band]);}
        s.amount=juce::jlimit(0.f,100.f,s.amount);}
    float gain=measuredOneShot&&!s.custom?
        measuredTemplateOneShotGain4(s,shifted,1/(rate*seconds),p[end3]->load(),p[swing3]->load()):
        effectiveCurveSample(s,at,seconds,mode,mode==3?(p[character3]->load()+value("morph"))*.5f:p[character3]->load());
    if(mode==1)gain=1-(1-gain)*value("spectralDepth");
    if(mode==4)gain=1-(1-gain)*value("grooveDepth");
    return gain;
}
OpenDuckProcessor::CurveSnapshot OpenDuckProcessor::getEffectiveCurveSnapshot(int mode,int band)const{
    mode=resolveMode(mode);auto master=getCurveSnapshot(mode);if(band<0||refs3[(size_t)mode][linked3]->load()>.5f)return master;
    auto s=getBandCurveSnapshot(mode,band);s.phase=(wrap3((master.phase+s.phase)/100+.5f)-.5f)*100;
    s.attack*=master.attack/10;s.release*=master.release/150;s.amount*=master.amount/100;
    s.tension=juce::jlimit(-100.f,100.f,s.tension+master.tension);s.length*=master.length;s.riseSlope*=master.riseSlope;
    return s;
}
float OpenDuckProcessor::previewCycleToRaw(int mode,float cycle,int band)const{
    mode=resolveMode(mode);const auto&p=refs3[(size_t)mode];const auto s=getEffectiveCurveSnapshot(mode,band);
    const float shiftedValue=cycle+(p[offset3]->load()+p[lookahead3]->load())*.001f/float(cycleSeconds(mode));
    const float shifted=shiftedValue==1?1:wrap3(shiftedValue);
    const float split=.5f+p[swing3]->load()*.005f;
    return curveTimeToRaw(s,shifted<split?.5f*shifted/split:.5f+.5f*(shifted-split)/(1-split),cycleSeconds(mode));
}
float OpenDuckProcessor::rawToPreviewCycle(int mode,float raw,int band)const{
    mode=resolveMode(mode);const auto&p=refs3[(size_t)mode];const float split=.5f+p[swing3]->load()*.005f;
    const float at=rawCurveTimeToCycle(getEffectiveCurveSnapshot(mode,band),raw,cycleSeconds(mode));
    const float unswung=at<.5f?at*2*split:split+(at-.5f)*2*(1-split);
    const float unshifted=unswung-(p[offset3]->load()+p[lookahead3]->load())*.001f/float(cycleSeconds(mode));
    return value("template4")>.5f&&mode==0?unshifted:unshifted==1?1:wrap3(unshifted);
}
float OpenDuckProcessor::previewGainToCurveEnvelope(int mode,float cycle,float gain,int band)const{
    mode=resolveMode(mode);auto s=getEffectiveCurveSnapshot(mode,band);const auto&p=refs3[(size_t)mode];
    if(s.template4){const float mix=odx::clamp(s.duckMix4/100);if(mix<1)gain=mix>1.e-7f?1+(gain-1)/mix:1;}
    float scalar=mode==1?value("spectralDepth"):mode==4?value("grooveDepth"):1;
    gain=scalar>0?odx::clamp(1-(1-gain)/scalar):1;
    float amount=s.amount;if(band>=0){amount*=p[(size_t)(lowAmount3+juce::jlimit(0,2,band))]->load()/100;
        if(mode==2){const char*keys[]={"bandLow","bandMid","bandHigh"};amount*=value(keys[juce::jlimit(0,2,band)]);}}
    const float character=mode==3?(p[character3]->load()+value("morph"))*.5f:p[character3]->load();
    float exponent=std::pow(2.f,(character-.5f)*1.5f);
        if(mode==3){auto soft=s;soft.custom=false;soft.shape=0;soft.phase=0;soft.riseSlope=1;soft.length=1;
        exponent=std::exp2((.5f-sampleCurve(soft,previewCycleToRaw(mode,cycle,band)))*character*2);}
    return std::pow(gainToEnvelope(gain,juce::jlimit(0.f,100.f,amount)),1/exponent);
}
const std::array<OpenDuckProcessor::BehaviorPreset,700>& OpenDuckProcessor::sectorPresets(){
    static const auto library=[](){std::array<BehaviorPreset,700> result{};
        constexpr int counts[]={120,120,120,120,100,120};
        const char* names[]={"Ducking","Spectral","Multiband","Morph","Groove","Transient"};
        int id=0;for(int mode=0;mode<6;++mode)for(int n=0;n<counts[mode];++n,++id){
            auto&p=result[(size_t)id];p.id=id;p.mode=mode;p.category=n%7;p.shape=(n/7+n%7*3)%16;
            p.grid=(n/16)%7;p.phase=float((n/7)%5-2)*3;p.length=.6f+.1f*(n%5);
            p.amount=65+float(n%8)*5;p.attack=1+float((n/5)%10)*2;p.release=60+float((n/3)%16)*15;
            p.tension=float((n/11)%7-3)*20;p.mix=100;p.character=.15f+.07f*(n%11);
            p.low=.55f+.05f*(n%10);p.mid=.45f+.05f*((n+3)%11);p.high=.35f+.05f*((n+7)%13);
            p.name=juce::String(names[mode])+" / "+odx::categoryNames()[(size_t)p.category]+" / "+
                odx::shapeNames()[(size_t)p.shape]+" "+juce::String(n+1).paddedLeft('0',3);
            if(n==0){p.shape=0;p.phase=0;p.length=1;p.tension=0;p.amount=100;p.attack=10;p.release=150;p.grid=0;
                p.name=juce::String(names[mode])+" / INIT Soft Duck";}
        }return result;}();return library;
}
std::vector<int> OpenDuckProcessor::sectorPresetIds(int mode,int category){
    std::vector<int> ids;for(const auto&p:sectorPresets())if(p.mode==juce::jlimit(0,5,mode)&&(category<0||p.category==category))ids.push_back(p.id);return ids;
}
void OpenDuckProcessor::applySectorPreset(int id,int mode){
    mode=resolveMode(mode);if(id<0||id>=700)return;const auto&p=sectorPresets()[(size_t)id];if(p.mode!=mode)return;
    auto s=getCurveSnapshot(mode);s.presetId=id;s.shape=p.shape;s.custom=false;s.count=0;s.edit=s.freeDraw=s.linear=false;
    s.amount=p.amount;s.attack=p.attack;s.release=p.release;s.phase=p.phase;s.length=p.length;s.tension=p.tension;s.grid=p.grid;s.modern3=true;
    setCurveSnapshot(mode,s,"Sector behavior");
    setSectorParameter(mode,"character",p.character);setSectorParameter(mode,"lowAmount",p.low*100);
    setSectorParameter(mode,"midAmount",p.mid*100);setSectorParameter(mode,"highAmount",p.high*100);
    // Browsing never changes global MIX, engine ON/SOLO, trigger or the routing configuration.
    if(value(sectorParameterId("locked",mode))<.5f){constexpr int map[]={2,3,4,9,10,12,13};setSectorParameter(mode,"speed",float(map[p.grid]));}
}
void OpenDuckProcessor::selectFactoryShape(int shape,int mode){
    mode=resolveMode(mode);shape=juce::jlimit(0,15,shape);auto s=getCurveSnapshot(mode);
    const bool measured=s.template4&&s.profileModel4==1;
    const float previousEnd=value(sectorParameterId("end",mode));float nextEnd=previousEnd;
    {const juce::SpinLock::ScopedLockType lock(curveLock);
        if(!s.custom&&s.shape>=0&&s.shape<16)shapeMemories3[(size_t)mode][(size_t)s.shape]={s.phase,s.length,s.tension,s.riseSlope,s.moveCurve4,s.slope4,s.mirrorH4,s.mirrorV4,previousEnd};
        const auto&memory=shapeMemories3[(size_t)mode][(size_t)shape];s.phase=memory.phase;s.length=memory.length;s.tension=memory.tension;s.riseSlope=memory.riseSlope;
        if(s.template4){s.moveCurve4=memory.moveCurve4;s.slope4=memory.slope4;s.mirrorH4=memory.mirrorH4;s.mirrorV4=memory.mirrorV4;}
        if(measured)nextEnd=memory.end4;}
    s.shape=shape;s.custom=false;s.count=0;s.edit=s.freeDraw=false;
    setCurveSnapshot(mode,s,"Select factory shape");
    if(measured)setSectorParameter(mode,"end",nextEnd);
}
OpenDuckProcessor::CurveSnapshot OpenDuckProcessor::getBandCurveSnapshot(int mode,int band) const{
    mode=resolveMode(mode);band=juce::jlimit(0,2,band);
    const juce::SpinLock::ScopedLockType lock(curveLock);auto s=bandCurves[(size_t)mode][(size_t)band];s.modern3=true;return s;
}
class OpenDuckBandCurveAction final:public juce::UndoableAction{
    OpenDuckProcessor&p;int mode,band;odx::CurveSnapshot before,after;
public:OpenDuckBandCurveAction(OpenDuckProcessor&processor,int m,int b,const odx::CurveSnapshot&old,const odx::CurveSnapshot&next):p(processor),mode(m),band(b),before(old),after(next){}
    bool perform()override{p.applyBandCurveSnapshot(mode,band,after);return true;}
    bool undo()override{p.applyBandCurveSnapshot(mode,band,before);return true;}
    int getSizeInUnits()override{return int(sizeof(before)*2);}
};
void OpenDuckProcessor::setBandCurveSnapshot(int mode,int band,const CurveSnapshot&s){
    mode=resolveMode(mode);band=juce::jlimit(0,2,band);
    undo.perform(new OpenDuckBandCurveAction(*this,mode,band,getBandCurveSnapshot(mode,band),sanitiseCurve(s)));
}
void OpenDuckProcessor::applyBandCurveSnapshot(int mode,int band,const CurveSnapshot&s){
    mode=resolveMode(mode);band=juce::jlimit(0,2,band);auto clean=sanitiseCurve(s);clean.modern3=true;
    {const juce::SpinLock::ScopedLockType lock(curveLock);bandCurves[(size_t)mode][(size_t)band]=clean;}
    revision3Mask.fetch_or(1u<<(unsigned)mode);modernMask.fetch_or(1u<<(unsigned)mode);
}
void OpenDuckProcessor::transformCurve(CurveOperation op,int mode,int band){
    mode=resolveMode(mode);auto s=band<0?getCurveSnapshot(mode):getBandCurveSnapshot(mode,band);
    if(band<0&&s.template4&&s.profileModel4==1&&!s.custom&&(op==CurveOperation::mirrorHorizontal||op==CurveOperation::mirrorVertical)){
        if(op==CurveOperation::mirrorHorizontal)s.mirrorH4=!s.mirrorH4;else s.mirrorV4=!s.mirrorV4;
        setCurveSnapshot(mode,s,"Mirror measured curve");return;
    }
    if(op==CurveOperation::resetTransforms){s.phase=0;s.length=1;s.tension=0;s.riseSlope=1;}
    else{
        if(!s.custom||s.count<2){auto sourceCurve=s;sourceCurve.phase=0;sourceCurve.length=1;sourceCurve.riseSlope=1;s.count=193;
            for(int i=0;i<s.count;++i){float x=i/192.f;s.points[(size_t)i]={x,sampleCurve(sourceCurve,x)};}
            s.custom=true;s.shape=16;s.tension=0;}
        const auto original=s;const int divisions=1<<juce::jlimit(0,6,(int)value(sectorParameterId("snap",mode)));
        juce::Random random((juce::int64)juce::Time::getHighResolutionTicks());
        for(int i=0;i<s.count;++i){auto&p=s.points[(size_t)i];const float x=p.x;
            if(op==CurveOperation::mirrorVertical)p.y=1-p.y;
            else if(op==CurveOperation::mirrorHorizontal){const auto&position=original.points[(size_t)(s.count-1-i)];p=position;p.x=1-position.x;
                if(i<s.count-1){const auto&segment=original.points[(size_t)(s.count-2-i)];p.segment=segment.segment;p.curvature=segment.curvature;p.reversed=!segment.reversed;}
                else {const auto&unused=original.points[(size_t)(s.count-1)];p.segment=unused.segment;p.curvature=unused.curvature;p.reversed=unused.reversed;}}
            else if(op==CurveOperation::clear)p.y=1;
            else if(op==CurveOperation::quantise){p.x=std::round(x*divisions)/divisions;p.y=std::round(p.y*16)/16;}
            else if(op==CurveOperation::rotateLeft||op==CurveOperation::rotateRight){auto raw=original;raw.phase=0;p.y=sampleCurve(raw,wrap3(x+(op==CurveOperation::rotateLeft?1.f:-1.f)/divisions));}
            else if(op==CurveOperation::doubleCurve){auto raw=original;raw.phase=0;p.y=sampleCurve(raw,wrap3(x*2));}
            else if(op==CurveOperation::halveCurve){auto raw=original;raw.phase=0;p.y=sampleCurve(raw,x*.5f);}
            else if(op==CurveOperation::ramp)p.y=x;
            else if(op==CurveOperation::step){p.y=std::floor(x*divisions)/divisions;p.segment=5;}
            else if(op==CurveOperation::gate){p.y=((int)(x*divisions)%2)?1.f:0.f;p.segment=5;}
            else if(op==CurveOperation::randomise)p.y=random.nextFloat();
        }s.custom=true;s.shape=16;s.presetId=0;s.modern3=true;
    }
    if(band<0)setCurveSnapshot(mode,s,"Curve operation");else setBandCurveSnapshot(mode,band,s);
}
juce::Result OpenDuckProcessor::saveUserCurve(const juce::File&file,int mode) const{
    auto s=getCurveSnapshot(mode);juce::XmlElement xml("OpenDuckShape");xml.setAttribute("version",1);
    xml.setAttribute("shape",s.shape);xml.setAttribute("custom",s.custom);xml.setAttribute("linear",s.linear);
    for(int i=0;i<s.count;++i){auto*p=xml.createNewChildElement("Point");p->setAttribute("x",s.points[(size_t)i].x);p->setAttribute("y",s.points[(size_t)i].y);
        p->setAttribute("segment",s.points[(size_t)i].segment);p->setAttribute("curvature",s.points[(size_t)i].curvature);p->setAttribute("reversed",s.points[(size_t)i].reversed);}
    juce::TemporaryFile temp(file);return xml.writeTo(temp.getFile())&&temp.overwriteTargetFileWithTemporary()?juce::Result::ok():juce::Result::fail("Cannot save curve");
}
juce::Result OpenDuckProcessor::loadUserCurve(const juce::File&file,int mode){
    auto xml=juce::XmlDocument::parse(file);if(!xml||!xml->hasTagName("OpenDuckShape")||xml->getIntAttribute("version")!=1)return juce::Result::fail("Unsupported curve file");
    auto s=getCurveSnapshot(mode);s.shape=xml->getIntAttribute("shape",0);s.custom=xml->getBoolAttribute("custom");s.linear=xml->getBoolAttribute("linear");s.count=0;
    for(auto*p=xml->getFirstChildElement();p;p=p->getNextElement()){
        if(!p->hasTagName("Point")||s.count>=256)return juce::Result::fail("Invalid points");
        CurvePoint point{(float)p->getDoubleAttribute("x",-1),(float)p->getDoubleAttribute("y",-1),p->getIntAttribute("segment",0),(float)p->getDoubleAttribute("curvature",0)};
        point.reversed=p->getBoolAttribute("reversed",false);
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.curvature)||point.x<0||point.x>1||point.y<0||point.y>1||point.segment<0||point.segment>5||std::abs(point.curvature)>1)return juce::Result::fail("Invalid curve values");
        s.points[(size_t)s.count++]=point;
    }if(s.shape<0||s.shape>16||(s.custom&&s.count<2))return juce::Result::fail("Incomplete curve");
    setCurveSnapshot(mode,s,"Load user curve");return juce::Result::ok();
}
OpenDuckProcessor::ScopeFrame OpenDuckProcessor::getScopeFrame(int mode) const{
    mode=resolveMode(mode);const juce::SpinLock::ScopedLockType lock(scopeLock);auto frame=publishedScopes[(size_t)mode];
    if(!scopeFrozen.load())frame.phase=playheads3[(size_t)mode].load();return frame;
}
void OpenDuckProcessor::setInputScopeBeats(double beats) {
    constexpr double options[]{1,2,4,8,16};
    if(!std::isfinite(beats))beats=4;
    double selected=options[0];
    for(const auto option:options)if(std::abs(option-beats)<std::abs(selected-beats))selected=option;
    inputScopeBeats.store(selected,std::memory_order_relaxed);
    // Editor/state-thread API; the audio callback reads only the atomic value.
    state.state.setProperty("templateWaveBeats",selected,nullptr);
}
OpenDuckProcessor::ScopeFrame OpenDuckProcessor::getInputScopeFrame(bool preferLive) const {
    const juce::SpinLock::ScopedLockType lock(inputScopeLock);
    return preferLive?inputLivePublishedScope:inputPublishedScope;
}
juce::Result OpenDuckProcessor::traceAudio(const juce::AudioBuffer<float>&audio,double sampleRate,int mode,int band){
    if(audio.getNumSamples()<2||audio.getNumChannels()<1||sampleRate<=0)return juce::Result::fail("No audio to trace");
    std::unique_ptr<juce::AudioBuffer<float>> filtered;
    if(band>=0){mode=resolveMode(mode);filtered=std::make_unique<juce::AudioBuffer<float>>(audio.getNumChannels(),audio.getNumSamples());filtered->clear();
        auto split=std::make_unique<BandEngine>();split->prepare(sampleRate);split->setSlope((int)value(sectorParameterId("slope",mode)));
        const float low=juce::jlimit(20.f,float(sampleRate*.4),value(sectorParameterId("crossLow",mode)));
        split->setCutoffs(low,juce::jlimit(low+1,float(sampleRate*.49),value(sectorParameterId("crossHigh",mode))));
        std::array<float,3> gains{};gains[(size_t)juce::jlimit(0,2,band)]=1;
        if((int)value(sectorParameterId("bands",mode))==1&&band==2)gains[1]=1;
        for(int i=0;i<audio.getNumSamples()+BandEngine::delay;++i){for(int c=0;c<juce::jmin(2,audio.getNumChannels());++c){float aligned=0;
            const float value=split->process(c,i<audio.getNumSamples()?audio.getSample(c,i):0,gains,aligned);
            if(i>=BandEngine::delay)filtered->setSample(c,i-BandEngine::delay,value);}split->advance();}
    }
    const auto& source=filtered?*filtered:audio;
    auto s=band<0?getCurveSnapshot(mode):getBandCurveSnapshot(mode,band);s.count=193;s.shape=16;s.custom=true;s.linear=false;s.modern3=true;s.phase=0;
    std::array<float,193> energy{};float maximum=0;
    for(int j=0;j<193;++j){int a=j*audio.getNumSamples()/193,b=juce::jmax(a+1,(j+1)*audio.getNumSamples()/193);double sum=0;int n=0;
        for(int i=a;i<juce::jmin(b,source.getNumSamples());++i)for(int c=0;c<source.getNumChannels();++c){float v=source.getSample(c,i);if(std::isfinite(v)){sum+=v*v;++n;}}
        energy[(size_t)j]=n?float(std::sqrt(sum/n)):0;maximum=juce::jmax(maximum,energy[(size_t)j]);}
    if(maximum<1e-7f)return juce::Result::fail("The trace is silent");
    for(int j=0;j<193;++j)s.points[(size_t)j]={j/192.f,1-std::sqrt(odx::clamp(energy[(size_t)j]/maximum)),0,0};
    if(band<0)setCustomCurve(mode,s,"Trace audio");else setBandCurveSnapshot(mode,band,s);return juce::Result::ok();
}
juce::Result OpenDuckProcessor::traceSidechain(int mode,int band){
    const auto frame=getScopeFrame(mode);if(!frame.sidechain||!frame.complete||frame.bins<2)return juce::Result::fail("Capture a complete sidechain cycle first");
    juce::AudioBuffer<float> audio(1,frame.bins);for(int i=0;i<frame.bins;++i)audio.setSample(0,i,juce::jmax(std::abs(frame.sideMin[(size_t)i]),std::abs(frame.sideMax[(size_t)i])));
    return traceAudio(audio,frame.bins/juce::jmax(.001,frame.periodSeconds),mode,band);
}

juce::String OpenDuckProcessor::curveParameterId(const char *parameter, int mode)
{
    juce::String name(parameter);
    return "curve" + name.substring(0, 1).toUpperCase() + name.substring(1) +
           juce::String(juce::jlimit(0, 5, mode));
}
juce::String OpenDuckProcessor::factoryPresetName(int id)
{
    return factoryPresets()[(size_t)juce::jlimit(0, 699, id)].name;
}
juce::String OpenDuckProcessor::templatePresetName(int id) {
    id=juce::jlimit(0,699,id); if(id==0)return "INIT Soft Duck";
    const char* names[]={"Soft Duck","Linear Rise","Exponential Rise","Early Rise","Three Steps","Gate Down","Arch","Soft Fall","Early Fall","Ramp Hold","Gate Up","Late Fall","Exponential Fall","Late Recovery","Tight Ramp","Curved Rise"};
    const auto& p=factoryPresets()[(size_t)id];
    return juce::String(names[juce::jlimit(0,15,p.shape)])+" / "+juce::String(factoryCategoryNames()[(size_t)(id/100)])+" / "+juce::String(id%100+1).paddedLeft('0',2);
}
void OpenDuckProcessor::applyTemplatePreset(int id) {
    id=juce::jlimit(0,699,id);const auto& p=factoryPresets()[(size_t)id];auto c=getCurveSnapshot(0);
    {const juce::SpinLock::ScopedLockType lock(curveLock);
        if(c.profileModel4==0){shapeMemories3[0]={};for(size_t i=0;i<16;++i)shapeMemories3[0][i].end4=templateOneShotEndPhase4[i];}
        else if(!c.custom&&c.shape>=0&&c.shape<16)shapeMemories3[0][(size_t)c.shape]={c.phase,c.length,c.tension,c.riseSlope,c.moveCurve4,c.slope4,c.mirrorH4,c.mirrorV4,value("v3End0")};}
    c.shape=p.shape;c.custom=false;c.count=0;c.phase=0;c.length=id==0?1.f:.4f+.14f*(id%4)+.02f*(id/100);
    c.tension=p.tension;c.amount=p.amount;c.attack=p.attack;c.release=p.release;c.presetId=id;
    c.moveCurve4=id==0?0.f:p.phase;c.slope4=0;c.duckMix4=id==0?0.f:100.f;
    c.profileModel4=1;c.mirrorH4=c.mirrorV4=false;
    setCurveSnapshot(0,c,"Select template preset");
    setSectorParameter(0,"end",templateOneShotEndPhase4[size_t(c.shape)]);
    for(const auto& item:std::array<std::pair<const char*,float>,2>{{{"position4",50},{"duration4",0}}})
        if(auto* parameter=state.getParameter(item.first)){parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(item.second));parameter->endChangeGesture();}
    if(id==0)if(auto* parameter=state.getParameter("bypass")){parameter->beginChangeGesture();parameter->setValueNotifyingHost(1);parameter->endChangeGesture();}
    state.state.setProperty("templatePresetCatalog4","measured8192-v1",nullptr);
}
void OpenDuckProcessor::shiftTemplateCurve(int direction,bool fine) {
    if(direction==0)return;
    float next=value("moveCurve4")+(direction>0?1.f:-1.f)*(fine?.390625f:6.25f);
    while(next>50)next-=100;while(next< -50)next+=100;
    if(auto* p=state.getParameter("moveCurve4")){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(next));p->endChangeGesture();}
}
void OpenDuckProcessor::resetTemplateCurveEdits() {
    undo.beginNewTransaction("Reset curve move and slope");
    for(const char* id:{"moveCurve4","slope4"})if(auto* p=state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(0));p->endChangeGesture();}
}
OpenDuckProcessor::CurveSnapshot OpenDuckProcessor::getTemplateShapePreview(int shape) const {
    shape=juce::jlimit(0,15,shape);auto s=getCurveSnapshot(0);
    s.duckMix4=100; // Thumbnails describe the full profile at every MIX depth.
    if(!s.custom&&s.shape==shape)return s;
    {const juce::SpinLock::ScopedLockType lock(curveLock);const auto& m=shapeMemories3[0][(size_t)shape];
        s.phase=m.phase;s.length=m.length;s.tension=m.tension;s.riseSlope=m.riseSlope;s.moveCurve4=m.moveCurve4;s.slope4=m.slope4;s.mirrorH4=m.mirrorH4;s.mirrorV4=m.mirrorV4;}
    s.shape=shape;s.custom=false;s.count=0;s.edit=s.freeDraw=false;return s;
}
void OpenDuckProcessor::beginTemplateEdit4(const juce::String& label) {
    // Public editor-thread API. No audio callback calls or allocates history.
    if(!templateEditOnMessageThread4()||restoringTemplateUndo4||loadingState.load(std::memory_order_relaxed))return;
    if(templateEditDepth4++==0){templatePendingLabel4=label.isNotEmpty()?label:"Edit OpenDuck";
        templatePendingEdit4.reset();getStateInformation(templatePendingEdit4);}
}
void OpenDuckProcessor::commitTemplateEdit4() {
    if(!templateEditOnMessageThread4()||restoringTemplateUndo4||templateEditDepth4<=0)return;
    if(--templateEditDepth4!=0)return;
    juce::MemoryBlock after;getStateInformation(after);
    if(templatePendingEdit4!=after){
        if(templateEditHistory4.size()==16)templateEditHistory4.erase(templateEditHistory4.begin());
        templateEditHistory4.push_back({templatePendingLabel4,std::move(templatePendingEdit4)});
    }
    templatePendingEdit4.reset();templatePendingLabel4.clear();
}
bool OpenDuckProcessor::canUndoTemplateEdit4() const {
    return templateEditOnMessageThread4()&&!restoringTemplateUndo4&&templateEditDepth4==0&&!templateEditHistory4.empty();
}
juce::String OpenDuckProcessor::getTemplateUndoLabel4() const {
    return canUndoTemplateEdit4()?templateEditHistory4.back().label:juce::String{};
}
bool OpenDuckProcessor::undoTemplateEdit4() {
    if(!canUndoTemplateEdit4())return false;
    auto checkpoint=std::move(templateEditHistory4.back());templateEditHistory4.pop_back();
    std::vector<std::pair<juce::AudioProcessorParameter*,float>> before;
    before.reserve((size_t)getParameters().size());
    for(auto* parameter:getParameters())before.emplace_back(parameter,parameter->getValue());
    {
        const juce::ScopedValueSetter<bool> guard(restoringTemplateUndo4,true);
        setStateInformation(checkpoint.state.getData(),(int)checkpoint.state.getSize());
        // replaceState restores audio and attachments; notify the host of every
        // changed public parameter so automation/controllers recall the undo too.
        // These notifications are recall, not fresh curve edits: retain the
        // restored legacy/modern masks instead of parameterChanged upgrading them.
        loadingState.store(true,std::memory_order_relaxed);
        for(const auto& entry:before){const float restored=entry.first->getValue();
            if(restored!=entry.second){entry.first->beginChangeGesture();
                entry.first->setValueNotifyingHost(restored);entry.first->endChangeGesture();}}
        loadingState.store(false,std::memory_order_relaxed);
    }
    return true;
}
int OpenDuckProcessor::resolveMode(int mode) const
{
    return juce::jlimit(0, 5, mode < 0 ? (int)value("selected") : mode);
}
bool OpenDuckProcessor::usesModernCurves(int mode) const
{
    return (modernMask.load(std::memory_order_relaxed) & (1u << (unsigned)resolveMode(mode))) != 0;
}
void OpenDuckProcessor::parameterChanged(const juce::String &id, float newValue)
{
    if (!loadingState.load(std::memory_order_relaxed))
    {
        const int mode = (int)id.getLastCharacter() - (int)'0';
        if(id.startsWith("curve"))modernMask.fetch_or(1u << (unsigned)juce::jlimit(0, 5, mode), std::memory_order_relaxed);
        if(usesRevision3(mode)&&id.startsWith("v3Group")&&newValue>=1&&newValue<=4){
            const juce::SpinLock::ScopedTryLockType local(curveLock);
            if(local.isLocked()){const juce::SpinLock::ScopedTryLockType global(groupLock3);
                if(global.isLocked()&&!groupValid3[(size_t)((int)newValue-1)][(size_t)mode]){
                    groupShapes3[(size_t)((int)newValue-1)][(size_t)mode]=curves[(size_t)mode];
                    groupValid3[(size_t)((int)newValue-1)][(size_t)mode]=true;}}}
        if(id=="template4"||id=="profileModel4"||id=="antiClick4"||id=="v3Trigger0")
            routingDirty4.store(true,std::memory_order_release);
    }
}
OpenDuckProcessor::~OpenDuckProcessor()
{
    stopTimer();
    for(const char* id:{"template4","profileModel4","antiClick4"})state.removeParameterListener(id,this);
    const char *bases[] = {"shape",  "phase",   "length", "tension", "amount",
                           "attack", "release", "grid",   "band",    "focus"};
    for (int mode = 0; mode < 6; ++mode)
    {
        for (auto *base : bases)
            state.removeParameterListener(curveParameterId(base, mode), this);
        for(const auto& s:specs3) state.removeParameterListener(sectorParameterId(s.base,mode),this);
    }
}
void OpenDuckProcessor::readCurveParameters(int mode, CurveSnapshot &s) const
{
    const auto &refs = curveRefs[(size_t)mode];
    s.shape = (int)refs[curveShape]->load(std::memory_order_relaxed);
    s.phase = refs[curvePhase]->load(std::memory_order_relaxed);
    s.length = refs[curveLength]->load(std::memory_order_relaxed);
    s.tension = refs[curveTension]->load(std::memory_order_relaxed);
    s.amount = refs[curveAmount]->load(std::memory_order_relaxed);
    s.attack = refs[curveAttack]->load(std::memory_order_relaxed);
    s.release = refs[curveRelease]->load(std::memory_order_relaxed);
    s.grid = (int)refs[curveGrid]->load(std::memory_order_relaxed);
    s.band = refs[curveBand]->load(std::memory_order_relaxed) > .5f;
    s.focus = refs[curveFocus]->load(std::memory_order_relaxed);
    s.custom = s.shape == 16;
    s.legacy = false;
    s.modern3 = usesRevision3(mode);
    s.template4 = mode == 0 && value("template4") > .5f;
    s.position4 = value("position4"); s.duration4 = value("duration4");
    s.moveCurve4=value("moveCurve4");s.slope4=value("slope4");
    s.duckMix4=value("duckMix4");
    s.profileModel4=value("profileModel4")>.5f?1:0;
    s.mirrorH4=value("mirrorH4")>.5f;s.mirrorV4=value("mirrorV4")>.5f;
    if(!refs3[(size_t)mode].empty())s.riseSlope=refs3[(size_t)mode][riseSlope3]->load();
}
OpenDuckProcessor::CurveSnapshot OpenDuckProcessor::getCurveSnapshot(int mode) const
{
    mode = resolveMode(mode);
    CurveSnapshot result;
    {
        const juce::SpinLock::ScopedLockType lock(curveLock);
        result = curves[(size_t)mode];
    }
    readCurveParameters(mode, result);
    if (!usesModernCurves(mode))
    {
        result.legacy = true;
        result.shape = (int)value("shape");
        result.custom = result.shape == 5;
        result.phase = value("shift");
        result.length = 1;
        result.tension = value("tension");
        result.amount = value("amount");
        result.attack = value("attack");
        result.release = value("release");
        result.grid = (int)value("grid");
        result.band = false;
        result.count = 8;
        for (int k = 0; k < 8; ++k)
            result.points[(size_t)k] = {(float)k / 7, pointRefs[(size_t)k]->load()};
    }
    else if (result.custom && result.count < 2)
        result = sanitiseCurve(result);
    if(result.modern3){const int g=(int)refs3[(size_t)mode][group3]->load();
        if(g>0){const juce::SpinLock::ScopedLockType lock(groupLock3);
            if(groupValid3[(size_t)(g-1)][(size_t)mode])copyShape3(result,groupShapes3[(size_t)(g-1)][(size_t)mode]);}}
    return result;
}
float OpenDuckProcessor::sampleCurve(const CurveSnapshot &s, float cycle)
{
    if(s.template4&&s.profileModel4==1&&!s.custom)return odx::measured4::sample(s.shape,double(cycle),s.tension,s.mirrorH4,s.mirrorV4,s.slope4);
    if (s.template4 && !s.custom) return std::pow(odx::template4::factoryShape(cycle,s.shape),std::exp2(-s.tension/70));
    if (!std::isfinite(cycle))
        return 1;
    float t = juce::jlimit(0.0f, 1.0f, cycle);
    if (s.legacy)
    {
        t = juce::jlimit(0.0f, 1.0f, t - s.phase / 100);
        if (!s.custom)
            return shape(t, s.shape, s.tension);
        std::array<float, 8> values{};
        for (int k = 0; k < 8; ++k)
            values[(size_t)k] = s.points[(size_t)k].y;
        return customShape(t, values, s.tension);
    }
    if (!(t == 1 && s.phase == 0))
    {
        t = std::fmod(t - s.phase / 100, 1.0f);
        if (t < 0)
            t += 1;
    }
    if(s.modern3)t=std::pow(t,juce::jmax(.0025f,s.length)/juce::jmax(.0625f,s.riseSlope));
    if (s.custom && s.count >= 2)
    {
        const int count = juce::jlimit(2, 256, s.count);
        if (t <= s.points[0].x)
            return odx::clamp(s.points[0].y);
        if (t >= s.points[(size_t)(count - 1)].x)
            return odx::clamp(s.points[(size_t)(count - 1)].y);
        int lo = 0, hi = count - 1;
        while (hi - lo > 1)
        {
            const int mid = (lo + hi) / 2;
            if (s.points[(size_t)mid].x < t)
                lo = mid;
            else
                hi = mid;
        }
        const auto a = s.points[(size_t)lo], b = s.points[(size_t)hi];
        float u = odx::clamp((t - a.x) / juce::jmax(.000001f, b.x - a.x));
        if(s.modern3){
            const float power=std::pow(2.f,-(s.tension/70+a.curvature*2));
            u=a.reversed?1-std::pow(1-u,power):std::pow(u,power);
            const int type=s.linear?5:a.segment;
            if(type==0)u=odx::smooth(u);
            else if(type==3)u=u*u*u*(u*(u*6-15)+10);
            else if(type==4)u=.5f-.5f*std::cos(u*juce::MathConstants<float>::pi);
            if(type==1||type==2){
                const float previous=s.points[(size_t)juce::jmax(0,lo-1)].y;
                const float next=s.points[(size_t)juce::jmin(count-1,hi+1)].y;
                const float scale=type==1?.35f:.5f;
                const float d0=(b.y-previous)*scale,d1=(next-a.y)*scale;
                return odx::clamp((2*u*u*u-3*u*u+1)*a.y+(u*u*u-2*u*u+u)*d0+
                    (-2*u*u*u+3*u*u)*b.y+(u*u*u-u*u)*d1);
            }
        }else if (!s.linear)u = odx::smooth(u);
        return odx::clamp(a.y + (b.y - a.y) * u);
    }
    if(!s.modern3)t = odx::clamp(t / juce::jmax(.05f, s.length));
    const float base=s.modern3&&s.shape==0?odx::smooth((t-.14f)/.43f):odx::factoryShape(t,s.shape);
    return odx::clamp(std::pow(base, std::pow(2.0f, -s.tension / 70)));
}
OpenDuckProcessor::CurveSnapshot OpenDuckProcessor::sanitiseCurve(CurveSnapshot s)
{
    auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
    s.shape = juce::jlimit(0, 16, s.shape);
    s.grid = juce::jlimit(0, 6, s.grid);
    s.phase = juce::jlimit(-50.0f, 50.0f, finite(s.phase, 0));
    s.length = juce::jlimit(.05f, 1.0f, finite(s.length, 1));
    s.tension = juce::jlimit(-100.0f, 100.0f, finite(s.tension, 0));
    s.amount = juce::jlimit(0.0f, 100.0f, finite(s.amount, 100));
    s.attack = juce::jlimit(.1f, 100.0f, finite(s.attack, 10));
    s.release = juce::jlimit(5.0f, 2000.0f, finite(s.release, 150));
    s.focus = juce::jlimit(20.0f, 20000.0f, finite(s.focus, 160));
    s.riseSlope=juce::jlimit(.25f,4.f,finite(s.riseSlope,1));
    s.moveCurve4=juce::jlimit(-50.f,50.f,finite(s.moveCurve4,0));
    s.slope4=juce::jlimit(-100.f,100.f,finite(s.slope4,0));
    s.duckMix4=juce::jlimit(0.f,100.f,finite(s.duckMix4,100));
    s.profileModel4=juce::jlimit(0,1,s.profileModel4);
    s.presetId = juce::jlimit(0, 699, s.presetId);
    const int incoming = juce::jlimit(0, 256, s.count);
    int count = 0;
    for (int k = 0; k < incoming; ++k)
    {
        auto p = s.points[(size_t)k];
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        p.x = odx::clamp(p.x);
        p.y = odx::clamp(p.y);
        p.segment=juce::jlimit(0,5,p.segment);
        p.curvature=juce::jlimit(-1.f,1.f,finite(p.curvature,0));
        s.points[(size_t)count++] = p;
    }
    std::sort(s.points.begin(), s.points.begin() + count, [](auto a, auto b) { return a.x < b.x; });
    int unique = 0;
    for (int k = 0; k < count; ++k)
    {
        if (unique && s.points[(size_t)k].x - s.points[(size_t)(unique - 1)].x < .000001f)
            s.points[(size_t)(unique - 1)].y = s.points[(size_t)k].y;
        else
            s.points[(size_t)unique++] = s.points[(size_t)k];
    }
    if (s.custom || s.shape == 16)
    {
        s.custom = true;
        s.shape = 16;
        if (unique < 2)
        {
            s.points[0] = {0, 0};
            s.points[1] = {1, 1};
            unique = 2;
        }
        s.points[0].x = 0;
        s.points[(size_t)(unique - 1)].x = 1;
    }
    s.count = unique;
    s.legacy = false;
    return s;
}
class OpenDuckCurveAction final : public juce::UndoableAction
{
  public:
    OpenDuckCurveAction(OpenDuckProcessor &p, int m, const odx::CurveSnapshot &b, const odx::CurveSnapshot &a)
        : processor(p), mode(m), before(b), after(a)
    {
    }
    bool perform() override
    {
        processor.applyCurveSnapshot(mode, after);
        return true;
    }
    bool undo() override
    {
        processor.applyCurveSnapshot(mode, before);
        return true;
    }
    int getSizeInUnits() override
    {
        return 2 * (int)sizeof(odx::CurveSnapshot);
    }

  private:
    OpenDuckProcessor &processor;
    int mode;
    odx::CurveSnapshot before, after;
};
void OpenDuckProcessor::applyCurveSnapshot(int mode, const CurveSnapshot &input)
{
    const bool restoreLegacy = input.legacy;
    auto s = sanitiseCurve(input);
    const int group=(int)refs3[(size_t)mode][group3]->load();
    if(group>0&&s.modern3){const juce::SpinLock::ScopedLockType lock(groupLock3);
        groupShapes3[(size_t)(group-1)][(size_t)mode]=s;groupValid3[(size_t)(group-1)][(size_t)mode]=true;
        const juce::SpinLock::ScopedLockType local(curveLock);copyShape3(s,curves[(size_t)mode]);}
    {
        const juce::SpinLock::ScopedLockType lock(curveLock);
        curves[(size_t)mode] = s;
    }
    const char *names[] = {"shape",  "phase",   "length", "tension", "amount",
                           "attack", "release", "grid",   "band",    "focus"};
    const float values[] = {
        (float)s.shape,       s.phase, s.length, s.tension, s.amount, s.attack, s.release, (float)s.grid,
        s.band ? 1.0f : 0.0f, s.focus};
    for (int k = 0; k < curveParameterCount; ++k)
    {
        auto *p = state.getParameter(curveParameterId(names[k], mode));
        p->setValueNotifyingHost(p->convertTo0to1(values[k]));
    }
    if(input.modern3){auto*p=state.getParameter(sectorParameterId("riseSlope",mode));p->setValueNotifyingHost(p->convertTo0to1(s.riseSlope));}
    if(s.template4&&mode==0)for(const auto& entry:std::array<std::pair<const char*,float>,6>{{{"moveCurve4",s.moveCurve4},{"slope4",s.slope4},{"duckMix4",s.duckMix4},{"profileModel4",float(s.profileModel4)},{"mirrorH4",s.mirrorH4?1.f:0.f},{"mirrorV4",s.mirrorV4?1.f:0.f}}})
        if(auto* p=state.getParameter(entry.first))p->setValueNotifyingHost(p->convertTo0to1(entry.second));
    modernMask.fetch_or(1u << (unsigned)mode, std::memory_order_relaxed);
    if (restoreLegacy)
        modernMask.fetch_and(~(1u << (unsigned)mode), std::memory_order_relaxed);
    if(input.modern3) revision3Mask.fetch_or(1u<<(unsigned)mode,std::memory_order_relaxed);
    else revision3Mask.fetch_and(~(1u<<(unsigned)mode),std::memory_order_relaxed);
}
void OpenDuckProcessor::setCurveSnapshot(int mode, const CurveSnapshot &input,
                                         const juce::String &transaction)
{
    mode = resolveMode(mode);
    if (transaction.isNotEmpty())
        undo.beginNewTransaction(transaction);
    auto target = sanitiseCurve(input);
    target.legacy = input.legacy;
    // Public edits opt into the new curve model. Only the undo memento may restore legacy.
    target.legacy = false;
    target.modern3 = usesRevision3(mode);
    undo.perform(new OpenDuckCurveAction(*this, mode, getCurveSnapshot(mode), target));
}
void OpenDuckProcessor::setCustomCurve(int mode, const CurveSnapshot &input, const juce::String &transaction)
{
    auto s = input;
    s.custom = true;
    s.shape = 16;
    setCurveSnapshot(mode, s, transaction);
}
int OpenDuckProcessor::currentBehaviorPresetId(int mode) const
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    return curves[(size_t)resolveMode(mode)].presetId;
}
void OpenDuckProcessor::applyBehaviorPreset(int id, int mode)
{
    mode = resolveMode(mode);
    id = ((id % 700) + 700) % 700;
    const auto &p = factoryPresets()[(size_t)id];
    auto s = getCurveSnapshot(mode);
    s.presetId = id;
    s.shape = p.shape;
    s.phase = p.phase;
    s.length = p.length;
    s.tension = p.tension;
    s.amount = p.amount;
    s.attack = p.attack;
    s.release = p.release;
    s.grid = p.grid;
    s.count = 0;
    s.custom = false;
    s.linear = false;
    s.edit = false;
    s.freeDraw = false;
    s.legacy = false;
    setCurveSnapshot(mode, s, "Factory preset");
}
bool OpenDuckProcessor::editPointsEnabled(int mode) const
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    return curves[(size_t)resolveMode(mode)].edit;
}
void OpenDuckProcessor::setEditPointsEnabled(bool enabled, int mode)
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    auto &c = curves[(size_t)resolveMode(mode)];
    c.edit = enabled;
    if (!enabled)
        c.freeDraw = false;
}
bool OpenDuckProcessor::freeDrawEnabled(int mode) const
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    return curves[(size_t)resolveMode(mode)].freeDraw;
}
void OpenDuckProcessor::setFreeDrawEnabled(bool enabled, int mode)
{
    const juce::SpinLock::ScopedLockType lock(curveLock);
    auto &c = curves[(size_t)resolveMode(mode)];
    c.freeDraw = enabled;
    if (enabled)
        c.edit = true;
}
juce::AudioProcessorValueTreeState::ParameterLayout OpenDuckProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto f = [&](const char *id, float lo, float hi, float initial)
    {
        const juce::String key(id);
        juce::String unit;
        if (key == "amount" || key == "tension" || key == "shift" || key == "mix" || key == "swing")
            unit = "%";
        if (key.startsWith("curveAmount") || key.startsWith("curvePhase") || key.startsWith("curveTension"))
            unit = "%";
        if (key == "attack" || key == "release" || key == "hold" || key.startsWith("bandAttack") ||
            key.startsWith("bandRelease") || key.startsWith("detector"))
            unit = " ms";
        if (key.startsWith("curveAttack") || key.startsWith("curveRelease"))
            unit = " ms";
        if (key == "threshold" || key == "hysteresis")
            unit = " dB";
        if (key == "spectralLow" || key == "spectralHigh" || key.startsWith("cross"))
            unit = " Hz";
        if (key.startsWith("curveFocus"))
            unit = " Hz";
        if (key == "freeBpm")
            unit = " BPM";
        const int decimals = hi <= 1 ? 2 : 1;
        auto attributes =
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction([unit, decimals](float v, int)
                                             { return juce::String(v, decimals) + unit; })
                .withValueFromStringFunction([](const juce::String &text) { return text.getFloatValue(); });
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(id, 1), key == "duckMix4" ? "MIX" : key == "position4" ? "MIX Position" : key == "duration4" ? "PHASE Duration" : key == "moveCurve4" ? "Curve Shift" : key == "slope4" ? "Slope Position" : id, juce::NormalisableRange<float>(lo, hi), initial, attributes));
    };
    auto b = [&](juce::String id, bool initial)
    { p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), id, initial)); };
    f("amount", 0, 100, 80);
    f("attack", 0.1f, 100, 10);
    f("release", 5, 2000, 150);
    f("tension", -100, 100, 0);
    f("shift", -100, 100, 0);
    f("mix", 0, 100, 100);
    f("swing", 0, 75, 0);
    f("freeBpm", 30, 300, 120);
    f("threshold", -60, 0, -24);
    f("hold", 0, 500, 30);
    f("detectorAttack", 0.1f, 20, 1);
    f("detectorRelease", 1, 200, 20);
    f("morph", 0, 1, 0.5f);
    f("spectralLow", 20, 2000, 40);
    f("spectralHigh", 2001, 20000, 8000);
    f("bandLow", 0, 1, 1);
    f("bandMid", 0, 1, 0.65f);
    f("bandHigh", 0, 1, 0.35f);
    f("crossLow", 40, 500, 180);
    f("crossHigh", 1000, 10000, 2500);
    f("hysteresis", 1, 18, 3);
    f("grooveDepth", 0, 1, 0.65f);
    f("spectralDepth", 0, 1, 0.75f);
    for (int k = 0; k < 3; ++k)
    {
        f(("bandAttack" + juce::String(k)).toRawUTF8(), 0.1f, 100, 10);
        f(("bandRelease" + juce::String(k)).toRawUTF8(), 5, 2000, 150);
    }
    b("sync", true);
    b("bypass", true);
    for (int i = 0; i < 6; ++i)
    {
        b("on" + juce::String(i), i == 0);
        b("solo" + juce::String(i), false);
    }
    auto c = [&](const char *id, juce::StringArray options, int initial)
    { p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(id, 1), id, options, initial)); };
    c("selected", {"Ducking", "Spectral", "Multiband", "Morph", "Groove", "Transient"}, 0);
    c("shape", {"Soft", "Aggressive", "Exponential", "Step", "Double", "Custom"}, 0);
    c("trigger", {"Host", "Sidechain", "MIDI"}, 0);
    c("grid", {"1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/8 triplet", "1/16 triplet"}, 0);
    for (int i = 0; i < 16; ++i)
        b("step" + juce::String(i), (i % 4) != 3);
    for (int i = 0; i < 8; ++i)
        f(("point" + juce::String(i)).toRawUTF8(), 0, 1, i == 0 ? 0.0f : 1.0f);
    // Append only: legacy parameter IDs, ranges and ordering remain stable.
    for (int mode = 0; mode < 6; ++mode)
    {
        juce::StringArray choices;
        for (auto *name : odx::shapeNames())
            choices.add(name);
        choices.add("Custom");
        auto id = curveParameterId("shape", mode);
        c(id.toRawUTF8(), choices, 0);
        id = curveParameterId("phase", mode);
        f(id.toRawUTF8(), -50, 50, 0);
        id = curveParameterId("length", mode);
        f(id.toRawUTF8(), .05f, 1, 1);
        id = curveParameterId("tension", mode);
        f(id.toRawUTF8(), -100, 100, 0);
        id = curveParameterId("amount", mode);
        f(id.toRawUTF8(), 0, 100, 100);
        id = curveParameterId("attack", mode);
        f(id.toRawUTF8(), .1f, 100, 10);
        id = curveParameterId("release", mode);
        f(id.toRawUTF8(), 5, 2000, 150);
        id = curveParameterId("grid", mode);
        c(id.toRawUTF8(), {"1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/8 triplet", "1/16 triplet"},
          0);
        b(curveParameterId("band", mode), false);
        id = curveParameterId("focus", mode);
        f(id.toRawUTF8(), 20, 20000, 160);
    }
    for(int mode=0;mode<6;++mode)for(const auto& spec:specs3){
        const auto id=sectorParameterId(spec.base,mode);
        if(spec.choices){auto choices=juce::StringArray::fromTokens(spec.choices,"|","");
            c(id.toRawUTF8(),choices,(int)spec.initial);}
        else f(id.toRawUTF8(),spec.low,spec.high,spec.initial);
    }
    b("template4", true);
    f("position4", 0, 100, 50);
    f("duration4", -50, 50, 0);
    f("moveCurve4", -50, 50, 0);
    f("slope4", -100, 100, 0);
    b("bandEnable4", false);
    juce::NormalisableRange<float> bandRange4(20.f,5120.f);
    bandRange4.setSkewForCentre(320.f);
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("bandCutoff4",1),"Band Cutoff",
        bandRange4,160.f,juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v,int){return juce::String(v,0)+" Hz";})
            .withValueFromStringFunction([](const juce::String& text){return text.getFloatValue();})));
    b("bandListen4", false);
    f("duckMix4", 0, 100, 0);
    b("profileModel4", true);b("mirrorH4", false);b("mirrorV4", false);
    b("antiClick4", true);
    return p;
}
OpenDuckProcessor::OpenDuckProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, &undo, "OpenDuckState", layout())
{
    templateEditHistory4.reserve(16);
    setInputScopeBeats(4);
    state.state.setProperty("templatePresetCatalog4","measured8192-v1",nullptr);
    for (auto *param : getParameters())
        if (auto *named = dynamic_cast<juce::AudioProcessorParameterWithID *>(param))
            raw.emplace(named->paramID.toStdString(), state.getRawParameterValue(named->paramID));
    for (int k = 0; k < 8; ++k)
        pointRefs[(size_t)k] = state.getRawParameterValue("point" + juce::String(k));
    for (int k = 0; k < 6; ++k)
    {
        onRefs[(size_t)k] = state.getRawParameterValue("on" + juce::String(k));
        soloRefs[(size_t)k] = state.getRawParameterValue("solo" + juce::String(k));
    }
    for (int k = 0; k < 16; ++k)
        stepRefs[(size_t)k] = state.getRawParameterValue("step" + juce::String(k));
    const char *bases[] = {"shape",  "phase",   "length", "tension", "amount",
                           "attack", "release", "grid",   "band",    "focus"};
    for (int mode = 0; mode < 6; ++mode)
        for (int k = 0; k < curveParameterCount; ++k)
        {
            const auto id = curveParameterId(bases[k], mode);
            curveRefs[(size_t)mode][(size_t)k] = state.getRawParameterValue(id);
            state.addParameterListener(id, this);
        }
    // New-instance INIT follows the approved concept; loading schema 1 restores its saved mix.
    state.getParameter("mix")->setValueNotifyingHost(state.getParameter("mix")->convertTo0to1(100));
    for(int mode=0;mode<6;++mode)for(const auto& spec:specs3){
        const auto id=sectorParameterId(spec.base,mode);
        refs3[(size_t)mode].push_back(state.getRawParameterValue(id));
        state.addParameterListener(id,this);
    }
    const int firstIds[]={0,120,240,360,480,580};
    for(int mode=0;mode<6;++mode)curves[(size_t)mode].presetId=firstIds[mode];
    for(size_t shapeIndex=0;shapeIndex<16;++shapeIndex)shapeMemories3[0][shapeIndex].end4=templateOneShotEndPhase4[shapeIndex];
    setSectorParameter(0,"end",templateOneShotEndPhase4[0]);
    for(const char* id:{"spectralDepth","grooveDepth","bandMid","bandHigh"}){
        auto*p=state.getParameter(id);p->setValueNotifyingHost(p->convertTo0to1(1));}
    setSectorParameter(2,"bands",2);
    (void)factoryPresets();
    initialiseLocalAssist();
    for(const char* id:{"template4","profileModel4","antiClick4"})state.addParameterListener(id,this);
    publishRouting4();
    startTimerHz(30);
}
bool OpenDuckProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    return (l.getMainInputChannelSet() == juce::AudioChannelSet::mono() ||
            l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()) &&
           l.getMainOutputChannelSet() == l.getMainInputChannelSet();
}
void OpenDuckProcessor::prepareToPlay(double sr, int)
{
    rate = sr;
    curvePhases.fill(.999999);
    curveCycles.fill(0);
    for (int mode = 0; mode < 6; ++mode)
    {
        focusSmoothers[(size_t)mode].reset(sr, .025);
        focusSmoothers[(size_t)mode].setCurrentAndTargetValue(curveRefs[(size_t)mode][curveFocus]->load());
    }
    phase = 0.999999;
    freeCycle = 0;
    follower = 0;
    smoothedGain = 1;
    gate = false;
    hold = 0;
    for (auto &s : waveform)
        s = 0;
    spectral.reset();
    bands.prepare(sr);
    for (auto &d : dryDelay)
        d.fill(0);
    for (auto &d : wetDelay)
        d.fill(0);
    delayPos = 0;
    bandSmooth.fill(0);
    engineSmooth.fill(0);
    spectralSmooth = 0;
    mixSmooth = value("mix") / 100;
    bypassSmooth = value("bypass") > 0.5f ? 0.0f : 1.0f;
    waveCounter = 0;
    wavePeak = 0;
    sideSilence = (int)rate;
    morphSmooth.reset(sr, 0.02);
    morphSmooth.setCurrentAndTargetValue(value("morph"));
    prepareRevision3(sr);
    prepareMeasuredTemplate4(sr);
    prepareLocalAssist();
    // Publish only the completed routing latency. An intermediate legacy value
    // makes hosts that re-prepare after kLatencyChanged restart indefinitely.
    publishRouting4();
}
float OpenDuckProcessor::shape(float t, int type, float tension)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    float y = 1;
    switch (type)
    {
    case 0:
        y = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * juce::jmin(1.0f, t * 2));
        break;
    case 1:
        y = juce::jlimit(0.0f, 1.0f, (t - 0.25f) * 5);
        break;
    case 2:
        y = 1 - std::exp(-7 * t);
        break;
    case 3:
        y = t < 0.5f ? 0.0f : 1.0f;
        break;
    case 4:
        y = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * std::fmod(t * 2, 1.0f));
        break;
    default:
        y = t;
    }
    return std::pow(juce::jlimit(0.0f, 1.0f, y), std::pow(2.0f, tension / 50));
}
float OpenDuckProcessor::customShape(float t, const std::array<float, 8> &points, float tension)
{
    float x = juce::jlimit(0.0f, 1.0f, t) * 7;
    int k = juce::jmin(6, (int)x);
    float f = std::pow(x - k, std::pow(2.0f, tension / 50));
    return juce::jmap(f, points[(size_t)k], points[(size_t)(k + 1)]);
}
void OpenDuckProcessor::processLegacyBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer(buffer, false, 0);
    auto sc = getBusBuffer(buffer, true, 1);
    const int channels = main.getNumChannels();
    const bool hasSc = sc.getNumChannels() > 0;
    const int trigger = (int)value("trigger"), shapeType = (int)value("shape");
    double tempo = value("freeBpm"), ppq = 0;
    bool playing = true, positioned = false;
    const bool synced = value("sync") > 0.5f;
    if (synced)
        if (auto *ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm())
                    tempo = *b;
                playing = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition())
                {
                    ppq = *q;
                    positioned = true;
                }
            }
    bpm = (float)tempo;
    const double divisions[] = {1, 0.5, 0.25, 1.5, 0.75, 1.0 / 3, 1.0 / 6};
    const double beats = divisions[juce::jlimit(0, 6, (int)value("grid"))];
    double cycleIndex = positioned ? std::floor(ppq / beats) : freeCycle;
    if (trigger == 0 && positioned)
        phase = ppq / beats - cycleIndex;
    const float amount = value("amount") / 100, mix = value("mix") / 100, tension = value("tension"),
                shift = value("shift") / 100, swing = value("swing") / 100;
    const float threshold = juce::Decibels::decibelsToGain(value("threshold")),
                hysteresis = juce::Decibels::decibelsToGain(-value("hysteresis"));
    const float a = std::exp(-1.0f / ((value("attack") * 0.001f) * (float)rate)),
                r = std::exp(-1.0f / ((value("release") * 0.001f) * (float)rate)),
                slew = std::exp(-1.0f / (0.005f * (float)rate));
    const float detectorAttack = std::exp(-1.0f / (value("detectorAttack") * 0.001f * (float)rate)),
                detectorRelease = std::exp(-1.0f / (value("detectorRelease") * 0.001f * (float)rate));
    const int holdSamples = (int)(value("hold") * 0.001 * rate);
    const double releaseSeconds = value("release") * 0.001;
    std::array<float, 8> points;
    for (int k = 0; k < 8; ++k)
        points[(size_t)k] = pointRefs[(size_t)k]->load(std::memory_order_relaxed);
    std::array<bool, 6> on, solo;
    bool anySolo = false;
    for (int k = 0; k < 6; ++k)
    {
        on[(size_t)k] = onRefs[(size_t)k]->load(std::memory_order_relaxed) > 0.5f;
        solo[(size_t)k] = soloRefs[(size_t)k]->load(std::memory_order_relaxed) > 0.5f;
        anySolo |= solo[(size_t)k];
    }
    std::array<bool, 16> steps;
    for (int k = 0; k < 16; ++k)
        steps[(size_t)k] = stepRefs[(size_t)k]->load(std::memory_order_relaxed) > 0.5f;
    auto enabled = [&](int k) { return anySolo ? solo[(size_t)k] : on[(size_t)k]; };
    const bool bypass = value("bypass") > 0.5f;
    const float specLow = value("spectralLow"),
                specHigh = juce::jmin(value("spectralHigh"), (float)rate * 0.49f),
                specDepth = value("spectralDepth"), grooveDepth = value("grooveDepth");
    const std::array<float, 3> bandAttack{{std::exp(-1.0f / (value("bandAttack0") * 0.001f * (float)rate)),
                                           std::exp(-1.0f / (value("bandAttack1") * 0.001f * (float)rate)),
                                           std::exp(-1.0f / (value("bandAttack2") * 0.001f * (float)rate))}};
    const std::array<float, 3> bandRelease{
        {std::exp(-1.0f / (value("bandRelease0") * 0.001f * (float)rate)),
         std::exp(-1.0f / (value("bandRelease1") * 0.001f * (float)rate)),
         std::exp(-1.0f / (value("bandRelease2") * 0.001f * (float)rate))}};
    const std::array<float, 3> bandDepth{{value("bandLow"), value("bandMid"), value("bandHigh")}};
    bands.setCutoffs(value("crossLow"), juce::jmin(value("crossHigh"), (float)rate * 0.45f));
    morphSmooth.setTargetValue(value("morph"));
    auto custom = [&](float t) { return customShape(t, points, tension); };
    float peakIn = 0, peakOut = 0, maxReduction = 0;
    std::array<float, 2> channelIn{}, channelOut{};
    std::array<float, 6> activities{};
    auto event = midi.cbegin();
    for (int i = 0; i < main.getNumSamples(); ++i)
    {
        while (event != midi.cend() && (*event).samplePosition <= i)
        {
            if (trigger == 2 && (*event).getMessage().isNoteOn())
                phase = 0;
            ++event;
        }
        float side = 0;
        for (int c = 0; c < sc.getNumChannels(); ++c)
            side += sc.getSample(c, i) / (float)sc.getNumChannels();
        if (std::abs(side) > std::abs(wavePeak))
            wavePeak = side;
        if (++waveCounter >= juce::jmax(1, (int)(rate / 256)))
        {
            waveform[waveWrite.fetch_add(1) % 512].store(wavePeak, std::memory_order_relaxed);
            waveCounter = 0;
            wavePeak = 0;
        }
        if (std::abs(side) > 1e-6f)
            sideSilence = 0;
        else
            sideSilence = juce::jmin(sideSilence + 1, (int)rate);
        float inputLevel = 0;
        for (int c = 0; c < channels; ++c)
            inputLevel = juce::jmax(inputLevel, std::abs(main.getSample(c, i)));
        peakIn = juce::jmax(peakIn, inputLevel);
        const bool externalActive = hasSc && sideSilence < (int)(rate * 0.5);
        const float detector = trigger == 1 ? std::abs(side) : externalActive ? std::abs(side) : inputLevel;
        follower =
            detector + (detector > follower ? detectorAttack : detectorRelease) * (follower - detector);
        if (!gate && follower > threshold)
        {
            if (trigger == 1)
                phase = 0;
            gate = true;
            hold = holdSamples;
        }
        if (hold > 0)
            --hold;
        else if (follower < threshold * hysteresis)
            gate = false;
        float t = (float)phase;
        const float split = 0.5f + swing * 0.5f;
        t = t < split ? 0.5f * t / split : 0.5f + 0.5f * (t - split) / (1 - split);
        t = juce::jlimit(0.0f, 1.0f, t - shift);
        float envelope = shapeType == 5 ? custom(t) : shape(t, shapeType, tension);
        if (trigger != 0 && phase >= 0.999998)
            envelope = 1;
        if (trigger == 0 && !playing)
            envelope = 1;
        const float curveReduction = amount * (1 - envelope);
        const float morph = morphSmooth.getNextValue();
        const float morphEnvelope =
            (1 - morph) * shape(t, 0, tension) + morph * (shapeType == 5 ? custom(t) : shape(t, 1, tension));
        const int step = ((int)cycleIndex % 16 + 16) % 16;
        std::array<float, 6> desired{
            {curveReduction, curveReduction * specDepth, curveReduction, amount * (1 - morphEnvelope),
             steps[(size_t)step] ? curveReduction * grooveDepth : 0, gate ? amount : 0}};
        if (trigger == 0 && !playing)
        {
            desired.fill(0);
        }
        if (trigger != 0 && phase >= 0.999998)
        {
            desired[3] = 0;
        }
        float fullReduction = 0;
        for (int k = 0; k < 6; ++k)
        {
            float target = enabled(k) ? desired[(size_t)k] : 0;
            float &v = engineSmooth[(size_t)k];
            v = target + (target > v ? a : r) * (v - target);
            activities[(size_t)k] = juce::jmax(activities[(size_t)k], v);
            if (k == 0 || k == 3 || k == 4 || k == 5)
                fullReduction = juce::jmax(fullReduction, v);
        }
        // Fullband engines share a maximum reduction; band/spectral stages share a bounded budget.
        std::array<float, 3> gains;
        float minimumBand = 1;
        for (int k = 0; k < 3; ++k)
        {
            float target = enabled(2) ? curveReduction * bandDepth[(size_t)k] : 0;
            float &bd = bandSmooth[(size_t)k];
            bd = target + (target > bd ? bandAttack[(size_t)k] : bandRelease[(size_t)k]) * (bd - target);
            gains[(size_t)k] = 1 - juce::jmax(fullReduction, bd);
            minimumBand = juce::jmin(minimumBand, gains[(size_t)k]);
        }
        const float permittedExtra =
            juce::jlimit(0.0f, 1.0f, (1 - engineSmooth[1]) / juce::jmax(0.05f, minimumBand));
        const float spectralDepth = 1 - permittedExtra;
        float input[2]{}, filtered[2]{}, spec[2]{}, dry[2]{};
        for (int c = 0; c < channels; ++c)
        {
            input[c] = main.getSample(c, i);
            channelIn[(size_t)c] = juce::jmax(channelIn[(size_t)c], std::abs(input[c]));
            dry[c] = dryDelay[(size_t)c][(size_t)delayPos];
            dryDelay[(size_t)c][(size_t)delayPos] = input[c];
            float aligned = 0;
            filtered[c] = bands.process(c, input[c], gains, aligned);
        }
        bands.advance();
        spectral.process(filtered, side, spec, channels, spectralDepth, specLow, specHigh, rate);
        mixSmooth = mix + (mixSmooth - mix) * slew;
        bypassSmooth = (bypass ? 0.0f : 1.0f) + (bypassSmooth - (bypass ? 0.0f : 1.0f)) * slew;
        for (int c = 0; c < channels; ++c)
        {
            float y = dry[c] + (spec[c] - dry[c]) * mixSmooth * bypassSmooth;
            if (!std::isfinite(y))
                y = 0;
            main.setSample(c, i, y);
            peakOut = juce::jmax(peakOut, std::abs(y));
            channelOut[(size_t)c] = juce::jmax(channelOut[(size_t)c], std::abs(y));
        }
        delayPos = (delayPos + 1) % 1152;
        maxReduction = juce::jmax(maxReduction, 1 - minimumBand * (1 - spectralDepth));
        if (trigger == 0)
        {
            if (playing)
            {
                phase += tempo / (60 * rate * beats);
                if (phase >= 1)
                {
                    phase -= 1;
                    cycleIndex += 1;
                }
            }
        }
        else
            phase = juce::jmin(0.999999, phase + 1 / (rate * releaseSeconds));
    }
    sidechainPresent = hasSc && sideSilence < (int)(rate * 0.5);
    freeCycle = cycleIndex;
    for (int c = 0; c < 2; ++c)
    {
        inputPeaks[(size_t)c] = channelIn[(size_t)c];
        outputPeaks[(size_t)c] = channelOut[(size_t)c];
    }
    inMeter = peakIn;
    outMeter = peakOut;
    reduction = maxReduction;
    for (int k = 0; k < 6; ++k)
        engineActivity[(size_t)k] = activities[(size_t)k];
}
void OpenDuckProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto copy = state.copyState();
    copy.setProperty("schema", 4, nullptr);
    copy.setProperty("revision3Mask",(int)revision3Mask.load(),nullptr);
    copy.setProperty("audioRouting",revision3Mask.load()?"serial-0.3":"legacy-budget",nullptr);
    copy.setProperty("modernMask", (int)modernMask.load(std::memory_order_relaxed), nullptr);
    auto old = copy.getChildWithName("Curves");
    if (old.isValid())
        copy.removeChild(old, nullptr);
    juce::ValueTree stored("Curves");
    for (int mode = 0; mode < 6; ++mode)
    {
        auto s = getCurveSnapshot(mode);
        if(usesRevision3(mode)&&(int)refs3[(size_t)mode][group3]->load()>0){
            {const juce::SpinLock::ScopedLockType lock(curveLock);s=curves[(size_t)mode];}readCurveParameters(mode,s);}
        juce::ValueTree curve("Curve");
        curve.setProperty("mode", mode, nullptr);
        curve.setProperty("preset", s.presetId, nullptr);
        curve.setProperty("linear", s.linear, nullptr);
        curve.setProperty("edit", s.edit, nullptr);
        curve.setProperty("freeDraw", s.freeDraw, nullptr);
        curve.setProperty("count", s.count, nullptr);
        for (int k = 0; k < s.count && k < 256; ++k)
        {
            juce::ValueTree point("Point");
            point.setProperty("x", s.points[(size_t)k].x, nullptr);
            point.setProperty("y", s.points[(size_t)k].y, nullptr);
            point.setProperty("segment",s.points[(size_t)k].segment,nullptr);
            point.setProperty("curvature",s.points[(size_t)k].curvature,nullptr);
            point.setProperty("reversed",s.points[(size_t)k].reversed,nullptr);
            curve.addChild(point, -1, nullptr);
        }
        stored.addChild(curve, -1, nullptr);
    }
    copy.addChild(stored, -1, nullptr);
    if(auto previous=copy.getChildWithName("Bands3");previous.isValid())copy.removeChild(previous,nullptr);
    juce::ValueTree bandsStored("Bands3");
    {const juce::SpinLock::ScopedLockType lock(curveLock);for(int mode=0;mode<6;++mode)for(int band=0;band<3;++band){
        auto child=shapeTree3(bandCurves[(size_t)mode][(size_t)band],"BandCurve");child.setProperty("mode",mode,nullptr);child.setProperty("band",band,nullptr);bandsStored.addChild(child,-1,nullptr);}}
    copy.addChild(bandsStored,-1,nullptr);
    if(auto previous=copy.getChildWithName("GroupShapes3");previous.isValid())copy.removeChild(previous,nullptr);
    juce::ValueTree groupsStored("GroupShapes3");for(int mode=0;mode<6;++mode)if((int)refs3[(size_t)mode][group3]->load()>0){
        auto child=shapeTree3(getCurveSnapshot(mode),"GroupShape");child.setProperty("mode",mode,nullptr);groupsStored.addChild(child,-1,nullptr);}
    copy.addChild(groupsStored,-1,nullptr);
    if(auto previous=copy.getChildWithName("ShapeMemories3");previous.isValid())copy.removeChild(previous,nullptr);
    juce::ValueTree memories("ShapeMemories3");{const juce::SpinLock::ScopedLockType lock(curveLock);for(int mode=0;mode<6;++mode)for(int shape=0;shape<16;++shape){
        const auto&m=shapeMemories3[(size_t)mode][(size_t)shape];juce::ValueTree entry("Memory");entry.setProperty("mode",mode,nullptr);entry.setProperty("shape",shape,nullptr);
        entry.setProperty("phase",m.phase,nullptr);entry.setProperty("length",m.length,nullptr);entry.setProperty("tension",m.tension,nullptr);entry.setProperty("riseSlope",m.riseSlope,nullptr);
        entry.setProperty("moveCurve4",m.moveCurve4,nullptr);entry.setProperty("slope4",m.slope4,nullptr);
        entry.setProperty("mirrorH4",m.mirrorH4,nullptr);entry.setProperty("mirrorV4",m.mirrorV4,nullptr);
        entry.setProperty("end4",m.end4,nullptr);memories.addChild(entry,-1,nullptr);}}
    copy.addChild(memories,-1,nullptr);
    auto xml = copy.createXml();
    copyXmlToBinary(*xml, dest);
}
void OpenDuckProcessor::setStateInformation(const void *data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName(state.state.getType()))
        return;
    const int schema = xml->getIntAttribute("schema", 1);
    if (schema < 1 || schema > 4)
        return;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
        return;
    for (int k = 0; k < tree.getNumChildren(); ++k)
    {
        auto p = tree.getChild(k);
        if (p.hasProperty("id") && p.hasProperty("value") && !std::isfinite((double)p.getProperty("value")))
            return;
    }
    auto read = [&](const juce::String &id, float initial)
    {
        for (int k = 0; k < tree.getNumChildren(); ++k)
        {
            auto p = tree.getChild(k);
            if (p.getProperty("id").toString() == id)
                return (float)p.getProperty("value", initial);
        }
        return initial;
    };
    auto write = [&](const juce::String &id, float valueToWrite)
    {
        for (int k = 0; k < tree.getNumChildren(); ++k)
        {
            auto p = tree.getChild(k);
            if (p.getProperty("id").toString() == id)
            {
                p.setProperty("value", valueToWrite, nullptr);
                return;
            }
        }
        juce::ValueTree p("PARAM");
        p.setProperty("id", id, nullptr);
        p.setProperty("value", valueToWrite, nullptr);
        tree.addChild(p, -1, nullptr);
    };
    // The new-instance Power default must never mute processing in older saved
    // sessions that omitted an explicit bypass node.
    write("bypass", read("bypass", 0));
    // END's historical missing-state default is 1. Fresh measured instances
    // use the explicit per-profile defaults, never as a migration side effect.
    write("v3End0",read("v3End0",1));
    const char* editIds4[]{"moveCurve4","slope4","bandEnable4","bandCutoff4","bandListen4","duckMix4","profileModel4","mirrorH4","mirrorV4","antiClick4"};
    constexpr float editLow4[]{-50,-100,0,20,0,0,0,0,0,0},editHigh4[]{50,100,1,5120,1,100,1,1,1,1},editInitial4[]{0,0,0,160,0,100,0,0,0,1};
    for(int i=0;i<10;++i){const float v=read(editIds4[i],editInitial4[i]);
        if(!std::isfinite(v)||v<editLow4[i]||v>editHigh4[i])return;write(editIds4[i],v);}
    if(!tree.hasProperty("templatePresetCatalog4"))tree.setProperty("templatePresetCatalog4",
        read("profileModel4",0)>.5f?"measured8192-v1":"legacy-analytic-v1",nullptr);
    if (schema < 4) { write("template4", 0); write("position4", 50); write("duration4", 0); }
    std::array<CurveSnapshot, 6> loaded{};
    unsigned mask = 0;
    if (schema == 1)
    {
        const int oldShape = juce::jlimit(0, 5, (int)read("shape", 0));
        constexpr int shapeMap[] = {0, 4, 1, 6, 14, 16};
        for (int mode = 0; mode < 6; ++mode)
        {
            auto &s = loaded[(size_t)mode];
            s.modern3=false;
            s.shape = shapeMap[oldShape];
            s.custom = oldShape == 5;
            s.amount = read("amount", 80);
            s.attack = read("attack", 10);
            s.release = read("release", 150);
            s.tension = juce::jlimit(-100.0f, 100.0f, -read("tension", 0) * 1.4f);
            s.phase = juce::jlimit(-50.0f, 50.0f, read("shift", 0));
            s.grid = (int)read("grid", 0);
            if (s.custom)
            {
                s.count = 8;
                for (int k = 0; k < 8; ++k)
                    s.points[(size_t)k] = {(float)k / 7,
                                           read("point" + juce::String(k), k == 0 ? 0.0f : 1.0f)};
            }
            write(curveParameterId("shape", mode), (float)s.shape);
            write(curveParameterId("phase", mode), s.phase);
            write(curveParameterId("length", mode), 1);
            write(curveParameterId("tension", mode), s.tension);
            write(curveParameterId("amount", mode), s.amount);
            write(curveParameterId("attack", mode), s.attack);
            write(curveParameterId("release", mode), s.release);
            write(curveParameterId("grid", mode), (float)s.grid);
            write(curveParameterId("band", mode), 0);
            write(curveParameterId("focus", mode), 160);
        }
    }
    else
    {
        const int storedMask = (int)tree.getProperty("modernMask", 63);
        if (storedMask < 0 || storedMask > 63)
            return;
        mask = (unsigned)storedMask;
        const auto stored = tree.getChildWithName("Curves");
        if (!stored.isValid() || stored.getNumChildren() != 6)
            return;
        unsigned seen = 0;
        for (int k = 0; k < 6; ++k)
        {
            const auto curve = stored.getChild(k);
            const int mode = (int)curve.getProperty("mode", -1);
            if (!curve.hasType("Curve") || mode < 0 || mode > 5 || (seen & (1u << (unsigned)mode)))
                return;
            seen |= 1u << (unsigned)mode;
            auto &s = loaded[(size_t)mode];
            s.modern3=schema>=3&&(((int)tree.getProperty("revision3Mask",63))&(1<<mode));
            s.presetId = (int)curve.getProperty("preset", 0);
            s.linear = (bool)curve.getProperty("linear", false);
            s.edit = (bool)curve.getProperty("edit", false);
            s.freeDraw = (bool)curve.getProperty("freeDraw", false);
            s.count = (int)curve.getProperty("count", curve.getNumChildren());
            if (s.count < 0 || s.count > 256 || s.count != curve.getNumChildren() || s.presetId < 0 ||
                s.presetId > 699)
                return;
            for (int p = 0; p < s.count; ++p)
            {
                const auto point = curve.getChild(p);
                const float x = (float)point.getProperty("x", -1), y = (float)point.getProperty("y", -1);
                if (!point.hasType("Point") || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 ||
                    y < 0 || y > 1 || (p > 0 && x <= s.points[(size_t)(p - 1)].x))
                    return;
                s.points[(size_t)p] = {x, y};
                if(schema>=3){const int segment=(int)point.getProperty("segment",0);
                    const float curvature=(float)point.getProperty("curvature",0);
                    if(segment<0||segment>5||!std::isfinite(curvature)||std::abs(curvature)>1)return;
                    s.points[(size_t)p].segment=segment;s.points[(size_t)p].curvature=curvature;}
                if(schema>=3)s.points[(size_t)p].reversed=(bool)point.getProperty("reversed",false);
            }
            if (read(curveParameterId("shape", mode), 0) == 16 && s.count < 2)
                return;
        }
    }
    std::array<std::array<CurveSnapshot,3>,6> loadedBands{};
    std::array<CurveSnapshot,6> loadedGroups{};std::array<bool,6> hasGroup{};
    std::array<std::array<ShapeMemory3,16>,6> loadedMemories{};
    if(schema>=3){const int revisionMask=(int)tree.getProperty("revision3Mask",-1);if(revisionMask<0||revisionMask>63)return;
        const auto bandsTree=tree.getChildWithName("Bands3");if(!bandsTree.isValid()||bandsTree.getNumChildren()!=18)return;
        unsigned seen=0;for(int j=0;j<18;++j){const auto child=bandsTree.getChild(j);const int mode=(int)child.getProperty("mode",-1),band=(int)child.getProperty("band",-1);
            if(!child.hasType("BandCurve")||mode<0||mode>5||band<0||band>2)return;const unsigned bit=1u<<(unsigned)(mode*3+band);
            if((seen&bit)||!readShapeTree3(child,loadedBands[(size_t)mode][(size_t)band]))return;seen|=bit;}
        const auto groups=tree.getChildWithName("GroupShapes3");for(int j=0;j<groups.getNumChildren();++j){const auto child=groups.getChild(j);const int mode=(int)child.getProperty("mode",-1);
            if(!child.hasType("GroupShape")||mode<0||mode>5||hasGroup[(size_t)mode]||!readShapeTree3(child,loadedGroups[(size_t)mode]))return;hasGroup[(size_t)mode]=true;}
        for(int mode=0;mode<6;++mode)for(const auto&spec:specs3){const float v=read(sectorParameterId(spec.base,mode),spec.initial);if(!std::isfinite(v)||v<spec.low||v>spec.high)return;}
        const auto memories=tree.getChildWithName("ShapeMemories3");std::array<std::array<bool,16>,6> seenMemory{};
        for(int i=0;i<memories.getNumChildren();++i){const auto entry=memories.getChild(i);const int mode=(int)entry.getProperty("mode",-1),shape=(int)entry.getProperty("shape",-1);
            if(!entry.hasType("Memory")||mode<0||mode>5||shape<0||shape>15||seenMemory[(size_t)mode][(size_t)shape])return;
            seenMemory[(size_t)mode][(size_t)shape]=true;auto&m=loadedMemories[(size_t)mode][(size_t)shape];
            m.phase=(float)entry.getProperty("phase",0);m.length=(float)entry.getProperty("length",1);m.tension=(float)entry.getProperty("tension",0);m.riseSlope=(float)entry.getProperty("riseSlope",1);
            m.moveCurve4=(float)entry.getProperty("moveCurve4",0);m.slope4=(float)entry.getProperty("slope4",0);
            m.mirrorH4=(bool)entry.getProperty("mirrorH4",false);m.mirrorV4=(bool)entry.getProperty("mirrorV4",false);
            m.end4=(float)entry.getProperty("end4",read(sectorParameterId("end",mode),1.f));
            if(!std::isfinite(m.phase)||!std::isfinite(m.length)||!std::isfinite(m.tension)||!std::isfinite(m.riseSlope)||!std::isfinite(m.moveCurve4)||!std::isfinite(m.slope4)||!std::isfinite(m.end4)||std::abs(m.phase)>50||m.length<.05f||m.length>1||std::abs(m.tension)>100||m.riseSlope<.25f||m.riseSlope>4||std::abs(m.moveCurve4)>50||std::abs(m.slope4)>100||m.end4<.01f||m.end4>1)return;}
    }else for(int mode=0;mode<6;++mode)for(const auto&spec:specs3)write(sectorParameterId(spec.base,mode),spec.initial);
    loadingState.store(true, std::memory_order_relaxed);
    state.replaceState(tree);
    setInputScopeBeats((double)tree.getProperty("templateWaveBeats",4.0));
    {
        const juce::SpinLock::ScopedLockType lock(curveLock);
        curves = loaded;
        bandCurves=loadedBands;
        shapeMemories3=loadedMemories;
    }
    modernMask.store(mask, std::memory_order_relaxed);
    revision3Mask.store(schema>=3?(unsigned)juce::jlimit(0,63,(int)tree.getProperty("revision3Mask",63)):0);
    {const juce::SpinLock::ScopedLockType lock(groupLock3);for(int mode=0;mode<6;++mode)if(hasGroup[(size_t)mode]){
        const int group=(int)refs3[(size_t)mode][group3]->load();if(group>0){groupShapes3[(size_t)(group-1)][(size_t)mode]=loadedGroups[(size_t)mode];groupValid3[(size_t)(group-1)][(size_t)mode]=true;}}}
    undo.clearUndoHistory();
    if(!restoringTemplateUndo4){templateEditHistory4.clear();templatePendingEdit4.reset();
        templatePendingLabel4.clear();templateEditDepth4=0;}
    resetLocalAssist();
    loadingState.store(false, std::memory_order_relaxed);
    // A host may synchronously save state from its latency notification. Publish
    // it only after grouped shapes and all restore bookkeeping are committed.
    publishRouting4();
}
void OpenDuckProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    pushLocalAssist(buffer);
    const unsigned routing=activeRouting4.load(std::memory_order_acquire);
    if((routing&1u)!=0)
        processMeasuredTemplateBlock4(buffer,midi,routing);
    else if(revision3Mask.load(std::memory_order_relaxed)!=0)
        processRevision3Block(buffer,midi);
    else if (modernMask.load(std::memory_order_relaxed) == 0)
        processLegacyBlock(buffer, midi);
    else
        processModernBlock(buffer, midi);
}
int OpenDuckProcessor::revision3LatencySamples() const {
    return (int)std::ceil(rate*.215)+6*BandEngine::delay+SpectralEngine::size;
}
unsigned OpenDuckProcessor::desiredRouting4() const {
    const bool measured=revision3Mask.load(std::memory_order_relaxed)!=0&&
        value("template4")>.5f&&value("profileModel4")>.5f;
    const unsigned trigger=unsigned(juce::jlimit(0,3,int(value("v3Trigger0"))));
    return (measured?1u:0u)|(trigger<<1u)|
        (value("antiClick4")>.5f?8u:0u);
}
int OpenDuckProcessor::routingLatency4(unsigned routing) const {
    if((routing&1u)==0)return revision3Mask.load(std::memory_order_relaxed)!=0?
        revision3LatencySamples():SpectralEngine::size+BandEngine::delay;
    const unsigned trigger=(routing>>1u)&3u;
    if(trigger==0)return 0;
    // Actual public-reference impulse measurements: MIDI 265/289/577 at
    // 44.1/48/96 kHz; AUDIO adds its independent 10 ms detector budget.
    const int detector=trigger==2?0:int(std::floor(rate*.010));
    const int anti=(routing&8u)!=0?int(std::floor(rate*.006))+1:0;
    return detector+anti;
}
void OpenDuckProcessor::publishRouting4() {
    // Message/prepare/restore thread only. Audio parameter callbacks merely set
    // routingDirty4; the audio callback uses this complete published config.
    const auto routing=desiredRouting4();
    activeRouting4.store(routing,std::memory_order_release);
    const int latency=routingLatency4(routing);
    if(getLatencySamples()!=latency)setLatencySamples(latency);
}
void OpenDuckProcessor::timerCallback() {
    if(!loadingState.load(std::memory_order_acquire)&&routingDirty4.exchange(false,std::memory_order_acq_rel))
        publishRouting4();
}
void OpenDuckProcessor::prepareMeasuredTemplate4(double sr) {
    const size_t capacity=size_t(std::ceil(sr*.016))+16;
    measuredHistory4.assign(capacity,MeasuredHistory4{});
    measuredAntiClick4.prepare(capacity);
    measuredSyncGain4.prepare(sr);
    measuredLowpass4.prepare(sr,value("bandCutoff4"));
    measuredBandBlend4=value("bandEnable4")>.5f?1.f:0.f;
    measuredListenBlend4=value("bandEnable4")>.5f&&value("bandListen4")>.5f?1.f:0.f;
    measuredLastRouting4=~0u;measuredLastEnd4=0;
    measuredLastControls4={};measuredControlsObserved4=false;
    measuredAntiMix4=measuredAntiStartMix4=measuredAntiTargetMix4=value("antiClick4")>.5f?1.f:0.f;
    measuredAntiTransitionLeft4=0;
    templateSidechainPeak4=0;templateDetectorPeak4=0;templateTriggerCount4=0;
    templateSidechainEnabled4=false;
}
void OpenDuckProcessor::processMeasuredTemplateBlock4(juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midi,unsigned routing) {
    juce::ScopedNoDenormals denormals;
    auto main=getBusBuffer(buffer,false,0),side=getBusBuffer(buffer,true,1);
    const int channels=main.getNumChannels();
    if(channels<1||delayCapacity3==0||measuredHistory4.empty())return;
    const int trigger=int((routing>>1u)&3u),latency=routingLatency4(routing);
    const bool antiEnabled=(routing&8u)!=0;
    // Keep both same-mode delay taps and the anticipation queue warm, even
    // while OFF. A live PDC toggle then crossfades complete aligned outputs.
    const int antiSamples=trigger!=0?int(std::floor(rate*.006))+1:0;
    const int shortLatency=trigger==0||trigger==2?0:int(std::floor(rate*.010));
    const int longLatency=shortLatency+antiSamples;
    const float antiTarget=(routing&8u)!=0?1.f:0.f;
    auto& clock=clocks3[0];
    if(measuredLastRouting4!=routing||measuredLastEnd4!=sampleClock3) {
        const bool liveChange=measuredLastRouting4!=~0u;
        const bool antiClickOnly=liveChange&&measuredLastEnd4==sampleClock3&&
            (measuredLastRouting4^routing)==8u;
        const float previousGain=clock.smoothGain[0];
        if(!antiClickOnly)measuredAntiClick4.reset(antiSamples);
        // A PDC/anti-click change within one trigger mode does not retrigger
        // the envelope or release a held MIDI note. Keep its exact audio age.
        if(!antiClickOnly) {
            clock=SectorClock{};clock.lastTrigger=trigger;
            measuredSyncGain4.reset(previousGain);
            if(liveChange)clock.smoothGain[0]=previousGain;
        }
        scopeClocks3[0]=ScopeCaptureClock{};writingScopes[0]=ScopeFrame{};
        if(antiClickOnly) {
            auto& scope=scopeClocks3[0];scope.active=clock.active||clock.releasing;
            scope.held=clock.held;scope.triggerSample=clock.triggerSample;scope.trigger=trigger;
        }
        if(!antiClickOnly) {
            measuredControlsObserved4=false;templateGainTransitionLeft4=0;
            templateGainTransitionPending4=liveChange;
            measuredAntiMix4=measuredAntiStartMix4=measuredAntiTargetMix4=antiTarget;
            measuredAntiTransitionLeft4=0;
        } else {
            measuredAntiStartMix4=measuredAntiMix4;measuredAntiTargetMix4=antiTarget;
            measuredAntiTransitionLeft4=measuredAntiMix4==antiTarget?0:templateGainTransitionTotal4;
        }
        measuredLastRouting4=routing;
    }
    std::array<float,r3Count> p{};
    for(int j=0;j<r3Count;++j)p[size_t(j)]=refs3[0][size_t(j)]->load(std::memory_order_relaxed);
    p[trigger3]=float(trigger);
    double tempo=value("freeBpm"),ppq=0,displayTempo=tempo;
    bool positioned=false,playing=false,hasTransport=false,displayPosition=false;
    const bool synced=value("sync")>.5f;
    if(auto* host=getPlayHead())if(auto position=host->getPosition()) {
        hasTransport=true;
        if(auto t=position->getBpm()){displayTempo=*t;if(synced)tempo=*t;}
        if(auto q=position->getPpqPosition()) {ppq=*q;displayPosition=std::isfinite(ppq);positioned=synced&&displayPosition;}
        playing=position->getIsPlaying();
    }
    tempo=std::isfinite(tempo)?juce::jlimit(20.,400.,tempo):120.;bpm=float(tempo);
    displayTempo=std::isfinite(displayTempo)?juce::jlimit(20.,400.,displayTempo):tempo;
    const double seconds=p[speedMode3]>.5f?1/juce::jmax(.05f,p[hz3]):
        speedBeats3[juce::jlimit(0,19,int(p[speed3]))]*60/tempo;
    if(trigger==0&&p[speedMode3]<.5f&&positioned&&playing) {
        const double cycles=ppq/(seconds*tempo/60);
        clock.phase=cycles-std::floor(cycles);clock.cycle=std::floor(cycles);
    }
    {const juce::SpinLock::ScopedTryLockType lock(curveLock);if(lock.isLocked())audioCurves[0]=curves[0];}
    auto shape=audioCurves[0];readCurveParameters(0,shape);
    shape.template4=true;shape.profileModel4=1;shape.modern3=true;
    const int group=int(p[group3]);
    if(group>0){const juce::SpinLock::ScopedTryLockType lock(groupLock3);
        if(lock.isLocked()&&groupValid3[size_t(group-1)][0])copyShape3(shape,groupShapes3[size_t(group-1)][0]);}
    // Measured template routing is explicitly engine 0 only. Hidden engines'
    // on/solo states cannot silently bring back the serial FIR/FFT delay.
    const bool enabled=onRefs[0]->load(std::memory_order_relaxed)>.5f;
    const bool bandEnabled=value("bandEnable4")>.5f;
    const bool bandListen=bandEnabled&&value("bandListen4")>.5f;
    measuredLowpass4.setCutoff(value("bandCutoff4"));
    const std::array<float,30> controls{{float(shape.shape),shape.custom?1.f:0.f,1.f,
        shape.duckMix4,shape.position4,shape.duration4,shape.tension,shape.amount,shape.length,
        shape.moveCurve4,shape.slope4,shape.mirrorH4?1.f:0.f,shape.mirrorV4?1.f:0.f,
        bandEnabled?1.f:0.f,value("bandCutoff4"),bandListen?1.f:0.f,enabled?1.f:0.f,
        float(trigger),p[speedMode3],float(seconds),p[end3],p[loop3],p[loopStart3],p[loopEnd3],
        p[lookahead3],p[offset3],p[swing3],shape.release,p[runStopped3],synced?1.f:0.f}};
    if(measuredControlsObserved4&&controls!=measuredLastControls4)templateGainTransitionPending4=true;
    measuredLastControls4=controls;measuredControlsObserved4=true;
    const float editStep=1-std::exp(-1.f/(float(rate)*.002f));
    const float slew=std::exp(-1.f/(float(rate)*.005f));
    const float detectorLo=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(p[detectorLow3],float(rate*.45))/float(rate));
    const float detectorHi=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(juce::jmax(p[detectorLow3],p[detectorHigh3]),float(rate*.49))/float(rate));
    const float detectorAttack=std::exp(-1.f/(float(rate)*p[detectorAttack3]*.001f));
    const float detectorRelease=std::exp(-1.f/(float(rate)*p[detectorRelease3]*.001f));
    const float threshold=juce::Decibels::decibelsToGain(p[threshold3]);
    const float releaseThreshold=threshold*juce::Decibels::decibelsToGain(-p[hysteresis3]);
    const double beatStep=displayTempo/(60*rate),displayBeats=getInputScopeBeats();
    const bool inputHostAligned=displayPosition&&playing;
    std::array<float,2> inPeak{},outPeak{};
    float peakIn=0,peakOut=0,activity=0,sidechainPeak=0,detectorPeak=0;
    double dryEnergy=0,outputEnergy=0;
    auto event=midi.cbegin();
    auto gainAt=[&](double rawPhase,bool active,bool held,bool releasing,float velocity,
                    uint64_t age,uint64_t noteOffSample) {
        double at=trigger==0?rawPhase:double(age)/(rate*seconds);
        at+=(p[lookahead3]+p[offset3])*.001/seconds;
        bool inCycle=trigger==0||active||releasing;
        if(trigger==0) {
            at-=std::floor(at);
            if(hasTransport&&!playing&&p[runStopped3]<.5f)inCycle=false;
        } else {
            inCycle&=at>=0;
            if(releasing)at=double(noteOffSample-clock.triggerSample)/(rate*seconds)+
                (p[lookahead3]+p[offset3])*.001/seconds;
            if(int(p[loop3])==1||(int(p[loop3])==2&&(held||releasing))) {
                const double start=p[loopStart3],end=juce::jmax(start+.01,double(p[loopEnd3]));
                if(at>=end)at=start+std::fmod(at-start,end-start);
            } else if(at>=p[end3])at=p[end3]; // Hold the endpoint only after a real trigger.
        }
        at=juce::jlimit(0.,1.,at);
        const double physicalPhase=at;
        if(p[swing3]!=0) {const double split=.5+double(p[swing3])*.005;
            at=at<split?.5*at/split:.5+.5*(at-split)/(1-split);}
        float gain=inCycle?(shape.custom?effectiveCurveSample(shape,float(at),seconds,0,p[character3]):
            trigger!=0&&int(p[loop3])==0?
                measuredTemplateOneShotGain4(shape,physicalPhase,1/(rate*seconds),p[end3],p[swing3]):
                measuredTemplateGain4(shape,at)):1.f;
        gain=1-(1-gain)*velocity;
        if(releasing)gain=1+(gain-1)*(1-odx::clamp(float(double(sampleClock3-noteOffSample)/(rate*shape.release*.001))));
        return std::pair<float,bool>{enabled?(antiEnabled?juce::jlimit(0.f,1.f,gain):gain):1.f,inCycle};
    };
    for(int i=0;i<main.getNumSamples();++i,++sampleClock3) {
        std::array<float,2> input{},sc{},dry{},scAligned{},output{};
        for(int c=0;c<2;++c) {
            const float x=main.getSample(juce::jmin(c,channels-1),i);
            const float s=side.getNumChannels()>0?side.getSample(juce::jmin(c,side.getNumChannels()-1),i):0;
            input[size_t(c)]=std::isfinite(x)?x:0;sc[size_t(c)]=std::isfinite(s)?s:0;
            if(c<side.getNumChannels())sidechainPeak=juce::jmax(sidechainPeak,std::abs(sc[size_t(c)]));
            delay3[0][size_t(c)][size_t(delayPosition3)]=input[size_t(c)];
            delay3[2][size_t(c)][size_t(delayPosition3)]=sc[size_t(c)];
            const size_t delayedSlot=size_t((delayPosition3+delayCapacity3-latency)%delayCapacity3);
            dry[size_t(c)]=latency==0?input[size_t(c)]:delay3[0][size_t(c)][delayedSlot];
            scAligned[size_t(c)]=latency==0?sc[size_t(c)]:delay3[2][size_t(c)][delayedSlot];
            if(c<channels){inPeak[size_t(c)]=juce::jmax(inPeak[size_t(c)],std::abs(input[size_t(c)]));peakIn=juce::jmax(peakIn,inPeak[size_t(c)]);}
        }
        // Smooth only edits, never the stationary measured periodic curve.
        float movement=shape.moveCurve4-templateMoveSmooth4;
        if(movement>50)movement-=100;if(movement< -50)movement+=100;
        templateMoveSmooth4=std::abs(movement)<1.e-5f?shape.moveCurve4:templateMoveSmooth4+movement*editStep;
        if(templateMoveSmooth4>50)templateMoveSmooth4-=100;if(templateMoveSmooth4< -50)templateMoveSmooth4+=100;
        const float slopeDelta=shape.slope4-templateSlopeSmooth4;
        templateSlopeSmooth4=std::abs(slopeDelta)<1.e-5f?shape.slope4:templateSlopeSmooth4+slopeDelta*editStep;
        const float wantedMove=shape.moveCurve4,wantedSlope=shape.slope4;
        shape.moveCurve4=templateMoveSmooth4;shape.slope4=templateSlopeSmooth4;
        const auto before=antiSamples>0?gainAt(clock.phase,clock.active,clock.held,clock.releasing,
            clock.velocity,clock.age,clock.noteOffSample):std::pair<float,bool>{1.f,false};
        bool triggered=false;
        while(event!=midi.cend()&&(*event).samplePosition<=i) {
            const auto metadata=*event;++event;
            if(trigger!=2||!metadata.data||metadata.numBytes!=3)continue;
            const int status=metadata.data[0]&0xf0,channel=(metadata.data[0]&15)+1;
            const int note=metadata.data[1]&127,velocity=metadata.data[2]&127;
            const bool acceptedChannel=int(p[midiChannel3])==0||channel==int(p[midiChannel3]);
            const bool on=status==0x90&&velocity>0,off=status==0x80||(status==0x90&&velocity==0);
            if(on&&acceptedChannel&&note>=int(p[noteLow3])&&note<=int(p[noteHigh3])&&
                    velocity>=int(p[velocityLow3])&&velocity<=int(p[velocityHigh3])) {
                clock.phase=0;clock.age=0;clock.triggerSample=sampleClock3;clock.active=clock.held=true;
                clock.releasing=clock.noteOffPending=false;clock.heldNote=note;
                clock.velocity=1+p[velocityDepth3]*(velocity/127.f-1);clock.cycle+=1;triggered=true;
                enqueueScopeTrigger(0,true,true,seconds);
            } else if(off&&acceptedChannel&&note==clock.heldNote) {
                clock.held=false;enqueueScopeTrigger(0,false,false,seconds);
                if(int(p[loop3])==2){clock.active=false;clock.releasing=true;clock.noteOffSample=sampleClock3;}
            }
        }
        double power=0;float detectorSample=0;
        // The detector follows its selected bus, not the main output width.
        // In particular, mono Main must not discard a right-only stereo SC.
        const int detectorChannels=trigger==3?channels:juce::jmin(2,side.getNumChannels());
        for(int c=0;c<detectorChannels;++c) {
            const float x=trigger==3?input[size_t(c)]:sc[size_t(c)];
            clock.filterLo[size_t(c)]+=detectorLo*(x-clock.filterLo[size_t(c)]);
            const float high=p[detectorLow3]<=20?x:x-clock.filterLo[size_t(c)];
            clock.filterHi[size_t(c)]+=detectorHi*(high-clock.filterHi[size_t(c)]);
            const float filtered=p[detectorHigh3]>=float(rate*.45)?high:clock.filterHi[size_t(c)];
            power+=double(filtered)*filtered;if(std::abs(filtered)>std::abs(detectorSample))detectorSample=filtered;
        }
        const float magnitude=float(std::sqrt(power/juce::jmax(1,detectorChannels)));
        clock.detector=magnitude+(clock.detector-magnitude)*(magnitude>clock.detector?detectorAttack:detectorRelease);
        detectorPeak=juce::jmax(detectorPeak,clock.detector);
        if(clock.holdoff>0)--clock.holdoff;
        if(clock.gate&&clock.detector<releaseThreshold)clock.gate=false;
        if(!clock.gate&&clock.detector>threshold&&clock.holdoff==0) {
            clock.gate=true;clock.holdoff=int(p[holdoff3]*.001*rate);
            if(trigger==1||trigger==3) {
                clock.phase=0;clock.age=0;clock.triggerSample=sampleClock3;clock.active=true;
                clock.held=clock.releasing=clock.noteOffPending=false;clock.velocity=1;clock.cycle+=1;triggered=true;
                enqueueScopeTrigger(0,true,false,seconds);
            }
        }
        const auto target=gainAt(clock.phase,clock.active,clock.held,clock.releasing,clock.velocity,clock.age,clock.noteOffSample);
        float gain=target.first;
        if(triggered)templateTriggerCount4.fetch_add(1,std::memory_order_relaxed);
        if(triggered&&antiSamples>0)measuredAntiClick4.push(int64_t(sampleClock3),gain-before.first);
        if(templateGainTransitionPending4) {
            templateGainTransitionDelta4[0]=clock.smoothGain[0]-gain;
            templateGainTransitionLeft4=templateGainTransitionTotal4;templateGainTransitionPending4=false;
        }
        if(templateGainTransitionLeft4>0) {
            gain=juce::jlimit(-.125f,1.125f,gain+templateGainTransitionDelta4[0]*
                float(templateGainTransitionLeft4)/templateGainTransitionTotal4);
            --templateGainTransitionLeft4;
        }
        if(antiEnabled)gain=juce::jlimit(0.f,1.f,gain);
        if(trigger==0)gain=measuredSyncGain4.process(gain,clock.phase,antiEnabled);
        clock.smoothGain[0]=gain;
        shape.moveCurve4=wantedMove;shape.slope4=wantedSlope;
        if(clock.releasing&&double(sampleClock3-clock.noteOffSample)>=rate*shape.release*.001)clock.releasing=false;
        const float bypassTarget=value("bypass")>.5f?0.f:1.f;
        bypassSmooth=bypassTarget+(bypassSmooth-bypassTarget)*slew;
        if(std::abs(bypassSmooth-bypassTarget)<1.e-7f)bypassSmooth=bypassTarget;
        measuredBandBlend4=(bandEnabled?1.f:0.f)+(measuredBandBlend4-(bandEnabled?1.f:0.f))*slew;
        measuredListenBlend4=(bandListen?1.f:0.f)+(measuredListenBlend4-(bandListen?1.f:0.f))*slew;
        if(std::abs(measuredBandBlend4-(bandEnabled?1.f:0.f))<1.e-7f)measuredBandBlend4=bandEnabled?1.f:0.f;
        if(std::abs(measuredListenBlend4-(bandListen?1.f:0.f))<1.e-7f)measuredListenBlend4=bandListen?1.f:0.f;
        auto& incoming=measuredHistory4[size_t(sampleClock3%measuredHistory4.size())];
        incoming=MeasuredHistory4{};incoming.sample=int64_t(sampleClock3);
        incoming.phase=trigger==0?clock.phase:double(clock.age)/(rate*seconds);
        incoming.gain=gain;incoming.inCycle=target.second;incoming.bandBlend=measuredBandBlend4;
        incoming.listenBlend=measuredListenBlend4;incoming.bypass=bypassSmooth;
        incoming.detector=detectorSample;incoming.detectorListen=p[listen3]>.5f;
        measuredLowpass4.advance();
        for(int c=0;c<2;++c)incoming.low[size_t(c)]=measuredLowpass4.process(c,input[size_t(c)]);
        const int64_t sourceSample=int64_t(sampleClock3)-latency;
        const auto& applied=measuredHistory4[size_t((sampleClock3+measuredHistory4.size()-size_t(latency))%measuredHistory4.size())];
        const bool valid=sourceSample>=0&&applied.sample==sourceSample;
        const auto& shortHistory=measuredHistory4[size_t((sampleClock3+measuredHistory4.size()-size_t(shortLatency))%measuredHistory4.size())];
        const auto& longHistory=measuredHistory4[size_t((sampleClock3+measuredHistory4.size()-size_t(longLatency))%measuredHistory4.size())];
        const bool shortValid=int64_t(sampleClock3)>=shortLatency&&shortHistory.sample==int64_t(sampleClock3)-shortLatency;
        const bool longValid=int64_t(sampleClock3)>=longLatency&&longHistory.sample==int64_t(sampleClock3)-longLatency;
        const float shortGain=shortValid?shortHistory.gain:1.f;
        float longGain=longValid?longHistory.gain:1.f;
        if(longValid&&antiSamples>0) {
            const float correction=measuredAntiClick4.correction(int64_t(sampleClock3)-longLatency);
            if(correction!=0)longGain=juce::jlimit(-.125f,1.125f,longGain+correction);
        }
        // The long tap is the protected path; keep it ready even while the
        // raw OFF tap is selected. Do not clip the resulting audio waveform.
        if(antiSamples>0)longGain=juce::jlimit(0.f,1.f,longGain);
        const float mixedGain=measuredAntiMix4<=0?shortGain:measuredAntiMix4>=1?longGain:
            shortGain+(longGain-shortGain)*measuredAntiMix4;
        activity=juce::jmax(activity,1-mixedGain);
        for(int c=0;c<channels;++c) {
            const auto renderTap=[&](int lag,const MeasuredHistory4& history,bool available,float gain) {
                const size_t slot=size_t((delayPosition3+delayCapacity3-lag)%delayCapacity3);
                const float x=lag==0?input[size_t(c)]:delay3[0][size_t(c)][slot];
                if(!available||history.bypass<=0)return x;
                const float full=gain*x,band=x+(gain-1)*history.low[size_t(c)];
                float wet=full+(band-full)*history.bandBlend;
                wet+=(gain*history.low[size_t(c)]-wet)*history.listenBlend;
                return history.detectorListen?history.detector:x+(wet-x)*history.bypass;
            };
            // Endpoint branches deliberately avoid a blend expression so
            // every settled sample remains bit-identical to its selected tap.
            float y;
            if(measuredAntiMix4<=0)y=renderTap(shortLatency,shortHistory,shortValid,shortGain);
            else if(measuredAntiMix4>=1)y=renderTap(longLatency,longHistory,longValid,longGain);
            else {const float a=renderTap(shortLatency,shortHistory,shortValid,shortGain);
                const float b=renderTap(longLatency,longHistory,longValid,longGain);
                y=a+(b-a)*measuredAntiMix4;}
            if(!std::isfinite(y))y=0;output[size_t(c)]=y;main.setSample(c,i,y);
            outPeak[size_t(c)]=juce::jmax(outPeak[size_t(c)],std::abs(y));peakOut=juce::jmax(peakOut,outPeak[size_t(c)]);
            dryEnergy+=double(dry[size_t(c)])*dry[size_t(c)];outputEnergy+=double(y)*y;
        }
        if(valid) {
            const float m=std::abs(dry[0])>=std::abs(dry[1])?dry[0]:dry[1];
            const float s=std::abs(scAligned[0])>=std::abs(scAligned[1])?scAligned[0]:scAligned[1];
            const float o=std::abs(output[0])>=std::abs(output[1])?output[0]:output[1];
            const auto tapCoefficient=[](const MeasuredHistory4& history,bool available,float tapGain) {
                return !available||history.bypass<=0?1.f:1.f+(tapGain-1.f)*history.bypass;
            };
            const auto tapHasEnvelope=[](const MeasuredHistory4& history,bool available) {
                return available&&(!history.detectorListen||history.bypass<=0);
            };
            const float shortCoefficient=tapCoefficient(shortHistory,shortValid,shortGain);
            const float longCoefficient=tapCoefficient(longHistory,longValid,longGain);
            ScopeEnvelopeSample4 envelope;
            envelope.gain=measuredAntiMix4<=0?shortCoefficient:measuredAntiMix4>=1?longCoefficient:
                shortCoefficient+(longCoefficient-shortCoefficient)*measuredAntiMix4;
            envelope.phase=applied.phase;envelope.cycleSeconds=seconds;envelope.active=applied.inCycle;
            envelope.valid=std::isfinite(envelope.gain)&&
                (measuredAntiMix4>=1||tapHasEnvelope(shortHistory,shortValid))&&
                (measuredAntiMix4<=0||tapHasEnvelope(longHistory,longValid));
            const double sourcePpq=inputHostAligned?ppq+(i-latency)*beatStep:inputScopeFreePpq;
            captureInputScope(m,s,o,sourcePpq,displayTempo,displayBeats,inputHostAligned,
                side.getNumChannels()>0,applied.phase,seconds,&envelope);
            inputScopeFreePpq+=beatStep;
            auto& f=writingScopes[0];auto& scopeClock=scopeClocks3[0];
            const auto recordEnvelope=[&](int bin) {
                f.hasEnvelopeTimeline=true;f.envelopeTriggerCapture=trigger!=0;
                f.envelopeCycleSeconds=envelope.cycleSeconds;
                f.envelopeGain[size_t(bin)]=envelope.gain;
                f.envelopePhase[size_t(bin)]=float(envelope.phase);
                f.envelopeTimeSeconds[size_t(bin)]=float(envelope.phase*envelope.cycleSeconds);
                f.envelopeActive[size_t(bin)]=uint8_t(envelope.active);
                f.envelopeValid[size_t(bin)]=uint8_t(envelope.valid);
            };
            auto publish=[&](){if(f.bins>0&&!f.complete){f.complete=true;f.generation=++scopeGeneration;
                const juce::SpinLock::ScopedTryLockType lock(scopeLock);if(lock.isLocked()&&!scopeFrozen.load())publishedScopes[0]=f;}};
            while(scopeClock.count>0) {
                const auto pending=scopeClock.events[scopeClock.first];
                if(sampleClock3<pending.sample+uint64_t(latency))break;
                scopeClock.first=(scopeClock.first+1)%scopeClock.events.size();--scopeClock.count;
                if(pending.noteOn) {if(f.bins>=juce::jmax(1,int(std::ceil(p[end3]*512))-2))publish();
                    f=ScopeFrame{};scopeClock.active=true;scopeClock.held=pending.held;
                    scopeClock.triggerSample=pending.sample;scopeClock.periodSeconds=pending.periodSeconds;}
                else scopeClock.held=false;
            }
            const bool frozen=scopeFrozen.load(std::memory_order_relaxed);
            if(scopeClock.frozen!=frozen){f=ScopeFrame{};scopeClock.frozen=frozen;}
            double at=applied.phase;bool validScope=trigger==0||scopeClock.active;
            if(trigger!=0&&int(p[loop3])==0&&at>=p[end3]){
                // Record the first real held sample without extending the
                // waveform or pretending that bins after END were captured.
                if(scopeClock.active&&!frozen&&!f.complete&&f.bins>0)recordEnvelope(511);
                publish();validScope=false;
            }
            at-=std::floor(at);const int bin=juce::jlimit(0,511,int(at*512));
            if(f.bins==0&&bin>1)validScope=false;
            if(validScope&&!frozen) {
                if(bin<f.bins-1){publish();f=ScopeFrame{};}
                f.periodSeconds=seconds;f.phase=float(at);f.triggerSample=scopeClock.triggerSample;
                f.sidechain=side.getNumChannels()>0;
                f.mainMin[size_t(bin)]=juce::jmin(f.mainMin[size_t(bin)],m);f.mainMax[size_t(bin)]=juce::jmax(f.mainMax[size_t(bin)],m);
                f.sideMin[size_t(bin)]=juce::jmin(f.sideMin[size_t(bin)],s);f.sideMax[size_t(bin)]=juce::jmax(f.sideMax[size_t(bin)],s);
                f.outMin[size_t(bin)]=juce::jmin(f.outMin[size_t(bin)],o);f.outMax[size_t(bin)]=juce::jmax(f.outMax[size_t(bin)],o);
                const float original=juce::jmax(std::abs(f.mainMin[size_t(bin)]),std::abs(f.mainMax[size_t(bin)]));
                const float result=juce::jmax(std::abs(f.outMin[size_t(bin)]),std::abs(f.outMax[size_t(bin)]));
                f.gain[size_t(bin)]=original>1.e-7f?odx::clamp(result/original):1.f;f.bins=juce::jmax(f.bins,bin+1);
                recordEnvelope(bin);
            }
            playheads3[0].store(float(at),std::memory_order_relaxed);
        }
        clock.phase+=1/(rate*seconds);if(clock.phase>=1){clock.phase-=1;if(trigger==0)clock.cycle+=1;}++clock.age;
        if(measuredAntiTransitionLeft4>0) {
            --measuredAntiTransitionLeft4;
            measuredAntiMix4=measuredAntiTransitionLeft4==0?measuredAntiTargetMix4:
                measuredAntiTargetMix4+(measuredAntiStartMix4-measuredAntiTargetMix4)*
                    float(measuredAntiTransitionLeft4)/templateGainTransitionTotal4;
        }
        delayPosition3=(delayPosition3+1)%delayCapacity3;
    }
    measuredLastEnd4=sampleClock3;
    if(!scopeFrozen.load()){const juce::SpinLock::ScopedTryLockType lock(scopeLock);
        if(lock.isLocked()&&!scopeFrozen.load()&&!publishedScopes[0].complete)publishedScopes[0]=writingScopes[0];}
    publishInputScope();inMeter=peakIn;outMeter=peakOut;
    reduction=dryEnergy>1.e-16?odx::clamp(1-float(std::sqrt(outputEnergy/dryEnergy))):0;
    inputPeaks[0]=inPeak[0];inputPeaks[1]=inPeak[1];outputPeaks[0]=outPeak[0];outputPeaks[1]=outPeak[1];
    sidechainPresent=side.getNumChannels()>0;engineActivity[0]=activity;
    templateSidechainPeak4.store(sidechainPeak,std::memory_order_relaxed);
    templateDetectorPeak4.store(detectorPeak,std::memory_order_relaxed);
    templateSidechainEnabled4.store(side.getNumChannels()>0,std::memory_order_relaxed);
    for(size_t k=1;k<6;++k)engineActivity[k]=0;
}
void OpenDuckProcessor::enqueueScopeTrigger(int mode,bool noteOn,bool held,double periodSeconds){
    auto&clock=scopeClocks3[(size_t)mode];
    if(clock.count==clock.events.size()){
        // An unbounded MIDI burst may exceed the display queue. Drop this
        // incomplete display epoch rather than block, allocate, or mix hits.
        clock.droppedEvents+=clock.count;clock.first=clock.count=0;
        clock.active=clock.held=clock.noteOffPending=false;
        writingScopes[(size_t)mode]=ScopeFrame{};
    }
    clock.events[(clock.first+clock.count)%clock.events.size()]={sampleClock3,periodSeconds,noteOn,held};
    ++clock.count;
}
void OpenDuckProcessor::prepareRevision3(double sr){
    delayCapacity3=(int)std::ceil(sr*.415)+6*BandEngine::delay+SpectralEngine::size+8;delayPosition3=0;sampleClock3=scopeGeneration=0;
    for(auto&bus:delay3)for(auto&channel:bus)channel.assign((size_t)delayCapacity3,0);
    for(auto&history:gateDelay3)history.assign((size_t)delayCapacity3,0);
    for(int k=0;k<6;++k){auto&b=sectorBands[(size_t)k];b.setSlope((int)refs3[(size_t)k][slope3]->load());b.prepare(sr);
        const float low=juce::jlimit(20.f,float(sr*.4),refs3[(size_t)k][crossLow3]->load());
        b.setCutoffs(low,juce::jlimit(low+1,float(sr*.49),refs3[(size_t)k][crossHigh3]->load()));}
    for(auto&c:clocks3)c=SectorClock{};
    phaseSmooth3.fill(0);crossoverMemory3={};writingScopes={};publishedScopes={};scopeClocks3={};
    inputWritingScope={};inputCompletedScope={};
    {const juce::SpinLock::ScopedLockType lock(inputScopeLock);inputPublishedScope={};inputLivePublishedScope={};}
    inputScopeFreePpq=inputScopeNextPpq=inputScopeTempo=inputScopeWindowBeats=0;
    inputScopeGeneration=0;inputScopeLastBin=-1;inputScopeClockActive=inputScopeHasCompleted=false;
    templateMoveSmooth4=value("moveCurve4");templateSlopeSmooth4=value("slope4");
    templateGainTransitionDelta4={};templateLastControls4={};templateGainTransitionLeft4=0;
    templateGainTransitionTotal4=juce::jmax(1,(int)std::round(sr*.002));
    templateControlsObserved4=templateGainTransitionPending4=templateLastInCycle4=false;
}
void OpenDuckProcessor::captureInputScope(float main,float side,float output,double ppq,
        double tempo,double beats,bool hostAligned,bool sidechain,double curvePhase,double curveSeconds,
        const ScopeEnvelopeSample4* envelope) {
    const double step=tempo/(60*rate),epoch=std::floor(ppq/beats)*beats;
    const double at=juce::jlimit(0.,1.,(ppq-epoch)/beats);
    const int bin=juce::jlimit(0,511,(int)(at*512));
    auto& f=inputWritingScope;
    const bool changed=!inputScopeClockActive||std::abs(inputScopeTempo-tempo)>1.e-9
        ||std::abs(inputScopeWindowBeats-beats)>1.e-9||f.hostAligned!=hostAligned;
    const bool jumped=inputScopeClockActive&&std::abs(ppq-inputScopeNextPpq)>step*2.5;
    const bool nextEpoch=inputScopeClockActive&&std::abs(epoch-f.startPpq)>1.e-9;
    if(changed||jumped||nextEpoch||bin<inputScopeLastBin) {
        // A full, continuous bar is held while its successor is assembled. A
        // tempo/transport/view change instead publishes new real partial data.
        if(!changed&&!jumped&&nextEpoch&&f.firstBin==0&&f.bins==512) {
            f.complete=true;inputCompletedScope=f;inputScopeHasCompleted=true;
        } else if(changed||jumped) inputScopeHasCompleted=false;
        f=ScopeFrame{};f.firstBin=bin;f.startPpq=epoch;f.captureBeats=beats;
        f.hostAligned=hostAligned;f.periodSeconds=beats*60/tempo;
        f.curvePhaseStart=curvePhase-at*f.periodSeconds/juce::jmax(.000001,curveSeconds);
        f.generation=++inputScopeGeneration;
        inputScopeTempo=tempo;inputScopeWindowBeats=beats;inputScopeClockActive=true;
    }
    f.sidechain=sidechain;f.phase=float(at);
    f.mainMin[(size_t)bin]=juce::jmin(f.mainMin[(size_t)bin],main);
    f.mainMax[(size_t)bin]=juce::jmax(f.mainMax[(size_t)bin],main);
    f.sideMin[(size_t)bin]=juce::jmin(f.sideMin[(size_t)bin],side);
    f.sideMax[(size_t)bin]=juce::jmax(f.sideMax[(size_t)bin],side);
    f.outMin[(size_t)bin]=juce::jmin(f.outMin[(size_t)bin],output);
    f.outMax[(size_t)bin]=juce::jmax(f.outMax[(size_t)bin],output);
    const float mainPeak=juce::jmax(std::abs(f.mainMin[(size_t)bin]),std::abs(f.mainMax[(size_t)bin]));
    const float outPeak=juce::jmax(std::abs(f.outMin[(size_t)bin]),std::abs(f.outMax[(size_t)bin]));
    f.gain[(size_t)bin]=mainPeak>1.e-7f?odx::clamp(outPeak/mainPeak):1;
    if(envelope) {
        f.hasEnvelopeTimeline=true;f.envelopeTriggerCapture=false;
        f.envelopeCycleSeconds=envelope->cycleSeconds;
        f.envelopeGain[size_t(bin)]=envelope->gain;
        f.envelopePhase[size_t(bin)]=float(envelope->phase);
        f.envelopeTimeSeconds[size_t(bin)]=float((ppq-epoch)*60/tempo);
        f.envelopeActive[size_t(bin)]=uint8_t(envelope->active);
        f.envelopeValid[size_t(bin)]=uint8_t(envelope->valid);
    }
    f.bins=juce::jmax(f.bins,bin+1);inputScopeLastBin=bin;inputScopeNextPpq=ppq+step;
}
void OpenDuckProcessor::publishInputScope() {
    if(!inputScopeClockActive||scopeFrozen.load(std::memory_order_relaxed))return;
    const juce::SpinLock::ScopedTryLockType lock(inputScopeLock);
    if(lock.isLocked()&&!scopeFrozen.load(std::memory_order_relaxed)) {
        inputLivePublishedScope=inputWritingScope;
        inputPublishedScope=inputScopeHasCompleted?inputCompletedScope:inputWritingScope;
    }
}
void OpenDuckProcessor::processRevision3Block(juce::AudioBuffer<float>&buffer,juce::MidiBuffer&midi){
    juce::ScopedNoDenormals denormals;
    auto main=getBusBuffer(buffer,false,0),sideBus=getBusBuffer(buffer,true,1);
    const int channels=main.getNumChannels();if(channels<1||delayCapacity3==0)return;
    const int latency=revision3LatencySamples(),mainDelay=(int)std::ceil(rate*.215);
    double tempo=value("freeBpm"),ppq=0,displayTempo=tempo;
    bool position=false,playing=false,hasTransport=false,displayHostPosition=false;
    const bool synced=value("sync")>.5f;
    if(auto*h=getPlayHead())if(auto p=h->getPosition()){
        hasTransport=true;if(auto t=p->getBpm()){displayTempo=*t;if(synced)tempo=*t;}
        if(auto q=p->getPpqPosition()){ppq=*q;displayHostPosition=std::isfinite(ppq);position=synced&&displayHostPosition;}playing=p->getIsPlaying();}
    tempo=std::isfinite(tempo)?juce::jlimit(20.,400.,tempo):120;bpm=float(tempo);
    displayTempo=std::isfinite(displayTempo)?juce::jlimit(20.,400.,displayTempo):tempo;
    const bool inputHostAligned=displayHostPosition&&playing;
    const double displayBeatStep=displayTempo/(60*rate),displayBeats=getInputScopeBeats();
    std::array<CurveSnapshot,6> shapes;
    {const juce::SpinLock::ScopedTryLockType lock(curveLock);if(lock.isLocked()){audioCurves=curves;audioBandCurves=bandCurves;}}
    std::array<std::array<float,r3Count>,6> settings{};
    std::array<double,6> seconds{};
    std::array<bool,6> enabled{};bool anySolo=false;
    const bool templateBandEnabled=value("template4")>.5f&&value("bandEnable4")>.5f;
    const bool templateBandListen=templateBandEnabled&&value("bandListen4")>.5f;
    for(int k=0;k<6;++k)anySolo|=soloRefs[(size_t)k]->load()>.5f;
    for(int k=0;k<6;++k){const auto n=(size_t)k;auto&s=shapes[n];s=audioCurves[n];readCurveParameters(k,s);
        auto&p=settings[n];for(int j=0;j<r3Count;++j)p[(size_t)j]=refs3[n][(size_t)j]->load(std::memory_order_relaxed);
        if(clocks3[n].lastTrigger!=(int)p[trigger3]){clocks3[n]=SectorClock{};clocks3[n].lastTrigger=(int)p[trigger3];}
        const int group=(int)p[group3];if(group>0){const juce::SpinLock::ScopedTryLockType lock(groupLock3);if(lock.isLocked()&&groupValid3[(size_t)(group-1)][n])copyShape3(s,groupShapes3[(size_t)(group-1)][n]);}
        enabled[n]=(anySolo?soloRefs[n]:onRefs[n])->load()>.5f;
        seconds[n]=p[speedMode3]>.5f?1/juce::jmax(.05f,p[hz3]):speedBeats3[juce::jlimit(0,19,(int)p[speed3])]*60/tempo;
        auto&scopeClock=scopeClocks3[n];
        if(scopeClock.trigger!=(int)p[trigger3]||std::abs(scopeClock.periodSeconds-seconds[n])>1e-9){
            // Do not combine bins captured on two different physical time axes.
            // The last published frame retains its own period until replaced.
            scopeClock=ScopeCaptureClock{};scopeClock.trigger=(int)p[trigger3];scopeClock.periodSeconds=seconds[n];writingScopes[n]=ScopeFrame{};}
        if((int)p[trigger3]==0&&p[speedMode3]<.5f&&position&&playing){const double cycles=ppq/(seconds[n]*tempo/60);clocks3[n].phase=cycles-std::floor(cycles);clocks3[n].cycle=std::floor(cycles);}
        if(k==0&&templateBandEnabled){
            // Template BAND is a complementary low/high split in engine 0.
            // Host-visible legacy band settings retain their saved values.
            p[bands3]=1;p[linked3]=1;p[phaseMode3]=1;
            p[lowSolo3]=p[midSolo3]=p[highSolo3]=0;
            p[crossLow3]=value("bandCutoff4");
            p[crossHigh3]=juce::jmax(p[crossHigh3],p[crossLow3]+1);
        }
        sectorBands[n].setSlope((int)p[slope3]);
        p[crossLow3]=juce::jlimit(20.f,float(rate*.4),p[crossLow3]);
        p[crossHigh3]=juce::jlimit(p[crossLow3]+1,float(rate*.49),p[crossHigh3]);
        sectorBands[n].setCutoffs(p[crossLow3],p[crossHigh3]);
    }
    const auto& tc=shapes[0];
    const std::array<float,20> templateControls{{float(tc.shape),tc.custom?1.f:0.f,float(tc.profileModel4),
        tc.duckMix4,tc.position4,tc.duration4,tc.tension,tc.amount,tc.length,tc.moveCurve4,tc.slope4,
        tc.mirrorH4?1.f:0.f,tc.mirrorV4?1.f:0.f,templateBandEnabled?1.f:0.f,
        templateBandEnabled?value("bandCutoff4"):160.f,templateBandListen?1.f:0.f,
        enabled[0]?1.f:0.f,settings[0][trigger3],settings[0][speedMode3],float(seconds[0])}};
    if(templateControlsObserved4&&templateControls!=templateLastControls4)templateGainTransitionPending4=true;
    templateLastControls4=templateControls;templateControlsObserved4=true;
    const float mix=value("template4")>.5f?1.f:odx::clamp(value("mix")/100),slew=std::exp(-1.f/(float(rate)*.005f));
    const float curveEditStep4=1-std::exp(-1.f/(float(rate)*.002f));
    float peakIn=0,peakOut=0;double dryEnergy=0,outputEnergy=0;std::array<float,6> activity{};
    std::array<float,2> inPeak{},outPeak{};auto event=midi.cbegin();
    for(int i=0;i<main.getNumSamples();++i,++sampleClock3){
        std::array<float,2> input{},sc{},delayed{},dry{},scAligned{};
        for(int c=0;c<2;++c){float x=c<channels?main.getSample(c,i):main.getSample(0,i);
            input[(size_t)c]=std::isfinite(x)?x:0;
            float s=sideBus.getNumChannels()?sideBus.getSample(juce::jmin(c,sideBus.getNumChannels()-1),i):0;
            sc[(size_t)c]=std::isfinite(s)?s:0;
            const auto read=[&](int bus,int lag){return delay3[(size_t)bus][(size_t)c][(size_t)((delayPosition3+delayCapacity3-lag)%delayCapacity3)];};
            dry[(size_t)c]=read(0,latency);delayed[(size_t)c]=read(1,mainDelay);scAligned[(size_t)c]=read(2,latency);
            delay3[0][(size_t)c][(size_t)delayPosition3]=input[(size_t)c];delay3[1][(size_t)c][(size_t)delayPosition3]=input[(size_t)c];delay3[2][(size_t)c][(size_t)delayPosition3]=sc[(size_t)c];
            inPeak[(size_t)c]=juce::jmax(inPeak[(size_t)c],std::abs(input[(size_t)c]));peakIn=juce::jmax(peakIn,inPeak[(size_t)c]);
        }
        // MIDI events retain their exact sample offsets. Each sector has its own note/channel filters.
        while(event!=midi.cend()&&(*event).samplePosition<=i){const auto metadata=*event;
            const int status=metadata.data&&metadata.numBytes==3?(metadata.data[0]&0xf0):0;
            if(status!=0x80&&status!=0x90){++event;continue;}
            // Read the three short bytes directly. Constructing MidiMessage for SysEx/text
            // would allocate owned payload storage inside this callback.
            const int midiChannel=(metadata.data[0]&15)+1,noteNumber=metadata.data[1]&127,velocity=metadata.data[2]&127;
            const bool noteOn=status==0x90&&velocity>0,noteOff=status==0x80||(status==0x90&&velocity==0);
            for(int k=0;k<6;++k){auto&clock=clocks3[(size_t)k];const auto&p=settings[(size_t)k];if((int)p[trigger3]!=2)continue;
                const bool channel=(int)p[midiChannel3]==0||midiChannel==(int)p[midiChannel3];
                if(noteOn&&channel&&noteNumber>=(int)p[noteLow3]&&noteNumber<=(int)p[noteHigh3]&&
                    velocity>=(int)p[velocityLow3]&&velocity<=(int)p[velocityHigh3]){
                    clock.phase=0;clock.age=0;clock.triggerSample=sampleClock3;clock.active=clock.held=true;clock.releasing=false;
                    clock.noteOffPending=false;
                    clock.heldNote=noteNumber;clock.velocity=1+p[velocityDepth3]*(velocity/127.f-1);clock.cycle+=1;
                    enqueueScopeTrigger(k,true,true,seconds[(size_t)k]);
                }else if(noteOff&&channel&&noteNumber==clock.heldNote){clock.held=false;enqueueScopeTrigger(k,false,false,seconds[(size_t)k]);if((int)p[loop3]==2){clock.noteOffPending=true;clock.noteOffSample=sampleClock3;}}
            }++event;
        }
        float detectorListen=0;bool listening=false;float spectralDepth=0;std::array<float,3> spectralGains{{1,1,1}};
        std::array<float,6> appliedGain{};
        for(int k=0;k<6;++k){const auto n=(size_t)k;auto&clock=clocks3[n];auto&p=settings[n];const auto&shape=shapes[n];
            double power=0;float detectorSample=0;
            for(int c=0;c<channels;++c){const bool inputDetector=(int)p[trigger3]==3||((int)p[trigger3]==0&&k==5);
                const float x=inputDetector?input[(size_t)c]:sc[(size_t)c];
                const float a=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(p[detectorLow3],float(rate*.45))/float(rate));
                const float b=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(juce::jmax(p[detectorLow3],p[detectorHigh3]),float(rate*.49))/float(rate));
                clock.filterLo[(size_t)c]+=a*(x-clock.filterLo[(size_t)c]);
                const float high=p[detectorLow3]<=20?x:x-clock.filterLo[(size_t)c];
                clock.filterHi[(size_t)c]+=b*(high-clock.filterHi[(size_t)c]);
                const float filtered=p[detectorHigh3]>=float(rate*.45)?high:clock.filterHi[(size_t)c];
                power+=filtered*filtered;if(std::abs(filtered)>std::abs(detectorSample))detectorSample=filtered;
            }
            const float magnitude=float(std::sqrt(power/channels));
            const float tau=(magnitude>clock.detector?p[detectorAttack3]:p[detectorRelease3])*.001f;
            clock.detector=magnitude+(clock.detector-magnitude)*std::exp(-1.f/(float(rate)*tau));
            const float threshold=juce::Decibels::decibelsToGain(p[threshold3]);
            if(clock.holdoff>0)--clock.holdoff;
            if(clock.gate&&clock.detector<threshold*juce::Decibels::decibelsToGain(-p[hysteresis3]))clock.gate=false;
            if(!clock.gate&&clock.detector>threshold&&clock.holdoff==0){clock.gate=true;clock.holdoff=int(p[holdoff3]*.001*rate);
                if((int)p[trigger3]==1||(int)p[trigger3]==3){clock.phase=0;clock.age=0;clock.triggerSample=sampleClock3;clock.active=true;clock.releasing=false;clock.noteOffPending=false;clock.velocity=1;clock.cycle+=1;enqueueScopeTrigger(k,true,false,seconds[n]);}}
            if(p[listen3]>.5f){listening=true;detectorListen=detectorSample;}
            // Delay modulation to the program's stage, then subtract the requested anticipation.
            const double prefix=k==1?6*BandEngine::delay+SpectralEngine::size*.5:(k+1)*BandEngine::delay;
            const double controlDelay=(mainDelay+prefix)/rate-(p[lookahead3]+p[offset3])*.001;
            const double playbackSample=double(sampleClock3)-controlDelay*rate;
            const bool releaseAtPlayback=(int)p[loop3]==2&&clock.noteOffPending&&playbackSample>=double(clock.noteOffSample);
            const bool heldAtPlayback=clock.held||(clock.noteOffPending&&!releaseAtPlayback);
            if(releaseAtPlayback){clock.active=false;clock.releasing=true;
                clock.releaseGain=odx::clamp(float((playbackSample-clock.noteOffSample)/(rate*shape.release*.001)));}
            gateDelay3[n][(size_t)delayPosition3]=clock.gate?1.f:0.f;
            const int gateLag=juce::jlimit(0,delayCapacity3-1,(int)std::round(controlDelay*rate));
            const float alignedGate=gateDelay3[n][(size_t)((delayPosition3+delayCapacity3-gateLag)%delayCapacity3)];
            double phaseFor=clock.phase-controlDelay/seconds[n];bool inCycle=true;
            if((int)p[trigger3]==0){phaseFor-=std::floor(phaseFor);if(phaseFor>=p[end3]){phaseFor=p[end3];inCycle=p[endHold3]>.5f;}}
            else {phaseFor=double(clock.age)/rate/seconds[n]-controlDelay/seconds[n];inCycle=phaseFor>=0;
                if(releaseAtPlayback)phaseFor=double(clock.noteOffSample-clock.triggerSample)/(rate*seconds[n]);
                if((int)p[loop3]==1||((int)p[loop3]==2&&(heldAtPlayback||releaseAtPlayback))){
                    const double start=p[loopStart3],end=juce::jmax(start+.01,double(p[loopEnd3]));
                    if(phaseFor>=end)phaseFor=start+std::fmod(phaseFor-start,end-start);
                }else if(phaseFor>=p[end3]){phaseFor=p[end3];inCycle=p[endHold3]>.5f;}
                inCycle&=clock.active||clock.releasing;
            }
            if((int)p[trigger3]==0&&hasTransport&&!playing&&p[runStopped3]<.5f)inCycle=false;
            float phaseValue=juce::jlimit(0.f,1.f,float(phaseFor));
            const float swing=p[swing3]/100,split=.5f+swing*.5f;
            phaseValue=phaseValue<split?.5f*phaseValue/split:.5f+.5f*(phaseValue-split)/(1-split);
            double precisePhase=juce::jlimit(0.,1.,phaseFor);
            if(p[swing3]!=0){const double split4=.5+double(p[swing3])*.005;
                precisePhase=precisePhase<split4?.5*precisePhase/split4:.5+.5*(precisePhase-split4)/(1-split4);}
            auto current=shape;current.phase=phaseSmooth3[n];
            float delta=shape.phase-phaseSmooth3[n];if(delta>50)delta-=100;if(delta< -50)delta+=100;
            phaseSmooth3[n]+=delta*(1-std::exp(-1.f/(float(rate)*.002f)));if(phaseSmooth3[n]>50)phaseSmooth3[n]-=100;if(phaseSmooth3[n]<-50)phaseSmooth3[n]+=100;
            if(current.template4){float movement=shape.moveCurve4-templateMoveSmooth4;
                if(movement>50)movement-=100;if(movement< -50)movement+=100;
                if(std::abs(movement)<1.e-5f)templateMoveSmooth4=shape.moveCurve4;
                else templateMoveSmooth4+=movement*curveEditStep4;
                if(templateMoveSmooth4>50)templateMoveSmooth4-=100;if(templateMoveSmooth4< -50)templateMoveSmooth4+=100;
                const float slopeDelta=shape.slope4-templateSlopeSmooth4;
                if(std::abs(slopeDelta)<1.e-5f)templateSlopeSmooth4=shape.slope4;
                else templateSlopeSmooth4+=slopeDelta*curveEditStep4;
                current.moveCurve4=templateMoveSmooth4;current.slope4=templateSlopeSmooth4;}
            const float character=k==3?(p[character3]+value("morph"))*.5f:p[character3];
            const bool measuredProfile=current.template4&&current.profileModel4==1&&!current.custom;
            float baseGain=inCycle?(measuredProfile?measuredTemplateGain4(current,precisePhase):effectiveCurveSample(current,phaseValue,seconds[n],k,character)):1;
            if(k==0){if(measuredProfile&&inCycle!=templateLastInCycle4)templateGainTransitionPending4=true;
                templateLastInCycle4=inCycle;}
            if(k==4){const int step=((int)clock.cycle%16+16)%16;if(stepRefs[(size_t)step]->load()<.5f)baseGain=1;
                else baseGain=1-(1-baseGain)*value("grooveDepth");}
            if(k==5){const float gateDepth=(int)p[trigger3]==2?1.f:alignedGate;baseGain=1-(1-baseGain)*gateDepth;}
            baseGain=1-(1-baseGain)*clock.velocity;
            if(clock.releasing)baseGain=1+(baseGain-1)*(1-clock.releaseGain);
            int bandCount=(int)p[bands3];if(shape.band&&bandCount==0)bandCount=1;
            std::array<float,3> gains{{baseGain,baseGain,baseGain}};
            if(bandCount>0){for(int b=0;b<3;++b){float g=baseGain;
                    float depth=odx::clamp(p[(size_t)(lowAmount3+b)]/100);
                    if(k==2){const char* ids[]={"bandLow","bandMid","bandHigh"};depth*=odx::clamp(value(ids[b]));}
                    if(p[linked3]<.5f||depth!=1){auto bandCurve=current;
                        if(p[linked3]<.5f){bandCurve=audioBandCurves[n][(size_t)b];bandCurve.modern3=true;
                            bandCurve.phase=(wrap3((current.phase+bandCurve.phase)/100+.5f)-.5f)*100;
                            bandCurve.attack*=current.attack/10;bandCurve.release*=current.release/150;bandCurve.amount*=current.amount/100;
                            bandCurve.tension=juce::jlimit(-100.f,100.f,bandCurve.tension+current.tension);bandCurve.length*=current.length;bandCurve.riseSlope*=current.riseSlope;}
                        // Compose all Amount controls before the same amplitude-envelope
                        // law used by the master and the editor's gain inverse.
                        bandCurve.amount=juce::jlimit(0.f,100.f,bandCurve.amount*depth);
                        g=inCycle?(bandCurve.template4&&bandCurve.profileModel4==1&&!bandCurve.custom?
                            measuredTemplateGain4(bandCurve,precisePhase):effectiveCurveSample(bandCurve,phaseValue,seconds[n],k,character)):1;
                        if(shape.template4&&!bandCurve.template4&&shape.duckMix4<100){
                            const float depthMix=odx::clamp(shape.duckMix4/100);
                            g=depthMix<=0?1.f:1+(g-1)*depthMix;}
                        if(k==4){const int step=((int)clock.cycle%16+16)%16;g=stepRefs[(size_t)step]->load()>.5f?1-(1-g)*value("grooveDepth"):1;}
                        if(k==5)g=1-(1-g)*((int)p[trigger3]==2?1.f:alignedGate);
                        g=1-(1-g)*clock.velocity;
                        if(clock.releasing)g=1+(g-1)*(1-clock.releaseGain);
                    }
                    gains[(size_t)b]=g;}
                if(bandCount==1)gains[1]=gains[2];
                if(shape.band&&(int)p[bands3]==0){gains[1]=gains[2]=1;}
            }
            if(k==0&&templateBandEnabled)gains={baseGain,templateBandListen?0.f:1.f,templateBandListen?0.f:1.f};
            if(clock.releasing&&clock.releaseGain>=1)clock.releasing=false;
            const bool transitionOnly=measuredProfile&&(int)p[trigger3]==0;
            if(transitionOnly&&templateGainTransitionPending4){
                for(int b=0;b<3;++b)templateGainTransitionDelta4[(size_t)b]=clock.smoothGain[(size_t)b]-(enabled[n]?gains[(size_t)b]:1.f);
                templateGainTransitionLeft4=templateGainTransitionTotal4;templateGainTransitionPending4=false;}
            const float smooth=transitionOnly||p[smooth3]<=0?0:std::exp(-1.f/(float(rate)*p[smooth3]*.001f));
            for(int b=0;b<3;++b){float target=enabled[n]?gains[(size_t)b]:1;
                auto&g=clock.smoothGain[(size_t)b];
                if(transitionOnly){g=target;
                    if(templateGainTransitionLeft4>0)g=juce::jlimit(-.125f,1.125f,target+templateGainTransitionDelta4[(size_t)b]*
                        float(templateGainTransitionLeft4)/templateGainTransitionTotal4);}
                else {g=target+(g-target)*smooth;if(target==0&&g<1e-7f)g=0;}
                gains[(size_t)b]=g;}
            if(transitionOnly&&templateGainTransitionLeft4>0)--templateGainTransitionLeft4;
            if(k==1){for(int b=0;b<3;++b)spectralGains[(size_t)b]=enabled[n]?1-(1-gains[(size_t)b])*value("spectralDepth"):1;}
            const bool bandSolo=bandCount>0&&(p[lowSolo3]>.5f||p[highSolo3]>.5f||(bandCount==2&&p[midSolo3]>.5f));
            auto selectedBand=[&](int b){return b==0?p[lowSolo3]>.5f:bandCount==1?p[highSolo3]>.5f:p[(size_t)(lowSolo3+b)]>.5f;};
            if(bandSolo&&enabled[n])for(int b=0;b<3;++b)if(!selectedBand(b))gains[(size_t)b]=0;
            if(k==1){gains={1,1,1};if(bandSolo&&enabled[n])for(int b=0;b<3;++b)if(!selectedBand(b))gains[(size_t)b]=0;}
            appliedGain[n]=k==1?*std::min_element(spectralGains.begin(),spectralGains.end()):*std::min_element(gains.begin(),gains.end());activity[n]=juce::jmax(activity[n],1-appliedGain[n]);
            const float output=enabled[n]?juce::Decibels::decibelsToGain(p[output3]):1;
            for(int c=0;c<channels;++c){float aligned=0;const float linear=sectorBands[n].process(c,delayed[(size_t)c],gains,aligned);
                float result=linear;
                if(p[phaseMode3]<.5f){auto&mem=crossoverMemory3[n][(size_t)c];float low=aligned,upper=aligned;
                    const int poles=2<<juce::jlimit(0,2,(int)p[slope3]);
                    for(int j=0;j<juce::jmin(8,poles);++j){const float a=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(p[crossLow3],float(rate*.4))/float(rate));
                        const float b=1-std::exp(-juce::MathConstants<float>::twoPi*juce::jmin(p[crossHigh3],float(rate*.49))/float(rate));
                        mem[0][(size_t)j]+=a*(low-mem[0][(size_t)j]);mem[1][(size_t)j]+=b*(upper-mem[1][(size_t)j]);low=mem[0][(size_t)j];upper=mem[1][(size_t)j];}
                    result=low*gains[0]+(upper-low)*gains[1]+(aligned-upper)*gains[2];}
                delayed[(size_t)c]=result*output;
            }sectorBands[n].advance();
            clock.phase+=1/(rate*seconds[n]);if(clock.phase>=1){clock.phase-=1;if((int)p[trigger3]==0)clock.cycle+=1;}++clock.age;
        }
        float processed[2]{};
        const int sideLag=juce::jlimit(0,delayCapacity3-1,(int)std::round(mainDelay+6*BandEngine::delay-
            (settings[1][lookahead3]+settings[1][offset3])*.001*rate));
        const size_t sideSlot=(size_t)((delayPosition3+delayCapacity3-sideLag)%delayCapacity3);
        const float sideL=delay3[2][0][sideSlot],sideR=delay3[2][1][sideSlot];
        const float side=std::abs(sideL)>=std::abs(sideR)?sideL:sideR;
        const float sideStereo[]{sideL,sideR};
        spectral.process(delayed.data(),side,processed,channels,spectralDepth,value("spectralLow"),juce::jmin(value("spectralHigh"),float(rate*.49)),rate,nullptr,true,&spectralGains,settings[1][crossLow3],settings[1][crossHigh3],sideStereo);
        mixSmooth=mix+(mixSmooth-mix)*slew;bypassSmooth=(value("bypass")>.5f?0.f:1.f)+(bypassSmooth-(value("bypass")>.5f?0.f:1.f))*slew;
        float outputPeak=0;std::array<float,2> output{};
        for(int c=0;c<channels;++c){float y=dry[(size_t)c]+(processed[c]-dry[(size_t)c])*mixSmooth*bypassSmooth;if(listening)y=detectorListen;
            if(!std::isfinite(y))y=0;output[(size_t)c]=y;main.setSample(c,i,y);outPeak[(size_t)c]=juce::jmax(outPeak[(size_t)c],std::abs(y));outputPeak=juce::jmax(outputPeak,std::abs(y));
            dryEnergy+=double(dry[(size_t)c])*dry[(size_t)c];outputEnergy+=double(y)*y;}
        peakOut=juce::jmax(peakOut,outputPeak);
        if(sampleClock3>=(uint64_t)latency) {
            const float m=std::abs(dry[0])>=std::abs(dry[1])?dry[0]:dry[1];
            const float s=std::abs(scAligned[0])>=std::abs(scAligned[1])?scAligned[0]:scAligned[1];
            const float o=std::abs(output[0])>=std::abs(output[1])?output[0]:output[1];
            const double sourcePpq=inputHostAligned?ppq+(i-latency)*displayBeatStep:inputScopeFreePpq;
            const double curvePhase=clocks3[0].phase-double(latency+1)/(rate*seconds[0]);
            captureInputScope(m,s,o,sourcePpq,displayTempo,displayBeats,inputHostAligned,
                              sideBus.getNumChannels()>0,curvePhase,seconds[0]);
            inputScopeFreePpq+=displayBeatStep;
        }
        for(int k=0;k<6;++k){const auto n=(size_t)k;auto&f=writingScopes[n];const auto&p=settings[n];const auto&clock=clocks3[n];auto&scopeClock=scopeClocks3[n];
            auto publish=[&](){if(f.bins>0&&!f.complete){f.complete=true;f.generation=++scopeGeneration;const juce::SpinLock::ScopedTryLockType lock(scopeLock);if(lock.isLocked()&&!scopeFrozen.load())publishedScopes[n]=f;}};
            // Drain even while frozen, so Freeze affects only display publication.
            // Accepted incoming triggers cannot erase samples still travelling
            // through the output delay, nor alter the DSP's own trigger clock.
            while(scopeClock.count>0){const auto event=scopeClock.events[scopeClock.first];
                if(sampleClock3<event.sample+(uint64_t)latency)break;
                scopeClock.first=(scopeClock.first+1)%scopeClock.events.size();--scopeClock.count;
                if(event.noteOn){if(!scopeFrozen.load(std::memory_order_relaxed)&&f.bins>=juce::jmax(1,(int)std::ceil(p[end3]*512)-2))publish();
                    f=ScopeFrame{};scopeClock.active=true;scopeClock.held=event.held;scopeClock.noteOffPending=false;
                    scopeClock.triggerSample=event.sample;scopeClock.periodSeconds=event.periodSeconds;}
                else{scopeClock.held=false;scopeClock.noteOffPending=true;scopeClock.noteOffSample=event.sample;}}
            const bool frozenScope=scopeFrozen.load(std::memory_order_relaxed);
            if(scopeClock.frozen!=frozenScope){
                // A partial frame must not splice samples from opposite sides
                // of a pause. Resume at the next real cycle/accepted hit.
                f=ScopeFrame{};scopeClock.frozen=frozenScope;}
            if(frozenScope)continue;
            double at=clock.phase-double(latency)/(rate*seconds[n]);bool valid=sampleClock3>=(uint64_t)latency;
            if((int)p[trigger3]!=0){const auto age=sampleClock3>=scopeClock.triggerSample?sampleClock3-scopeClock.triggerSample:0;valid=scopeClock.active&&age>=(uint64_t)latency;at=(double(age)-latency)/(rate*scopeClock.periodSeconds);
                const bool heldAtScope=scopeClock.held||(scopeClock.noteOffPending&&sampleClock3<scopeClock.noteOffSample+(uint64_t)latency);
                const bool loops=(int)p[loop3]==1||((int)p[loop3]==2&&heldAtScope);
                if(!loops&&at>=p[end3]){publish();valid=false;}}
            at-=std::floor(at);const int bin=juce::jlimit(0,511,(int)(at*512));
            if(f.bins==0&&bin>1)valid=false;
            if(valid){if(bin<f.bins-1){f.complete=true;f.generation=++scopeGeneration;{const juce::SpinLock::ScopedTryLockType lock(scopeLock);if(lock.isLocked()&&!scopeFrozen.load())publishedScopes[n]=f;}f=ScopeFrame{};}
                f.periodSeconds=(int)p[trigger3]==0?seconds[n]:scopeClock.periodSeconds;f.phase=float(at);f.triggerSample=(int)p[trigger3]==0?clock.triggerSample:scopeClock.triggerSample;f.sidechain=sideBus.getNumChannels()>0;playheads3[n].store(float(at),std::memory_order_relaxed);
                const float m=std::abs(dry[0])>=std::abs(dry[1])?dry[0]:dry[1];const float s=std::abs(scAligned[0])>=std::abs(scAligned[1])?scAligned[0]:scAligned[1];
                const float o=std::abs(output[0])>=std::abs(output[1])?output[0]:output[1];
                f.mainMin[(size_t)bin]=juce::jmin(f.mainMin[(size_t)bin],m);f.mainMax[(size_t)bin]=juce::jmax(f.mainMax[(size_t)bin],m);
                f.sideMin[(size_t)bin]=juce::jmin(f.sideMin[(size_t)bin],s);f.sideMax[(size_t)bin]=juce::jmax(f.sideMax[(size_t)bin],s);
                f.outMin[(size_t)bin]=juce::jmin(f.outMin[(size_t)bin],o);f.outMax[(size_t)bin]=juce::jmax(f.outMax[(size_t)bin],o);
                const float mainPeak=juce::jmax(std::abs(f.mainMin[(size_t)bin]),std::abs(f.mainMax[(size_t)bin]));
                const float resultPeak=juce::jmax(std::abs(f.outMin[(size_t)bin]),std::abs(f.outMax[(size_t)bin]));
                f.gain[(size_t)bin]=mainPeak>1e-7f?odx::clamp(resultPeak/mainPeak):1;
                f.bins=juce::jmax(f.bins,bin+1);}
        }
        delayPosition3=(delayPosition3+1)%delayCapacity3;
    }
    if(!scopeFrozen.load()) {const juce::SpinLock::ScopedTryLockType lock(scopeLock);if(lock.isLocked()&&!scopeFrozen.load())for(int k=0;k<6;++k)if(!publishedScopes[(size_t)k].complete)publishedScopes[(size_t)k]=writingScopes[(size_t)k];}
    publishInputScope();
    inMeter=peakIn;outMeter=peakOut;reduction=dryEnergy>1e-16?odx::clamp(1-float(std::sqrt(outputEnergy/dryEnergy))):0;
    inputPeaks[0]=inPeak[0];inputPeaks[1]=inPeak[1];outputPeaks[0]=outPeak[0];outputPeaks[1]=outPeak[1];sidechainPresent=sideBus.getNumChannels()>0;
    for(int k=0;k<6;++k)engineActivity[(size_t)k]=activity[(size_t)k];
}
void OpenDuckProcessor::processModernBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    const unsigned modern = modernMask.load(std::memory_order_relaxed);
    // The audio callback never waits for the editor/state thread. A busy writer leaves the last
    // valid snapshot active until the next block. Arrays have fixed capacity; no RT allocation.
    {
        const juce::SpinLock::ScopedTryLockType lock(curveLock);
        if (lock.isLocked())
            audioCurves = curves;
    }
    auto main = getBusBuffer(buffer, false, 0), sc = getBusBuffer(buffer, true, 1);
    const int channels = main.getNumChannels(), trigger = (int)value("trigger");
    const bool hasSc = sc.getNumChannels() > 0, synced = value("sync") > .5f;
    double tempo = value("freeBpm"), ppq = 0;
    bool playing = true, positioned = false;
    if (synced)
        if (auto *ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm())
                    tempo = *b;
                playing = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition())
                {
                    ppq = *q;
                    positioned = true;
                }
            }
    bpm = (float)tempo;
    const double divisions[] = {1, .5, .25, 1.5, .75, 1.0 / 3, 1.0 / 6};
    const double legacyBeats = divisions[juce::jlimit(0, 6, (int)value("grid"))];
    double legacyCycle = positioned ? std::floor(ppq / legacyBeats) : freeCycle;
    if (trigger == 0 && positioned)
        phase = ppq / legacyBeats - legacyCycle;
    std::array<CurveSnapshot, 6> snapshots;
    std::array<double, 6> beats{}, cycles{};
    std::array<float, 6> attacks{}, releases{};
    const float legacyAmount = value("amount") / 100, legacyTension = value("tension"),
                legacyShift = value("shift") / 100;
    std::array<float, 8> legacyPoints{};
    for (int k = 0; k < 8; ++k)
        legacyPoints[(size_t)k] = pointRefs[(size_t)k]->load(std::memory_order_relaxed);
    const int legacyShape = (int)value("shape");
    for (int k = 0; k < 6; ++k)
    {
        const auto index = (size_t)k;
        snapshots[index] = audioCurves[index];
        readCurveParameters(k, snapshots[index]);
        auto &s = snapshots[index];
        if ((modern & (1u << (unsigned)k)) == 0)
        {
            s.legacy = true;
            s.shape = legacyShape;
            s.custom = legacyShape == 5;
            s.amount = legacyAmount * 100;
            s.tension = legacyTension;
            s.phase = legacyShift * 100;
            s.length = 1;
            s.attack = value("attack");
            s.release = value("release");
            s.grid = (int)value("grid");
            s.band = false;
            s.count = 8;
            for (int p = 0; p < 8; ++p)
                s.points[(size_t)p] = {(float)p / 7, legacyPoints[(size_t)p]};
        }
        beats[index] = divisions[juce::jlimit(0, 6, s.grid)];
        cycles[index] = positioned ? std::floor(ppq / beats[index]) : curveCycles[index];
        if (trigger == 0 && positioned)
            curvePhases[index] = ppq / beats[index] - cycles[index];
        attacks[index] = std::exp(-1.0f / (s.attack * .001f * (float)rate));
        releases[index] = std::exp(-1.0f / (s.release * .001f * (float)rate));
        focusSmoothers[index].setTargetValue(s.focus);
    }
    const float mix = value("mix") / 100, swing = value("swing") / 100;
    const float split = .5f + swing * .5f;
    const float threshold = juce::Decibels::decibelsToGain(value("threshold"));
    const float hysteresis = juce::Decibels::decibelsToGain(-value("hysteresis"));
    const int holdSamples = (int)(value("hold") * .001 * rate);
    const float detectorAttack = std::exp(-1.0f / (value("detectorAttack") * .001f * (float)rate));
    const float detectorRelease = std::exp(-1.0f / (value("detectorRelease") * .001f * (float)rate));
    const float slew = std::exp(-1.0f / (.005f * (float)rate));
    const bool bypass = value("bypass") > .5f;
    std::array<bool, 6> on{}, solo{};
    bool anySolo = false;
    for (int k = 0; k < 6; ++k)
    {
        on[(size_t)k] = onRefs[(size_t)k]->load() > .5f;
        solo[(size_t)k] = soloRefs[(size_t)k]->load() > .5f;
        anySolo |= solo[(size_t)k];
    }
    auto enabled = [&](int k) { return anySolo ? solo[(size_t)k] : on[(size_t)k]; };
    std::array<bool, 16> steps{};
    for (int k = 0; k < 16; ++k)
        steps[(size_t)k] = stepRefs[(size_t)k]->load() > .5f;
    const float specLow = value("spectralLow"),
                specHigh = juce::jmin(value("spectralHigh"), (float)rate * .49f);
    const float specDepth = value("spectralDepth"), grooveDepth = value("grooveDepth");
    const std::array<float, 3> depths{{value("bandLow"), value("bandMid"), value("bandHigh")}};
    std::array<float, 3> bandAttack{}, bandRelease{};
    // Literal lookup keys avoid constructing parameter names in the real-time callback.
    const char *ba[] = {"bandAttack0", "bandAttack1", "bandAttack2"};
    const char *br[] = {"bandRelease0", "bandRelease1", "bandRelease2"};
    for (int k = 0; k < 3; ++k)
    {
        bandAttack[(size_t)k] = std::exp(-1.0f / (value(ba[k]) * .001f * (float)rate));
        bandRelease[(size_t)k] = std::exp(-1.0f / (value(br[k]) * .001f * (float)rate));
    }
    bands.setCutoffs(value("crossLow"), juce::jmin(value("crossHigh"), (float)rate * .45f));
    morphSmooth.setTargetValue(value("morph"));
    std::array<float, 6> activities{};
    std::array<float, 2> channelIn{}, channelOut{};
    float peakIn = 0, peakOut = 0, maxReduction = 0;
    auto event = midi.cbegin();
    auto warped = [&](float t)
    { return t < split ? .5f * t / split : .5f + .5f * (t - split) / (1 - split); };
    for (int i = 0; i < main.getNumSamples(); ++i)
    {
        while (event != midi.cend() && (*event).samplePosition <= i)
        {
            if (trigger == 2 && (*event).getMessage().isNoteOn())
            {
                phase = 0;
                curvePhases.fill(0);
            }
            ++event;
        }
        float side = 0;
        for (int c = 0; c < sc.getNumChannels(); ++c)
            side += sc.getSample(c, i) / (float)sc.getNumChannels();
        if (std::abs(side) > std::abs(wavePeak))
            wavePeak = side;
        if (++waveCounter >= juce::jmax(1, (int)(rate / 256)))
        {
            waveform[waveWrite.fetch_add(1) % 512].store(wavePeak, std::memory_order_relaxed);
            waveCounter = 0;
            wavePeak = 0;
        }
        if (std::abs(side) > 1e-6f)
            sideSilence = 0;
        else
            sideSilence = juce::jmin(sideSilence + 1, (int)rate);
        float inputLevel = 0;
        for (int c = 0; c < channels; ++c)
            inputLevel = juce::jmax(inputLevel, std::abs(main.getSample(c, i)));
        peakIn = juce::jmax(peakIn, inputLevel);
        const bool externalActive = hasSc && sideSilence < (int)(rate * .5);
        const float detector = trigger == 1 ? std::abs(side) : externalActive ? std::abs(side) : inputLevel;
        follower =
            detector + (detector > follower ? detectorAttack : detectorRelease) * (follower - detector);
        if (!gate && follower > threshold)
        {
            if (trigger == 1)
            {
                phase = 0;
                curvePhases.fill(0);
            }
            gate = true;
            hold = holdSamples;
        }
        if (hold > 0)
            --hold;
        else if (follower < threshold * hysteresis)
            gate = false;
        const float morph = morphSmooth.getNextValue();
        std::array<float, 6> reductions{}, desired{};
        for (int k = 0; k < 6; ++k)
        {
            const auto index = (size_t)k;
            const auto &s = snapshots[index];
            const double ph = s.legacy ? phase : curvePhases[index];
            const float t = warped((float)ph);
            float envelope = sampleCurve(s, t);
            if ((trigger != 0 && ph >= .999998) || (trigger == 0 && !playing))
                envelope = 1;
            reductions[index] = (s.amount / 100) * (1 - envelope);
            desired[index] = reductions[index];
            if (k == 1)
                desired[index] *= specDepth;
            if (k == 2 && s.band)
                desired[index] *= juce::jmax(depths[0], juce::jmax(depths[1], depths[2]));
            if (k == 3)
            {
                float morphed = 0;
                if (s.legacy)
                {
                    const float lt = juce::jlimit(0.0f, 1.0f, t - legacyShift);
                    morphed = (1 - morph) * shape(lt, 0, legacyTension) +
                              morph * (legacyShape == 5 ? customShape(lt, legacyPoints, legacyTension)
                                                        : shape(lt, 1, legacyTension));
                }
                else
                {
                    auto soft = s;
                    soft.custom = false;
                    soft.shape = 0;
                    morphed = (1 - morph) * sampleCurve(soft, t) + morph * envelope;
                }
                desired[index] = (s.amount / 100) * (1 - morphed);
                if (trigger != 0 && ph >= .999998)
                    desired[index] = 0;
            }
            if (k == 4)
            {
                const auto cycle = s.legacy ? legacyCycle : cycles[index];
                const int step = ((int)cycle % 16 + 16) % 16;
                desired[index] = steps[(size_t)step] ? reductions[index] * grooveDepth : 0;
            }
            if (k == 5)
                desired[index] = s.legacy ? (gate ? legacyAmount : 0) : (gate ? reductions[index] : 0);
            if (trigger == 0 && !playing)
                desired[index] = 0;
        }
        float fullReduction = 0;
        for (int k = 0; k < 6; ++k)
        {
            const auto index = (size_t)k;
            const float target = enabled(k) ? desired[index] : 0;
            float &v = engineSmooth[index];
            v = target + (target > v ? attacks[index] : releases[index]) * (v - target);
            activities[index] = juce::jmax(activities[index], v);
            if ((k == 0 || k == 3 || k == 4 || k == 5) && !snapshots[index].band)
                fullReduction = juce::jmax(fullReduction, v);
        }
        std::array<float, 3> gains{};
        float minimumBand = 1;
        for (int k = 0; k < 3; ++k)
        {
            const float target = enabled(2) && !snapshots[2].band ? reductions[2] * depths[(size_t)k] : 0;
            float &bd = bandSmooth[(size_t)k];
            bd = target + (target > bd ? bandAttack[(size_t)k] : bandRelease[(size_t)k]) * (bd - target);
            gains[(size_t)k] = 1 - juce::jmax(fullReduction, bd);
            minimumBand = juce::jmin(minimumBand, gains[(size_t)k]);
        }
        const float spectralReduction = snapshots[1].band ? 0 : engineSmooth[1];
        const float spectralDepth =
            1 - juce::jlimit(0.0f, 1.0f, (1 - spectralReduction) / juce::jmax(.05f, minimumBand));
        std::array<SpectralEngine::FocusMask, 6> masks{};
        float maximumExtra = spectralDepth;
        bool anyFocused = false;
        for (int k = 0; k < 6; ++k)
        {
            const auto index = (size_t)k;
            const float cutoff = focusSmoothers[index].getNextValue();
            if (snapshots[index].band)
            {
                const float extra =
                    1 - juce::jlimit(0.0f, 1.0f, (1 - engineSmooth[index]) / juce::jmax(.05f, minimumBand));
                masks[index] = {extra, cutoff, k == 1};
                maximumExtra = juce::jmax(maximumExtra, extra);
                anyFocused |= extra > 0;
            }
        }
        float input[2]{}, filtered[2]{}, spec[2]{}, dry[2]{};
        for (int c = 0; c < channels; ++c)
        {
            input[c] = main.getSample(c, i);
            channelIn[(size_t)c] = juce::jmax(channelIn[(size_t)c], std::abs(input[c]));
            dry[c] = dryDelay[(size_t)c][(size_t)delayPos];
            dryDelay[(size_t)c][(size_t)delayPos] = input[c];
            float aligned = 0;
            filtered[c] = bands.process(c, input[c], gains, aligned);
        }
        bands.advance();
        spectral.process(filtered, side, spec, channels, spectralDepth, specLow, specHigh, rate,
                         anyFocused ? &masks : nullptr);
        mixSmooth = mix + (mixSmooth - mix) * slew;
        bypassSmooth = (bypass ? 0.0f : 1.0f) + (bypassSmooth - (bypass ? 0.0f : 1.0f)) * slew;
        for (int c = 0; c < channels; ++c)
        {
            float y = dry[c] + (spec[c] - dry[c]) * mixSmooth * bypassSmooth;
            if (!std::isfinite(y))
                y = 0;
            main.setSample(c, i, y);
            peakOut = juce::jmax(peakOut, std::abs(y));
            channelOut[(size_t)c] = juce::jmax(channelOut[(size_t)c], std::abs(y));
        }
        delayPos = (delayPos + 1) % 1152;
        maxReduction = juce::jmax(maxReduction, 1 - minimumBand * (1 - maximumExtra));
        if (trigger == 0)
        {
            if (playing)
            {
                phase += tempo / (60 * rate * legacyBeats);
                if (phase >= 1)
                {
                    phase -= 1;
                    legacyCycle += 1;
                }
                for (int k = 0; k < 6; ++k)
                {
                    auto index = (size_t)k;
                    curvePhases[index] += tempo / (60 * rate * beats[index]);
                    if (curvePhases[index] >= 1)
                    {
                        curvePhases[index] -= 1;
                        cycles[index] += 1;
                    }
                }
            }
        }
        else
        {
            phase = juce::jmin(.999999, phase + 1 / (rate * value("release") * .001));
            for (int k = 0; k < 6; ++k)
            {
                auto index = (size_t)k;
                curvePhases[index] =
                    juce::jmin(.999999, curvePhases[index] + 1 / (rate * snapshots[index].release * .001));
            }
        }
    }
    freeCycle = legacyCycle;
    curveCycles = cycles;
    sidechainPresent = hasSc && sideSilence < (int)(rate * .5);
    inMeter = peakIn;
    outMeter = peakOut;
    reduction = maxReduction;
    for (int c = 0; c < 2; ++c)
    {
        inputPeaks[(size_t)c] = channelIn[(size_t)c];
        outputPeaks[(size_t)c] = channelOut[(size_t)c];
    }
    for (int k = 0; k < 6; ++k)
        engineActivity[(size_t)k] = activities[(size_t)k];
}
juce::AudioProcessorEditor *OpenDuckProcessor::createEditor()
{
    if (value("template4") > .5f) return new SingleDuckEditor(*this);
    return new OpenDuckEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new OpenDuckProcessor();
}

