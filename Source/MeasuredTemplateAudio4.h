#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace odx::audio4 {

// Zero-latency repair of periodic seams and steep gain steps. Only the
// transition is rounded; ordinary curve samples pass through unchanged.
// This bounds the gain of the protected ducking path, never the audio signal.
class CausalGainSmoother {
public:
    void prepare(double sampleRate) {
        window=std::max(1,int(std::round(sampleRate*.002)));reset();
    }
    void reset(float initial=1) {
        output=previous=from=initial;position=window;lastPhase=0;
        observed=phaseObserved=wasEnabled=false;
    }
    float process(float gain,double phase,bool enabled) {
        const float target=enabled?std::clamp(gain,0.f,1.f):gain;
        const bool wrapped=phaseObserved&&phase<lastPhase-.5;
        lastPhase=phase;phaseObserved=true;
        if(!enabled) {
            output=previous=from=target;position=window;observed=true;wasEnabled=false;
            return target;
        }
        if(!observed) {
            output=previous=from=target;position=window;observed=wasEnabled=true;
            return target;
        }
        if(!wasEnabled||(wrapped&&std::abs(target-output)>1.e-5f)||std::abs(target-previous)>.02f) {
            from=std::clamp(output,0.f,1.f);position=0;
        }
        float result=target;
        if(position<window) {
            const float u=float(position)/window;
            const float weight=u*u*(3-2*u);
            result=from+(target-from)*weight;++position;
        }
        output=std::clamp(result,0.f,1.f);previous=target;wasEnabled=true;
        return output;
    }
private:
    int window=96,position=96;
    float output=1,previous=1,from=1;
    double lastPhase=0;
    bool observed=false,phaseObserved=false,wasEnabled=false;
};

// A causal complementary split. Preparation/allocation is never performed by
// process(); coefficient design runs only when the block's cutoff changes.
class LowpassTpt {
public:
    void prepare(double sampleRate, double cutoff) {
        rate=sampleRate; previousCutoff=-1; memory={};
        setCutoff(cutoff); coefficient=target;
        slew=std::exp(-1.0/(rate*.002));
    }
    void reset() { memory={}; }
    void setCutoff(double cutoff) {
        cutoff=std::clamp(cutoff,20.0,std::min(5120.0,rate*.45));
        if(cutoff==previousCutoff)return;
        previousCutoff=cutoff;
        const double g=std::tan(3.14159265358979323846*cutoff/rate);
        target=float(g/(1.0+g));
    }
    // Once per stereo frame, not once per channel.
    void advance() {
        coefficient=float(target+(coefficient-target)*slew);
        if(std::abs(coefficient-target)<1.e-9f)coefficient=target;
    }
    float process(int channel,float input) {
        auto& z=memory[size_t(channel)];
        const float v=(input-z)*coefficient;
        const float low=v+z;
        z=low+v;
        return low;
    }
private:
    double rate=48000,previousCutoff=-1,slew=0;
    float coefficient=0,target=0;
    std::array<float,2> memory{};
};

// Anticipated jump corrections: add delta*smoothstep in the N samples BEFORE
// each accepted trigger. Four distance moments evaluate the sum in O(1), with
// amortised queue admission/removal. Simultaneous/close triggers superpose;
// raw steady-state curve samples are never clipped or low-pass filtered.
class AnticipatedGain {
public:
    void prepare(size_t capacity) { events.resize(std::max(size_t(8),capacity));reset(0); }
    void reset(int windowSamples) {
        window=std::max(0,windowSamples);first=count=active=0;
        moments={};lastSource=0;observed=false;rebaseCountdown=4096;
    }
    void push(int64_t triggerSample,float gainJump) {
        if(window==0||events.empty()||!std::isfinite(gainJump)||std::abs(gainJump)<1.e-12f)return;
        if(count&&events[(first+count-1)%events.size()].sample==triggerSample) {
            events[(first+count-1)%events.size()].delta+=gainJump;return;
        }
        // Capacity exceeds the longest declared latency, with at most one
        // coalesced jump per incoming sample. This is only a corruption guard.
        if(count==events.size())return;
        events[(first+count)%events.size()]={triggerSample,double(gainJump)};++count;
    }
    float correction(int64_t sourceSample) {
        if(window==0)return 0;
        const bool contiguous=observed&&sourceSample==lastSource+1;
        if(contiguous) {
            const auto old=moments;
            moments[3]=old[3]-3*old[2]+3*old[1]-old[0];
            moments[2]=old[2]-2*old[1]+old[0];
            moments[1]=old[1]-old[0];
        } else {moments={};active=0;}
        while(count&&events[first].sample<=sourceSample) {
            if(contiguous&&active) {moments[0]-=events[first].delta;--active;}
            first=(first+1)%events.size();--count;
        }
        if(active==0)moments={};
        while(active<count) {
            const auto& event=events[(first+active)%events.size()];
            const double d=double(event.sample-sourceSample);
            if(d>window)break;
            add(event.delta,d);++active;
        }
        // Rebase bounded fixed-capacity state to prevent numerical drift in
        // pathological uninterrupted bursts. No allocation or locking occurs.
        if(--rebaseCountdown==0) {
            moments={};
            for(size_t j=0;j<active;++j) {
                const auto& event=events[(first+j)%events.size()];
                add(event.delta,double(event.sample-sourceSample));
            }
            rebaseCountdown=4096;
        }
        observed=true;lastSource=sourceSample;
        const double n=double(window);
        return float(moments[0]-3*moments[2]/(n*n)+2*moments[3]/(n*n*n));
    }
private:
    struct Event {int64_t sample=0;double delta=0;};
    std::vector<Event> events;
    std::array<double,4> moments{};
    size_t first=0,count=0,active=0;
    int window=0,rebaseCountdown=4096;
    int64_t lastSource=0;
    bool observed=false;
    void add(double delta,double d) {
        double term=delta;
        for(auto& moment:moments){moment+=term;term*=d;}
    }
};
}
