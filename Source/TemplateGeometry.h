#pragma once
#include <algorithm>
#include <cmath>
#include "MeasuredSpline4.h"
namespace odx::template4 {
constexpr float pi = 3.14159265358979323846f;
inline float unit(float x) { return std::clamp(std::isfinite(x) ? x : 0.f, 0.f, 1.f); }
struct Rotary {
    float normalized, start, angle, end;
    static Rotary from(float value, float low, float high) {
        const float u = unit((value-low)/(high-low));
        const float first = pi*1.25f, last = pi*2.75f;
        return {u, first, first+(last-first)*u, last};
    }
};
// MIX 0..100 => -0.5..+0.5 cycles; PHASE -50..50 => 0.5..1.5 duration.
// No wrapping: exposed boundaries hold the original endpoint value.
inline float toSource(float display, float mix, float phase) {
    return unit(.5f+(display-.5f-(mix-50.f)*.01f)/(1.f+phase*.01f));
}
inline float toDisplay(float source, float mix, float phase) {
    return .5f+(source-.5f)*(1.f+phase*.01f)+(mix-50.f)*.01f;
}
inline float wrap(float x) { return std::isfinite(x)?x-std::floor(x):0.f; }
// The zero-offset identity preserves both endpoints of existing saved curves.
inline float movedCycle(float display,float movePercent) {
    return movePercent==0?display:wrap(display-movePercent*.01f);
}
struct SlopeBounds { float start,end; };
inline SlopeBounds slopeBounds(int shape,int profileModel=0) {
    if(profileModel==1){const auto b=odx::measured4::spline::bounds(shape);return {float(b.start),float(b.end)};}
    constexpr SlopeBounds bounds[]{
        {.25f,.75f},{.1f,.85f},{.1f,.9f},{.1f,.9f},
        {.28f,.78f},{.4f,.44f},{.1f,.9f},{.1f,.9f},
        {.05f,.7f},{.18f,.6f},{.63f,.67f},{.2f,.85f},
        {.1f,.65f},{.2f,.85f},{.35f,.65f},{.1f,.9f}};
    return bounds[std::clamp(shape,0,15)];
}
inline SlopeBounds slopeTargetBounds(int shape,float slopePercent,int profileModel=0) {
    const auto b=slopeBounds(shape,profileModel);
    if(profileModel==1){const float delta=float(odx::measured4::spline::delta(shape,slopePercent));return {b.start+delta,b.end+delta};}
    constexpr float epsilon=.001f;
    const float p=std::clamp(std::isfinite(slopePercent)?slopePercent:0.f,-100.f,100.f)*.01f;
    const float delta=p*(p>=0?1-epsilon-b.end:b.start-epsilon);
    return {b.start+delta,b.end+delta};
}
// Inverse of the signed normalized control-window translation, in cycle units.
inline float slopePercentForDelta(int shape,float delta,int profileModel=0) {
    if(!std::isfinite(delta))return 0;
    const auto b=slopeBounds(shape,profileModel);
    float margin=delta<0?b.start-.001f:1-.001f-b.end;
    if(profileModel==1){const auto raw=odx::measured4::spline::bounds(shape);
        margin=float(delta<0?raw.start-raw.outerStart:raw.outerEnd-raw.end);}
    return margin>0?std::clamp(delta/margin*100.f,-100.f,100.f):0.f;
}
inline float toSlopeSource(float display,int shape,float slopePercent,int profileModel=0) {
    display=unit(display);if(profileModel==1||slopePercent==0||display==0||display==1)return display;
    const auto a=slopeBounds(shape,profileModel),b=slopeTargetBounds(shape,slopePercent,profileModel);
    if(display<b.start)return display*a.start/b.start;
    if(display<b.end)return a.start+(display-b.start)*(a.end-a.start)/(b.end-b.start);
    return a.end+(display-b.end)*(1-a.end)/(1-b.end);
}
inline float fromSlopeSource(float source,int shape,float slopePercent,int profileModel=0) {
    source=unit(source);if(profileModel==1||slopePercent==0||source==0||source==1)return source;
    const auto a=slopeBounds(shape,profileModel),b=slopeTargetBounds(shape,slopePercent,profileModel);
    if(source<a.start)return source*b.start/a.start;
    if(source<a.end)return b.start+(source-a.start)*(b.end-b.start)/(a.end-a.start);
    return b.end+(source-a.end)*(1-b.end)/(1-a.end);
}
inline float curveSource(float display,float mix,float phase,float length,
                         int shape,float movePercent,float slopePercent,int profileModel=0) {
    const float positioned=unit(.5f+(toSource(movedCycle(display,movePercent),mix,phase)-.5f)/length);
    return toSlopeSource(positioned,shape,slopePercent,profileModel);
}
// Unwrapped inverse keeps anchor intervals ordered for tiled slope highlights.
inline float curveDisplay(float source,float mix,float phase,float length,
                          int shape,float movePercent,float slopePercent,int profileModel=0) {
    return toDisplay(.5f+(fromSlopeSource(source,shape,slopePercent,profileModel)-.5f)*length,mix,phase)+movePercent*.01f;
}
inline double curveSourcePrecise(double display,float mix,float phase,float length,
                                int shape,float movePercent,float slopePercent,int profileModel=0) {
    double x=std::isfinite(display)?display:0;
    if(movePercent!=0){x-=double(movePercent)*.01;x-=std::floor(x);}
    if(mix!=50||phase!=0)x=.5+(x-.5-(double(mix)-50)*.01)/(1+double(phase)*.01);
    x=std::clamp(x,0.,1.);
    if(length!=1)x=std::clamp(.5+(x-.5)/double(length),0.,1.);
    if(profileModel!=1&&slopePercent!=0&&x>0&&x<1){
        const auto a=slopeBounds(shape,profileModel),b=slopeTargetBounds(shape,slopePercent,profileModel);
        if(x<b.start)x=x*a.start/b.start;
        else if(x<b.end)x=a.start+(x-b.start)*(a.end-a.start)/(b.end-b.start);
        else x=a.end+(x-b.end)*(1-a.end)/(1-b.end);
    }
    return x;
}
inline float smooth(float x) { x=unit(x);return x*x*(3-2*x); }
inline float factoryShape(float x,int shape) {
    x=unit(x);
    switch(shape) {
        case 0:return smooth((x-.25f)/.5f);
        case 1:return unit((x-.1f)/.75f);
        case 2:return std::pow(unit((x-.1f)/.8f),2.7f);
        case 3:return 1-std::pow(1-unit((x-.1f)/.8f),2.7f);
        case 4:return x<.28f?0.f:x<.56f?.33f:x<.78f?.66f:1.f;
        case 5:return 1-smooth((x-.40f)/.04f);
        case 6:return std::max(0.f,std::sin(pi*x));
        case 7:return 1-smooth((x-.1f)/.8f);
        case 8:return 1-smooth((x-.05f)/.65f);
        case 9:return unit((x-.18f)/.42f);
        case 10:return smooth((x-.63f)/.04f);
        case 11:return 1-smooth((x-.2f)/.65f);
        case 12:return std::pow(1-unit((x-.1f)/.55f),2.4f);
        case 13:return smooth((x-.2f)/.65f);
        case 14:return unit((x-.35f)/.3f);
        case 15:return std::pow(unit((x-.1f)/.8f),1.65f);
        default:return smooth(x);
    }
}
}
