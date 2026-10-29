#pragma once
#include <algorithm>
#include <cmath>

namespace odx::measured4 {
// Measured in the physical cycle, after periodic Catmull-Rom sampling.
// The last1/64 cycle is a linear gain ramp from the previous actual sample
// to the held endpoint. This is independent of MOVE/PHASE's source mapping.
// Evidence:8192/24000/22050-sample cycles, including fractional tail lengths.
template<class BaseSampler,class EndpointPolicy>
inline float applyOneShotTail(double physicalCycle,double phaseStep,double endPhase,
                              const BaseSampler& base,const EndpointPolicy& endpoint) noexcept {
    const double end=std::clamp(std::isfinite(endPhase)?endPhase:1.,0.000001,1.);
    if(!std::isfinite(physicalCycle))physicalCycle=0;
    if(physicalCycle>=end)return endpoint(end);
    const double duration=std::min(1./64.,end);
    const double first=end-duration;
    if(physicalCycle<first)return base(physicalCycle);
    double previous=first;
    if(std::isfinite(phaseStep)&&phaseStep>0){
        // Numerical guard preserves mathematically exact integer phase grids.
        const double firstIndex=std::ceil(first/phaseStep-1.e-9);
        previous=std::max(0.,(firstIndex-1)*phaseStep);
    }
    const double fraction=std::clamp((physicalCycle-previous)/duration,0.,1.);
    const float a=base(previous),b=endpoint(end);
    return float(double(a)+(double(b)-a)*fraction);
}
}
