#include "LocalAssist.h"
#include "NeuralModels.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <new>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace odx::ai
{
namespace
{
constexpr double pi = 3.14159265358979323846;
float clamp(float x) { return std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : 0; }
float norm(float x, float lo, float hi) { return clamp((x-lo)/(hi-lo)); }
float denorm(float x, float lo, float hi) { return lo + clamp(x)*(hi-lo); }
float audio(const float *p, int i) { return p && std::isfinite(p[i]) ? p[i] : 0; }
float mono(const float *a, const float *b, int i) { return .5f*(audio(a,i)+audio(b ? b:a,i)); }
std::string id(const char *base, int mode) { return std::string("v3")+base+std::to_string(mode); }
void put(Candidate &c, const std::string &key, float v)
{
    for(int i=0;i<c.controlCount;++i) if(c.controls[(size_t)i].id==key) { c.controls[(size_t)i].value=v; return; }
    if(c.controlCount<(int)c.controls.size()) c.controls[(size_t)c.controlCount++]={key,v};
}
float get(const Candidate &c, const std::string &key, float fallback)
{
    for(int i=0;i<c.controlCount;++i) if(c.controls[(size_t)i].id==key) return c.controls[(size_t)i].value;
    return fallback;
}
bool modelsReady() { return neural::trained && std::strcmp(neural::version,"OpenDuck-native-teacher-0.3.0-r2")==0; }
struct Filters
{
    std::array<double,3> z{}, a{};
    explicit Filters(double rate) { const double hz[]={120,500,2500}; for(int b=0;b<3;++b)a[(size_t)b]=1-std::exp(-2*pi*hz[b]/rate); }
    std::array<double,4> next(double x) { for(int b=0;b<3;++b)z[(size_t)b]+=a[(size_t)b]*(x-z[(size_t)b]); return {z[0],z[1]-z[0],z[2]-z[1],x-z[2]}; }
};
}

const char *stateName(State s) noexcept
{
    switch(s) {case State::idle:return "Idle";case State::capturing:return "Capturing";case State::analyzing:return "Analyzing";
    case State::ready:return "Ready";case State::insufficient:return "Insufficient audio";case State::cancelled:return "Cancelled";
    case State::stale:return "Stale proposal";default:return "Error";}
}

Observation analyseAverageReference(const CaptureView &v, int mode, const Context &context)
{
    Observation o;
    if(v.samples<=0 || v.sampleRate<8000 || !v.mainL) return o;
    std::array<double,2> sum{}, peak{};
    std::array<std::array<double,4>,2> bands{};
    std::array<std::array<double,16>,2> phase{};
    std::array<int,16> bins{};
    Filters fx(v.sampleRate), fk(v.sampleRate);
    std::vector<int> hits;
    double env=0, triggerPeak=0; int last=-100000000;bool armed=true;
    const bool inputTrigger=context.trigger==3;
    for(int i=0;i<v.samples;++i)triggerPeak=std::max(triggerPeak,(double)std::abs(mono(inputTrigger?v.mainL:v.sideL,inputTrigger?v.mainR:v.sideR,i)));
    const double decay=std::exp(-1/(.015*v.sampleRate));
    const double period=v.sampleRate*60/std::clamp(v.bpm,30.0,300.0)*(mode==4?16:1);
    for(int i=0;i<v.samples;++i)
    {
        const double x=mono(v.mainL,v.mainR,i), k=mono(v.sideL,v.sideR,i);
        sum[0]+=x*x;sum[1]+=k*k;peak[0]=std::max(peak[0],std::abs(x));peak[1]=std::max(peak[1],std::abs(k));
        const auto bx=fx.next(x),bk=fk.next(k);
        for(int b=0;b<4;++b){bands[0][(size_t)b]+=bx[(size_t)b]*bx[(size_t)b];bands[1][(size_t)b]+=bk[(size_t)b]*bk[(size_t)b];}
        const double trigger=inputTrigger?x:k;
        env=std::max(std::abs(trigger),env*decay);
        if(env<triggerPeak*.1)armed=true;
        if(armed && triggerPeak>.001 && std::abs(trigger)>triggerPeak*.25 && i-last>(int)(v.sampleRate*.12)) {hits.push_back(i);last=i;armed=false;}
        const double origin=context.trigger==0?-context.startPpq*v.sampleRate*60/std::clamp(v.bpm,30.0,300.0):(hits.empty()?0:hits.front());
        double position=std::fmod(i-origin,period);if(position<0)position+=period;
        const int bin=std::clamp((int)(position/period*16),0,15);
        phase[0][(size_t)bin]+=x*x;phase[1][(size_t)bin]+=k*k;++bins[(size_t)bin];
    }
    o.mainRms=std::sqrt(sum[0]/v.samples);o.sideRms=std::sqrt(sum[1]/v.samples);
    o.mainPeak=peak[0];o.sidePeak=peak[1];
    o.mainPresent=o.mainRms>3.16227766e-4 && peak[0]>.001;
    o.sidechainPresent=o.sideRms>3.16227766e-4 && peak[1]>.001;
    o.validHits=(int)hits.size();
    auto &f=o.features;
    f[0]=norm((float)(20*std::log10(o.mainRms+1e-12)),-80,0);f[1]=norm((float)(20*std::log10(o.sideRms+1e-12)),-80,0);
    f[2]=norm((float)peak[0],0,1);f[3]=norm((float)peak[1],0,1);
    f[4]=norm((float)(peak[0]/(o.mainRms+1e-9)),1,12);f[5]=norm((float)(peak[1]/(o.sideRms+1e-9)),1,12);
    f[6]=norm((float)v.bpm,40,220);f[7]=clamp(context.mix/100);
    f[8]=norm((float)(hits.size()*v.sampleRate/v.samples),0,12);f[9]=norm((float)context.baseline.curve.release,5,2000);
    for(int b=0;b<4;++b){f[(size_t)(10+b)]=clamp((float)(bands[0][(size_t)b]/(sum[0]+1e-12)));f[(size_t)(14+b)]=clamp((float)(bands[1][(size_t)b]/(sum[1]+1e-12)));}
    for(int b=0;b<16;++b){f[(size_t)(18+b)]=clamp((float)(std::sqrt(phase[0][(size_t)b]/std::max(1,bins[(size_t)b]))/(peak[0]+1e-9)));
        f[(size_t)(34+b)]=clamp((float)(std::sqrt(phase[1][(size_t)b]/std::max(1,bins[(size_t)b]))/(peak[1]+1e-9)));}
    // Native FFT features: averaged actual input/SC spectral energy in 16 log bands.
    juce::dsp::FFT fft(10);
    std::array<float,2048> work{};
    std::array<std::array<double,16>,2> spectrum{};
    for(int source=0;source<2;++source)
        for(int start=0;start+1024<=v.samples;start+=2048)
        {
            work.fill(0);
            for(int n=0;n<1024;++n) work[(size_t)n]=mono(source?v.sideL:v.mainL,source?v.sideR:v.mainR,start+n)*(float)(.5-.5*std::cos(2*pi*n/1023));
            fft.performFrequencyOnlyForwardTransform(work.data());
            for(int b=1;b<512;++b){const double hz=b*v.sampleRate/1024;const int bin=std::clamp((int)(std::log2(std::max(20.0,hz)/20)/std::log2(20000.0/20)*16),0,15);spectrum[(size_t)source][(size_t)bin]+=work[(size_t)b]*work[(size_t)b];}
        }
    for(int source=0;source<2;++source){double total=1e-12;for(double n:spectrum[(size_t)source])total+=n;if(source==0)o.mainSpectralEnergy=total;else o.sideSpectralEnergy=total;for(int b=0;b<16;++b)f[(size_t)(50+source*16+b)]=clamp((float)(spectrum[(size_t)source][(size_t)b]/total));}
    // Slot-specific context is measured/configured, not an invented quality score.
    const auto encoded=encode(context.baseline,mode);
    for(int b=0;b<14;++b) f[(size_t)(82+b)]=encoded[(size_t)b];
    f[95]=clamp(context.trigger/3.f);
    return o;
}

namespace
{
bool coincident(const float *left,const float *right,int samples)
{
    if(!right || left==right)return true;
    for(int i=0;i<samples;++i)if(audio(left,i)!=audio(right,i))return false;
    return true;
}
CaptureView channelView(const CaptureView &v,bool right)
{
    auto c=v;c.mainL=right && v.mainR?v.mainR:v.mainL;c.mainR=c.mainL;
    c.sideL=right && v.sideR?v.sideR:v.sideL;c.sideR=c.sideL;return c;
}
struct MetricAccum {double base=0,overlap=0,offX=0,offY=0,sx=0,sy=0,art=0;};
}
Observation analyse(const CaptureView &v,int mode,const Context &context)
{
    // Keep every operation in the original mono/dual-mono path unchanged.
    if(v.samples<=0 || (coincident(v.mainL,v.mainR,v.samples) && coincident(v.sideL,v.sideR,v.samples)))
        return analyseAverageReference(v,mode,context);
    const auto left=analyseAverageReference(channelView(v,false),mode,context);
    const auto right=analyseAverageReference(channelView(v,true),mode,context);
    auto result=left;result.analysedChannels=2;result.validHits=std::max(left.validHits,right.validHits);
    const double mainL=left.mainRms*left.mainRms,mainR=right.mainRms*right.mainRms;
    const double sideL=left.sideRms*left.sideRms,sideR=right.sideRms*right.sideRms;
    result.mainRms=std::sqrt(.5*(mainL+mainR));result.sideRms=std::sqrt(.5*(sideL+sideR));
    result.mainPeak=std::max(left.mainPeak,right.mainPeak);result.sidePeak=std::max(left.sidePeak,right.sidePeak);
    result.mainPresent=result.mainRms>3.16227766e-4 && result.mainPeak>.001;
    result.sidechainPresent=result.sideRms>3.16227766e-4 && result.sidePeak>.001;
    auto weighted=[](float a,float b,double ea,double eb){return clamp((float)((a*ea+b*eb)/(ea+eb+1e-30)));};
    auto &f=result.features;
    f[0]=norm((float)(20*std::log10(result.mainRms+1e-12)),-80,0);f[1]=norm((float)(20*std::log10(result.sideRms+1e-12)),-80,0);
    f[2]=norm((float)result.mainPeak,0,1);f[3]=norm((float)result.sidePeak,0,1);
    f[4]=norm((float)(result.mainPeak/(result.mainRms+1e-9)),1,12);f[5]=norm((float)(result.sidePeak/(result.sideRms+1e-9)),1,12);
    f[8]=norm((float)(result.validHits*v.sampleRate/v.samples),0,12);
    for(int b=0;b<4;++b){const auto x=(size_t)(10+b),k=(size_t)(14+b);f[x]=weighted(left.features[x],right.features[x],mainL,mainR);f[k]=weighted(left.features[k],right.features[k],sideL,sideR);}
    for(int b=0;b<16;++b)
    {
        const auto x=(size_t)(18+b),k=(size_t)(34+b);
        const double xl=left.features[x]*left.mainPeak,xr=right.features[x]*right.mainPeak;
        const double kl=left.features[k]*left.sidePeak,kr=right.features[k]*right.sidePeak;
        f[x]=clamp((float)(std::sqrt(.5*(xl*xl+xr*xr))/(result.mainPeak+1e-9)));
        f[k]=clamp((float)(std::sqrt(.5*(kl*kl+kr*kr))/(result.sidePeak+1e-9)));
        const auto sx=(size_t)(50+b),sk=(size_t)(66+b);
        f[sx]=weighted(left.features[sx],right.features[sx],left.mainSpectralEnergy,right.mainSpectralEnergy);
        f[sk]=weighted(left.features[sk],right.features[sk],left.sideSpectralEnergy,right.sideSpectralEnergy);
    }
    result.mainSpectralEnergy=.5*(left.mainSpectralEnergy+right.mainSpectralEnergy);
    result.sideSpectralEnergy=.5*(left.sideSpectralEnergy+right.sideSpectralEnergy);
    return result;
}

static Metrics measureAverageImpl(const CaptureView &v,const float *yl,const float *yr,int count,MetricAccum *accum)

{
    Metrics m; count=std::min(count,v.samples);
    if(count<=0 || !yl) return m;
    Filters fx(v.sampleRate),fk(v.sampleRate),fy(v.sampleRate);
    std::array<double,4> ex{},ek{},ey{};
    double base=0,overlap=0,offX=0,offY=0,sx=0,sy=0,art=0,lastY=0,lastX=0;
    double scPeak=0;for(int i=0;i<count;++i)scPeak=std::max(scPeak,(double)std::abs(mono(v.sideL,v.sideR,i)));
    const double a=1-std::exp(-1/(.01*v.sampleRate));
    double scEnv=0;
    for(int i=0;i<count;++i)
    {
        const double x=mono(v.mainL,v.mainR,i),k=mono(v.sideL,v.sideR,i),y=mono(yl,yr,i);
        if(!std::isfinite(yl[i]) || (yr && !std::isfinite(yr[i]))) return m;
        const auto bx=fx.next(x),bk=fk.next(k),by=fy.next(y);
        scEnv=std::max(std::abs(k),scEnv*std::exp(-1/(.045*v.sampleRate)));
        const double hit=std::clamp(scEnv/(scPeak*.25+1e-9),0.0,1.0);
        for(int b=0;b<4;++b){const auto z=(size_t)b;ex[z]+=a*(bx[z]*bx[z]-ex[z]);ek[z]+=a*(bk[z]*bk[z]-ek[z]);ey[z]+=a*(by[z]*by[z]-ey[z]);base+=ek[z]*ex[z];overlap+=ek[z]*ey[z];}
        offX+=(1-hit)*x*x;offY+=(1-hit)*y*y;sx+=x*x;sy+=y*y;
        const double ratio=std::clamp(std::sqrt((y*y+1e-10)/(x*x+1e-10)),0.0,1.0);
        const double residual=(y-lastY)-ratio*(x-lastX);art+=residual*residual;lastY=y;lastX=x;
    }
    m.overlap=overlap/(base+1e-15);m.preservation=std::max(0.0,(offX-offY)/(offX+1e-12));
    m.artifacts=art/(sx+1e-12);m.hitExtent=std::max(0.0,1-sy/(sx+1e-12));
    m.inputRms=std::sqrt(sx/count);m.outputRms=std::sqrt(sy/count);
    m.objective=m.overlap+1.6*m.preservation+.08*m.artifacts+.08*m.hitExtent;
    m.finite=std::isfinite(m.objective) && m.inputRms>1e-8;
    if(accum)*accum={base,overlap,offX,offY,sx,sy,art};return m;
}
Metrics measureAverageReference(const CaptureView &v,const float *yl,const float *yr,int count)
{
    return measureAverageImpl(v,yl,yr,count,nullptr);
}
Metrics measure(const CaptureView &v,const float *yl,const float *yr,int count)
{
    count=std::min(count,v.samples);
    if(count<=0 || !yl || (coincident(v.mainL,v.mainR,count) && coincident(v.sideL,v.sideR,count) && coincident(yl,yr,count)))
        return measureAverageReference(v,yl,yr,count);
    MetricAccum left{},right{};
    const auto a=measureAverageImpl(channelView(v,false),yl,yl,count,&left);
    const auto b=measureAverageImpl(channelView(v,true),yr?yr:yl,yr?yr:yl,count,&right);
    Metrics m;m.analysedChannels=2;
    const double base=left.base+right.base,overlap=left.overlap+right.overlap;
    const double offX=left.offX+right.offX,offY=left.offY+right.offY;
    const double sx=left.sx+right.sx,sy=left.sy+right.sy,art=left.art+right.art;
    m.overlap=overlap/(base+2e-15);m.preservation=std::max(0.0,(offX-offY)/(offX+2e-12));
    m.artifacts=art/(sx+2e-12);m.hitExtent=std::max(0.0,1-sy/(sx+2e-12));
    m.inputRms=std::sqrt(sx/(2*count));m.outputRms=std::sqrt(sy/(2*count));
    m.objective=m.overlap+1.6*m.preservation+.08*m.artifacts+.08*m.hitExtent;
    // A silent channel is valid stereo, but non-finite output on either side is not.
    bool finiteOutput=true;for(int i=0;i<count;++i)finiteOutput &= std::isfinite(yl[i]) && (!yr || std::isfinite(yr[i]));
    m.finite=finiteOutput && std::isfinite(m.objective) && m.inputRms>1e-8;
    (void)a;(void)b;return m;
}

Metrics measure(const CaptureView &v,const float *yl,const float *yr,int count,const Context &context,int mode)
{
    auto result=measure(v,yl,yr,count);count=std::min(count,v.samples);
    if(!result.finite || count<=0 || v.sampleRate<8000)return result;
    struct Frame {
        std::array<double,4> x{},y{},key{};
        double ex=0,ey=0,peakX=0,peakY=0,peakKey=0;
        int count=0;
    };
    const int frameSize=std::max(1,(int)std::llround(v.sampleRate*.001));
    std::vector<Frame> frames((size_t)((count+frameSize-1)/frameSize));
    std::array<Filters,2> xf{{Filters(v.sampleRate),Filters(v.sampleRate)}};
    std::array<Filters,2> yf{{Filters(v.sampleRate),Filters(v.sampleRate)}};
    std::array<Filters,2> kf{{Filters(v.sampleRate),Filters(v.sampleRate)}};
    double peakKey=0,totalX=0;std::array<double,4> totalBandX{},totalBandKey{};
    for(int i=0;i<count;++i){auto&f=frames[(size_t)(i/frameSize)];++f.count;
        for(int channel=0;channel<2;++channel){
            const float x=audio(channel && v.mainR?v.mainR:v.mainL,i),y=audio(channel && yr?yr:yl,i);
            const float key=audio(channel && v.sideR?v.sideR:v.sideL,i);
            const auto bx=xf[(size_t)channel].next(x),by=yf[(size_t)channel].next(y),bk=kf[(size_t)channel].next(key);
            f.ex+=.5*x*x;f.ey+=.5*y*y;f.peakX=std::max(f.peakX,(double)std::abs(x));f.peakY=std::max(f.peakY,(double)std::abs(y));
            f.peakKey=std::max(f.peakKey,(double)std::abs(key));
            for(int b=0;b<4;++b){f.x[(size_t)b]+=.5*bx[(size_t)b]*bx[(size_t)b];f.y[(size_t)b]+=.5*by[(size_t)b]*by[(size_t)b];f.key[(size_t)b]+=.5*bk[(size_t)b]*bk[(size_t)b];}
        }
        peakKey=std::max(peakKey,f.peakKey);
    }
    for(const auto&f:frames){totalX+=f.ex;for(int b=0;b<4;++b){totalBandX[(size_t)b]+=f.x[(size_t)b];totalBandKey[(size_t)b]+=f.key[(size_t)b];}}
    if(peakKey<=.001 || totalX<=1e-10)return result;
    auto frozen=[&](const char*base,float fallback){const auto name=id(base,mode);for(int i=0;i<context.frozenCount;++i)if(context.frozen[(size_t)i].id==name)return context.frozen[(size_t)i].value;return fallback;};
    // Timing belongs to the captured intent, never to candidate-controlled smoothing.
    const float anticipation=frozen("Lookahead",0)+frozen("Offset",0);
    const int pre=(int)std::ceil(16+std::max(0.f,anticipation));
    const int post=(int)std::ceil(16+std::max(0.f,-anticipation)+3*std::max(0.f,get(context.baseline,id("Smooth",mode),1)));
    std::vector<unsigned char> quiet(frames.size(),1);double envelope=0;
    auto allow=[&](int f,int before,int after){const int begin=std::max(0,f-before),end=std::min((int)frames.size(),f+after+1);for(int j=begin;j<end;++j)quiet[(size_t)j]=0;};
    for(size_t f=0;f<frames.size();++f){envelope=std::max(frames[f].peakKey,envelope*std::exp(-frames[f].count/(v.sampleRate*.020)));
        if(envelope>peakKey*.08)allow((int)f,pre,post);
        if(context.trigger==0 && context.hasIntentProfile && context.intentCycleSeconds>0){
            const double seconds=context.startPpq*60/context.bpm+f*frameSize/v.sampleRate;
            const double phase=seconds/context.intentCycleSeconds-std::floor(seconds/context.intentCycleSeconds);
            const double index=phase*255;const int a=std::clamp((int)index,0,254);const double gain=context.intentGain[(size_t)a]+(context.intentGain[(size_t)(a+1)]-context.intentGain[(size_t)a])*(index-a);
            if(gain<.995)allow((int)f,16,(int)std::ceil(16+3*std::max(0.f,get(context.baseline,id("Smooth",mode),1))));
        }}
    double protectedX=0,protectedY=0;int protectedSamples=0;
    for(size_t f=0;f<frames.size();++f)if(quiet[f]){protectedX+=frames[f].ex;protectedY+=frames[f].ey;protectedSamples+=frames[f].count;}
    auto ratioDb=[](double y,double x){return 10*std::log10(std::max(y,1e-30)/std::max(x,1e-30));};
    result.protectedFraction=protectedSamples/(double)count;
    result.protectedRmsDb=ratioDb(protectedY,protectedX);
    const double keyEnergy=std::accumulate(totalBandKey.begin(),totalBandKey.end(),0.0);
    for(int b=0;b<4;++b){const auto z=(size_t)b;double ex=0,ey=0;
        // Selective engines also protect bands with <0.5% of measured key energy.
        // Full-range shaping is intentionally allowed to duck every band during hits.
        const bool noConflict=(mode==1||mode==2)&&totalBandKey[z]<keyEnergy*.005;
        for(size_t f=0;f<frames.size();++f)if(quiet[f]||noConflict){ex+=frames[f].x[z];ey+=frames[f].y[z];}
        if(totalBandX[z]>totalX*.001 && ex>totalX*.00001){result.protectedBandMask|=1u<<(unsigned)b;result.protectedBandDb[z]=ratioDb(ey,ex);}
    }
    // Each independently measured quiet interval must recover, not just its mean.
    for(size_t start=0;start<frames.size();){if(!quiet[start]){++start;continue;}size_t end=start;double ex=0,ey=0;int samples=0;
        while(end<frames.size()&&quiet[end]){ex+=frames[end].ex;ey+=frames[end].ey;samples+=frames[end].count;++end;}
        if(samples>=v.sampleRate*.020 && ex>totalX*(samples/(double)count)*.01){++result.recoveryRegions;result.worstRecoveryDb=std::min(result.worstRecoveryDb,ratioDb(ey,ex));}start=end;
    }
    // Detect a main onset from 20-ms energy change, then protect only attacks
    // wholly outside the dilated key-event/timing windows. No fabricated onsets.
    int lastAttack=-1000;
    for(int f=20;f+20<(int)frames.size();++f){if(f-lastAttack<30)continue;
        double before=0,ex=0,ey=0,px=0,py=0;bool protectedAttack=true;
        for(int j=f-20;j<f;++j)before+=frames[(size_t)j].ex;
        for(int j=f;j<f+20;++j){const auto&x=frames[(size_t)j];ex+=x.ex;ey+=x.ey;px=std::max(px,x.peakX);py=std::max(py,x.peakY);protectedAttack &=quiet[(size_t)j]!=0;}
        if(protectedAttack && ex>before*2.5 && ex>totalX*.0001 && px>.001){
            ++result.protectedAttacks;lastAttack=f;result.worstAttackDb=std::min(result.worstAttackDb,std::min(ratioDb(ey,ex),20*std::log10(std::max(py,1e-15)/px)));}
    }
    result.preservationMeasured=result.protectedFraction>=.02 && protectedX>totalX*.001 && result.recoveryRegions>=2 && result.protectedBandMask!=0;
    return result;
}

CandidateAssessment assessCandidate(const Metrics &baseline,const Metrics &candidate)
{
    if(!baseline.finite || !candidate.finite || !std::isfinite(baseline.objective) || !std::isfinite(candidate.objective))return {false,"Candidate or baseline render is not finite"};
    if(!baseline.preservationMeasured || !candidate.preservationMeasured)return {false,"Insufficient measured quiet/recovery coverage; use a longer section with clear gaps"};
    if(!std::isfinite(baseline.protectedRmsDb)||!std::isfinite(baseline.worstRecoveryDb)||!std::isfinite(baseline.worstAttackDb))return {false,"Baseline preservation measurements are not finite"};
    auto floor=[](double baselineDb,double absoluteDb,double tolerance){return std::max(absoluteDb,std::min(0.0,baselineDb)-tolerance);};
    if(!std::isfinite(candidate.protectedRmsDb) || candidate.protectedRmsDb<floor(baseline.protectedRmsDb,-.5,.1))return {false,"Protected quiet audio loses level"};
    if(candidate.protectedBandMask!=baseline.protectedBandMask)return {false,"Protected band coverage changed"};
    for(int b=0;b<4;++b)if(candidate.protectedBandMask&(1u<<(unsigned)b))
        if(!std::isfinite(baseline.protectedBandDb[(size_t)b]) || !std::isfinite(candidate.protectedBandDb[(size_t)b]) || candidate.protectedBandDb[(size_t)b]<floor(baseline.protectedBandDb[(size_t)b],-.5,.15))return {false,"A protected frequency band loses level"};
    if(!std::isfinite(candidate.worstRecoveryDb) || candidate.worstRecoveryDb<floor(baseline.worstRecoveryDb,-.75,.15))return {false,"Audio does not recover between key events"};
    if(candidate.protectedAttacks!=baseline.protectedAttacks)return {false,"Protected attack coverage changed"};
    if(candidate.protectedAttacks>0 && (!std::isfinite(candidate.worstAttackDb) || candidate.worstAttackDb<floor(baseline.worstAttackDb,-1,.25)))return {false,"A main attack outside key events is attenuated"};
    return {true,"Measured quiet audio, bands, recovery and non-colliding attacks preserved"};
}

Outputs encode(const Candidate &c, int mode)
{
    Outputs o{};const auto &s=c.curve;
    o[0]=clamp(s.amount/100);o[1]=norm(s.phase,-25,25);o[2]=norm(s.length,.1f,1);o[3]=norm(s.tension,-80,80);
    o[4]=norm(s.attack,.1f,50);o[5]=norm(s.release,5,500);o[6]=norm(get(c,id("Smooth",mode),1),0,30);
    o[7]=norm(get(c,id("Character",mode),.5f),0,1);o[8]=norm(get(c,id("LowAmount",mode),100),0,100);
    o[9]=norm(get(c,id("MidAmount",mode),100),0,100);o[10]=norm(get(c,id("HighAmount",mode),100),0,100);
    if(mode==2){o[11]=norm(std::log(std::max(20.0f,get(c,id("CrossLow",mode),160))),std::log(20.f),std::log(2000.f));o[12]=norm(std::log(std::max(200.f,get(c,id("CrossHigh",mode),2500))),std::log(200.f),std::log(15000.f));}
    if(mode==4){o[8]=norm(get(c,id("Swing",mode),0),0,60);for(int n=0;n<16;++n)o[(size_t)(11+n)]=clamp(get(c,"step"+std::to_string(n),1));}
    if(mode==5){o[8]=norm(get(c,id("Threshold",mode),-24),-54,-6);o[9]=norm(get(c,id("Hysteresis",mode),3),1,12);o[10]=norm(get(c,id("Holdoff",mode),30),0,120);}
    o[(size_t)(controlOutputs+std::clamp(s.shape,0,15))]=1;return o;
}
Candidate decode(const Outputs &o, int mode, const Candidate &baseline)
{
    Candidate c=baseline;auto &s=c.curve;
    s.amount=denorm(o[0],0,100);s.phase=denorm(o[1],-25,25);s.length=denorm(o[2],.1f,1);s.tension=denorm(o[3],-80,80);
    s.attack=denorm(o[4],.1f,50);s.release=denorm(o[5],5,500);
    s.shape=(int)std::distance(o.begin()+controlOutputs,std::max_element(o.begin()+controlOutputs,o.end()));
    s.count=0;s.custom=false;s.edit=false;s.freeDraw=false;s.legacy=false;s.modern3=true;s.presetId=-1;
    put(c,id("Smooth",mode),denorm(o[6],0,30));put(c,id("Character",mode),clamp(o[7]));
    if(mode==2){put(c,id("Bands",mode),2);put(c,id("LowAmount",mode),clamp(o[8])*100);put(c,id("MidAmount",mode),clamp(o[9])*100);put(c,id("HighAmount",mode),clamp(o[10])*100);
        const float lo=std::exp(denorm(o[11],std::log(20.f),std::log(2000.f))),hi=std::exp(denorm(o[12],std::log(200.f),std::log(15000.f)));put(c,id("CrossLow",mode),lo);put(c,id("CrossHigh",mode),std::max(lo*1.5f,hi));}
    if(mode==4){put(c,id("Swing",mode),denorm(o[8],0,60));for(int n=0;n<16;++n)put(c,"step"+std::to_string(n),o[(size_t)(11+n)]>.5f?1.0f:0.0f);}
    if(mode==5){put(c,id("Threshold",mode),denorm(o[8],-54,-6));put(c,id("Hysteresis",mode),denorm(o[9],1,12));put(c,id("Holdoff",mode),denorm(o[10],0,120));}
    return c;
}

Outputs refinementCandidate(const Outputs &prediction,int attempt)
{
    auto values=prediction;attempt=std::clamp(attempt,0,7);
    if(attempt>0 && attempt<7){const int index=attempt-1;values[(size_t)index]=clamp(values[(size_t)index]+((attempt%2)?.08f:-.08f));}
    else if(attempt==7)
    {
        // The previous attempt7 duplicated amount+0.08. Retain every old unique
        // candidate and spend the eighth real render on the next model-ranked shape.
        const auto first=values.begin()+controlOutputs,selected=std::max_element(first,values.end());
        *selected=*std::min_element(first,values.end())-1.f;
    }
    return values;
}

struct Controller::Impl
{
    struct Slot { std::atomic<State> state{State::idle};std::atomic<float> progress{0};std::atomic<std::uint64_t> revision{0},proposalRevision{0};Status status;Proposal proposal;
        std::array<std::vector<float>,4> capture;Context cachedContext;bool hasCapture=false;int cachedSamples=0; };
    std::array<Slot,6> slots;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::array<std::vector<float>,4> audio;
    std::atomic<int> active{-1},written{0},target{0},pushing{0};
    std::atomic<bool> stop{false},cancelled{false};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<double> firstPpq{std::numeric_limits<double>::quiet_NaN()},captureBpm{0};
    std::atomic<bool> timingChanged{false};
    bool processing=false;
    bool strengthJob=false;
    float requestedStrength=1;
    double rate=48000;
    Context context;
    Evaluator evaluator;
    ~Impl(){stop=true;cancelled=true;wake.notify_all();if(worker.joinable())worker.join();}
    // This is a std::thread, not juce::Thread: no JUCE exception wrapper
    // surrounds it. Recover outside the unwound job lock, on this worker only.
    // Error cleanup never touches another slot or publishes an applicable target.
    void failedJob(int mode,const char* message) noexcept
    {
        try
        {
            std::unique_lock<std::mutex> lock(mutex);
            if(mode<0 || mode>=6){cancelled=true;stop=true;return;}
            if(active.load()!=mode)return;
            auto&slot=slots[(size_t)mode];
            while(pushing.load()!=0){lock.unlock();std::this_thread::yield();lock.lock();}
            slot.hasCapture=false;slot.cachedSamples=0;
            slot.proposal.improved=false;slot.proposal.targetImproved=false;
            for(auto&channel:slot.capture)channel.clear();
            for(auto&channel:audio)channel.clear();
            // Even a genuine allocation failure must not throw while reporting
            // itself. State::error remains available if the string cannot grow.
            try{slot.status.message.assign(message);}catch(...){slot.status.message.clear();}
            State finalState=State::error;
            if(cancelled.load())finalState=State::cancelled;
            else if(slot.revision.load()!=slot.status.contextRevision)finalState=State::stale;
            auto expected=State::analyzing;
            slot.state.compare_exchange_strong(expected,finalState,std::memory_order_release,std::memory_order_relaxed);
            active=-1;processing=false;strengthJob=false;
        }
        catch(...)
        {
            // A failed OS synchronization primitive is not recoverable as a new
            // job. Contain it at this thread boundary and make Apply impossible.
            cancelled=true;stop=true;
            if(mode>=0 && mode<6){auto expected=State::analyzing;
                slots[(size_t)mode].state.compare_exchange_strong(expected,State::error,std::memory_order_release,std::memory_order_relaxed);}
            active=-1;
        }
    }
    void run() noexcept
    {
        while(!stop.load())
        {
            int failedMode=-1;
            try
            {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock,std::chrono::milliseconds(10),[&]{return stop.load() || (active.load()>=0 && (strengthJob||written.load(std::memory_order_acquire)>=target.load()));});
            if(stop.load())return;
            const int mode=active.load();if(mode<0)continue;failedMode=mode;
            auto &slot=slots[(size_t)mode];
            if(cancelled.load()){if(pushing.load()!=0)continue;slot.state=State::cancelled;active=-1;processing=false;continue;}
            if(strengthJob){
                processing=true;slot.state=State::analyzing;slot.progress=.1f;
                const auto ctx=slot.cachedContext;const auto gen=generation.load();const auto eval=evaluator;const float strength=requestedStrength;
                auto result=slot.proposal;result.evaluatedCandidate=Controller::blend(result.before,result.after,strength);result.evaluatedStrength=strength;result.improved=false;
                const CaptureView view{slot.capture[0].data(),slot.capture[1].data(),slot.capture[2].data(),slot.capture[3].data(),slot.cachedSamples,ctx.sampleRate,ctx.bpm};
                lock.unlock();Metrics metric;const bool valid=eval&&eval(mode,result.evaluatedCandidate,view,ctx,metric,cancelled);
                const auto assessment=assessCandidate(result.baseline,metric);
                result.result=metric;result.improved=valid&&assessment.admissible&&metric.objective<result.baseline.objective-1e-6;
                std::string message=result.improved?"Selected strength measured and preservation verified":valid?(assessment.admissible?"No measured improvement at selected strength; full target retained":"Selected strength not applicable: "+assessment.reason):"Real DSP strength verification failed";
                lock.lock();State finalState=valid?State::ready:State::error;
                if(cancelled.load()||gen!=generation.load())finalState=State::cancelled;else if(slot.revision.load()!=ctx.revision)finalState=State::stale;
                slot.proposal=result;slot.status.message=message;if(finalState==State::ready)slot.progress=1;
                auto expected=State::analyzing;slot.state.compare_exchange_strong(expected,finalState,std::memory_order_release,std::memory_order_relaxed);
                active=-1;processing=false;strengthJob=false;continue;
            }
            if(written.load(std::memory_order_acquire)<target.load())continue;
            processing=true;slot.state=State::analyzing;slot.progress=.05f;
            auto ctx=context;if(std::isfinite(firstPpq.load())){ctx.startPpq=firstPpq.load();ctx.hostPositionAvailable=true;}if(captureBpm.load()>0)ctx.bpm=captureBpm.load();const auto gen=generation.load();const auto eval=evaluator;
            CaptureView view{audio[0].data(),audio[1].data(),audio[2].data(),audio[3].data(),target.load(),rate,ctx.bpm};
            lock.unlock();
            const auto observation=analyse(view,mode,ctx);
            State finalState=State::ready;std::string message="Proposal measured on captured audio";
            Proposal result;result.mode=mode;result.generation=gen;result.contextRevision=ctx.revision;result.before=ctx.baseline;result.modelVersion=neural::version;
            bool freeHz=false;
            for(int k=0;k<ctx.frozenCount;++k)if(ctx.frozen[(size_t)k].id==id("SpeedMode",mode))freeHz=ctx.frozen[(size_t)k].value>.5f;
            if(!observation.mainPresent || (!observation.sidechainPresent && ctx.trigger!=3) || observation.validHits<2)
            {finalState=State::insufficient;message="Capture needs main audio and at least two usable sidechain hits";}
            else if(timingChanged.load()){finalState=State::insufficient;message="Tempo or timeline changed during capture; record a stable section";}
            else if(ctx.trigger==2){finalState=State::insufficient;message="MIDI-trigger analysis requires recorded MIDI; no substitute events are invented";}
            else if(ctx.trigger==0 && (freeHz || !ctx.synced || !ctx.hostPositionAvailable)){finalState=State::insufficient;message="Repeat analysis needs captured host timing; free-running phase is not assumed";}
            else if(!eval || !modelsReady()){finalState=State::error;message="Matching r2 trained models or real DSP evaluator unavailable";}
            else
            {
                slot.progress=.2f;
                bool valid=eval(mode,ctx.baseline,view,ctx,result.baseline,cancelled);
                Candidate best=ctx.baseline;Metrics bestMetrics=result.baseline;
                auto prediction=neural::infer(mode,observation.features);
                bool eligibleCoverage=result.baseline.preservationMeasured;std::string rejection;
                for(int attempt=0;valid && eligibleCoverage && attempt<8 && !cancelled.load();++attempt)
                {
                    const auto values=refinementCandidate(prediction,attempt);
                    auto candidate=decode(values,mode,ctx.baseline);Metrics metric;
                    if(eval(mode,candidate,view,ctx,metric,cancelled) && metric.finite){const auto assessment=assessCandidate(result.baseline,metric);if(assessment.admissible && metric.objective<bestMetrics.objective){best=candidate;bestMetrics=metric;}else if(!assessment.admissible)rejection=assessment.reason;}
                    slot.progress=.25f+.7f*(attempt+1)/8;
                }
                if(!valid){finalState=State::error;message="Real DSP evaluation failed";}
                result.after=best;result.result=bestMetrics;result.improved=bestMetrics.finite && result.baseline.finite && bestMetrics.objective<result.baseline.objective-1e-6;
                if(!result.improved){result.after=ctx.baseline;result.result=result.baseline;}
                result.targetImproved=result.improved;result.evaluatedCandidate=result.after;result.evaluatedStrength=1;
                if(!eligibleCoverage){finalState=State::insufficient;message="Insufficient measured quiet/recovery coverage; use a section with clear gaps";}
                else if(!result.improved)message=rejection.empty()?"No measured improvement; baseline retained":"Baseline retained: "+rejection;
            }
            lock.lock();
            if(cancelled.load() || gen!=generation.load())finalState=State::cancelled;
            else if(slot.revision.load()!=ctx.revision)finalState=State::stale;
            slot.status.capturedBpm=captureBpm.load();slot.status.validHits=observation.validHits;slot.status.mainPresent=observation.mainPresent;slot.status.sidechainPresent=observation.sidechainPresent;slot.status.message=message;
            // The audio callback published all sample writes before written. Wait
            // only on this worker until even its pointer-owning guard is released.
            while(pushing.load()!=0){lock.unlock();std::this_thread::yield();lock.lock();}
            if(cancelled.load()||gen!=generation.load())finalState=State::cancelled;
            else if(slot.revision.load()!=ctx.revision)finalState=State::stale;
            slot.hasCapture=finalState==State::ready&&result.targetImproved;
            if(slot.hasCapture){slot.capture=std::move(audio);slot.cachedContext=ctx;slot.cachedSamples=view.samples;}
            else{for(auto&channel:slot.capture)channel.clear();slot.cachedSamples=0;}
            slot.proposal=result;slot.proposalRevision=result.contextRevision;
            if(finalState!=State::cancelled && finalState!=State::error)slot.progress=1;
            // Cancellation may publish Cancelled after the checks above. It wins
            // both before (CAS fails) and after (its store wins) final publication.
            auto expected=State::analyzing;slot.state.compare_exchange_strong(expected,finalState,std::memory_order_release,std::memory_order_relaxed);
            active=-1;processing=false;
            }
            catch(const std::bad_alloc&){failedJob(failedMode,"Insufficient memory for local analysis; no proposal was applied");}
            catch(const std::exception&){failedJob(failedMode,"Local analysis failed; no proposal was applied");}
            catch(...){failedJob(failedMode,"Unexpected local analysis failure; no proposal was applied");}
        }
    }
};
Controller::Controller():impl(std::make_unique<Impl>()){}
Controller::~Controller()=default;
void Controller::prepare(double sampleRate){cancel();std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->processing && impl->active.load()<0)impl->rate=sampleRate;}
void Controller::setEvaluator(Evaluator e){std::lock_guard<std::mutex> lock(impl->mutex);impl->evaluator=std::move(e);}
bool Controller::available() const noexcept{return modelsReady();}
bool Controller::start(int mode,int seconds,const Context &context)
{
    if(mode<0 || mode>=6 || (seconds!=5 && seconds!=10) || !std::isfinite(context.sampleRate) || context.sampleRate<8000 || context.sampleRate>192000)return false;
    std::lock_guard<std::mutex> lock(impl->mutex);if(impl->active.load()>=0 || impl->processing || impl->pushing.load()!=0)return false;
    if(impl->stop.load())return false;
    auto& affected=impl->slots[(size_t)mode];
    auto failedStart=[&](const char* message) noexcept
    {
        affected.hasCapture=false;affected.cachedSamples=0;
        affected.proposal.improved=false;affected.proposal.targetImproved=false;
        for(auto&channel:affected.capture)channel.clear();
        for(auto&channel:impl->audio)channel.clear();
        try{affected.status.message.assign(message);}catch(...){affected.status.message.clear();}
        impl->cancelled=true;impl->active=-1;impl->written=0;impl->target=0;
        impl->processing=false;impl->strengthJob=false;affected.state=State::error;
        return false;
    };
    try
    {
    const int count=(int)std::llround(context.sampleRate*seconds);
    for(auto &channel:impl->audio)channel.assign((size_t)count,0);
    impl->rate=context.sampleRate;impl->context=context;impl->cancelled=false;impl->written=0;impl->target=count;impl->firstPpq=std::numeric_limits<double>::quiet_NaN();impl->captureBpm=0;impl->timingChanged=false;
    impl->strengthJob=false;
    const auto gen=++impl->generation;auto &slot=impl->slots[(size_t)mode];slot.revision=context.revision;slot.progress=0;slot.proposal={};slot.status={};
    slot.status.requestedSeconds=seconds;slot.status.generation=gen;slot.status.contextRevision=context.revision;slot.status.modelsAvailable=available();slot.status.message="Recording actual main and sidechain samples";
    // The new worker can wait with active==-1. Do not expose writable capture
    // buffers until thread construction, including its OS resources, succeeded.
    if(!impl->worker.joinable())impl->worker=std::thread([p=impl.get()]{p->run();});
    slot.state=State::capturing;
    impl->active.store(mode,std::memory_order_release);
    impl->wake.notify_one();return true;
    }
    catch(const std::bad_alloc&){return failedStart("Insufficient memory to start local capture");}
    catch(const std::exception&){return failedStart("Unable to start local capture; no proposal was applied");}
    catch(...){return failedStart("Unexpected local capture failure; no proposal was applied");}
}
bool Controller::requestStrength(int mode,float strength)
{
    if(mode<0||mode>=6||!std::isfinite(strength)||strength<0||strength>1)return false;
    std::lock_guard<std::mutex> lock(impl->mutex);auto&slot=impl->slots[(size_t)mode];
    if(impl->active.load()>=0||impl->processing||!slot.hasCapture||slot.state.load()!=State::ready||!slot.proposal.targetImproved||slot.proposal.contextRevision!=slot.revision.load())return false;
    impl->strengthJob=true;impl->requestedStrength=strength;impl->cancelled=false;++impl->generation;
    impl->rate=slot.cachedContext.sampleRate;impl->captureBpm=slot.cachedContext.bpm;impl->firstPpq=slot.cachedContext.startPpq;impl->written=slot.cachedSamples;impl->target=slot.cachedSamples;
    slot.progress=0;slot.status.message="Verifying selected strength on retained real audio";slot.state=State::analyzing;impl->active=mode;
    impl->wake.notify_one();return true;
}
void Controller::push(const float *ml,const float *mr,const float *sl,const float *sr,int samples,double ppq,double bpm) noexcept
{
    struct PushGuard {std::atomic<int> &n;PushGuard(std::atomic<int>&v):n(v){n.fetch_add(1);}~PushGuard(){n.fetch_sub(1);}} guard(impl->pushing);
    const int mode=impl->active.load(std::memory_order_acquire);
    if(mode<0 || samples<=0 || impl->cancelled.load() || impl->slots[(size_t)mode].state.load()!=State::capturing)return;
    const int start=impl->written.load(std::memory_order_relaxed),count=std::min(samples,impl->target.load()-start);
    if(start==0){if(std::isfinite(ppq))impl->firstPpq=ppq;if(bpm>0 && std::isfinite(bpm))impl->captureBpm=bpm;}
    else
    {
        const double tempo=impl->captureBpm.load(),origin=impl->firstPpq.load();
        if(bpm>0 && tempo>0 && std::abs(bpm-tempo)>.001)impl->timingChanged=true;
        if(impl->context.trigger==0 && std::isfinite(ppq) && std::isfinite(origin) && tempo>0 &&
           std::abs(ppq-(origin+start/impl->rate*tempo/60))>.02)impl->timingChanged=true;
    }
    for(int i=0;i<count;++i){impl->audio[0][(size_t)(start+i)]=audio(ml,i);impl->audio[1][(size_t)(start+i)]=audio(mr?mr:ml,i);impl->audio[2][(size_t)(start+i)]=audio(sl,i);impl->audio[3][(size_t)(start+i)]=audio(sr?sr:sl,i);}
    impl->written.store(start+count,std::memory_order_release);
}
void Controller::cancel(int mode) noexcept
{
    // Explicit UI cancellation also dismisses a completed proposal, including
    // one whose Strength request is queued while another sector owns the worker.
    // No cache vectors are freed here and another sector's worker is untouched.
    if(mode>=0 && mode<6){auto expected=State::ready;
        impl->slots[(size_t)mode].state.compare_exchange_strong(expected,State::cancelled,
                                                               std::memory_order_release,std::memory_order_relaxed);}
    const int current=impl->active.load();if(current<0 || (mode>=0 && current!=mode))return;
    impl->cancelled=true;++impl->generation;
    impl->slots[(size_t)current].state=State::cancelled;
    // Worker clears active after it has relinquished all pointers into the capture.
}
void Controller::invalidate(int mode,std::uint64_t revision) noexcept
{
    if(mode<0 || mode>=6)return;auto &slot=impl->slots[(size_t)mode];slot.revision=revision;
    if(slot.state.load()==State::ready && slot.proposalRevision.load()!=revision)slot.state=State::stale;
}
Status Controller::status(int mode) const
{
    if(mode<0 || mode>=6)return {};std::lock_guard<std::mutex> lock(impl->mutex);const auto &slot=impl->slots[(size_t)mode];
    auto out=slot.status;out.state=slot.state.load();out.modelsAvailable=available();out.analysisProgress=slot.progress.load();
    if(impl->active.load()==mode){out.capturedBpm=impl->captureBpm.load();out.capturedSeconds=impl->written.load()/impl->rate;out.captureProgress=clamp((float)impl->written.load()/std::max(1,impl->target.load()));}
    else if(out.state==State::ready || out.state==State::insufficient || out.state==State::error){out.captureProgress=1;out.capturedSeconds=out.requestedSeconds;}
    return out;
}
bool Controller::proposal(int mode,Proposal &out) const
{
    if(mode<0 || mode>=6)return false;std::lock_guard<std::mutex> lock(impl->mutex);const auto &slot=impl->slots[(size_t)mode];
    if(slot.state.load()!=State::ready || slot.proposal.contextRevision!=slot.revision.load())return false;out=slot.proposal;return true;
}
Candidate Controller::blend(const Candidate &a,const Candidate &b,float strength)
{
    strength=clamp(strength);if(strength<=0)return a;if(strength>=1)return b;auto c=b;
    auto mix=[&](float x,float y){return x+(y-x)*strength;};
    c.curve.amount=mix(a.curve.amount,b.curve.amount);c.curve.phase=mix(a.curve.phase,b.curve.phase);c.curve.length=mix(a.curve.length,b.curve.length);
    c.curve.tension=mix(a.curve.tension,b.curve.tension);c.curve.attack=mix(a.curve.attack,b.curve.attack);c.curve.release=mix(a.curve.release,b.curve.release);
    for(int i=0;i<c.controlCount;++i)c.controls[(size_t)i].value=mix(get(a,c.controls[(size_t)i].id,c.controls[(size_t)i].value),c.controls[(size_t)i].value);
    if(strength<.5f){c.curve.shape=a.curve.shape;c.curve.custom=a.curve.custom;c.curve.count=a.curve.count;c.curve.points=a.curve.points;}
    return c;
}
} // namespace odx::ai
