#include "AssistTrainingData.h"
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc,char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int mode=0,scenes=48,candidates=16,seed=42030,trainOnly=0;std::string output;
    for(int i=1;i+1<argc;i+=2){std::string key=argv[i];if(key=="--mode")mode=std::stoi(argv[i+1]);else if(key=="--scenes")scenes=std::stoi(argv[i+1]);else if(key=="--candidates")candidates=std::stoi(argv[i+1]);else if(key=="--seed")seed=std::stoi(argv[i+1]);else if(key=="--train-only")trainOnly=std::stoi(argv[i+1]);else if(key=="--output")output=argv[i+1];}
    if(mode<0||mode>=6||scenes<12||candidates<1||candidates>64||output.empty()){std::cerr<<"Usage: AssistTeacher --mode 0..5 --scenes 48 --candidates 16 --seed 32030 --output dataset.csv\n";return 2;}
    std::cout<<"CONTRACT OpenDuck-native-teacher-0.3.0-r2 hard-preservation-v1 trainOnly="<<trainOnly<<std::endl;
    std::ofstream file(output);if(!file)return 3;file<<std::setprecision(9);
    file<<"mode,scene,split";for(int n=0;n<odx::ai::featureCount;++n)file<<",f"<<n;for(int n=0;n<odx::ai::outputCount;++n)file<<",y"<<n;file<<",baselineObjective,teacherObjective,sourceSeed,attempt,eligibleCandidates,rejectedCandidates,selectedQuietDb,selectedRecoveryDb\n";
    std::atomic<bool> cancel{false};int rows=0,improved=0;
    for(int sceneId=0;sceneId<scenes;++sceneId)
    {
        int sourceId=trainOnly?(sceneId/8)*12+sceneId%8:sceneId,retries=0;
        auto scene=odx::ai::training::synth(sourceId,mode,seed);auto context=odx::ai::training::context(mode,scene);
        auto observation=odx::ai::analyse(scene.view(),mode,context);
        auto coverage=odx::ai::measure(scene.view(),scene.main.data(),scene.main.data(),(int)scene.main.size(),context,mode);
        // Keep the exact scene slot/family/split and INIT/edited balance while
        // changing only the source seed; runtime guards are never lowered.
        while((!observation.mainPresent || !observation.sidechainPresent || observation.validHits<2 || !coverage.preservationMeasured) && retries<15)
        {
            ++retries;scene=odx::ai::training::synth(sourceId,mode,seed+retries*104729);
            context=odx::ai::training::context(mode,scene);observation=odx::ai::analyse(scene.view(),mode,context);
            coverage=odx::ai::measure(scene.view(),scene.main.data(),scene.main.data(),(int)scene.main.size(),context,mode);
        }
        if(!observation.mainPresent || !observation.sidechainPresent || observation.validHits<2 || !coverage.preservationMeasured){std::cerr<<"No eligible actual-audio scene with measured quiet/recovery coverage after 16 deterministic guarded attempts\n";return 7;}
        const auto view=scene.view();
        odx::ai::Metrics baseline,best;
        if(!odx::ai::renderCandidate(mode,context.baseline,view,context,baseline,cancel)){std::cerr<<"Actual DSP baseline failed at scene "<<sceneId<<'\n';return 4;}
        // A fixed search bank per mode avoids teaching the network accidental
        // differences caused only by changing the candidate RNG between scenes.
        // Two disjoint eight-shape banks alternate per complete 12-family group,
        // exposing all 16 shapes within train, validation and test independently.
        auto selected=context.baseline;best=baseline;std::mt19937 generator((unsigned)(seed+mode*100003));int eligible=0,rejected=0;
        for(int trial=0;trial<candidates;++trial)
        {
            auto candidate=odx::ai::training::randomCandidate(context.baseline,mode,trial,generator,observation,8*((sourceId/12)%2));odx::ai::Metrics metric;
            if(!odx::ai::renderCandidate(mode,candidate,view,context,metric,cancel)){std::cerr<<"Actual DSP candidate failed\n";return 5;}
            const auto guard=odx::ai::assessCandidate(baseline,metric);
            if(guard.admissible){++eligible;if(metric.objective<best.objective-1e-6){best=metric;selected=candidate;}}else ++rejected;
        }
        const auto target=odx::ai::encode(selected,mode);
        file<<mode<<','<<sourceId<<','<<scene.split;for(float value:observation.features)file<<','<<value;for(float value:target)file<<','<<value;file<<','<<baseline.objective<<','<<best.objective<<','<<scene.seed<<','<<retries+1<<','<<eligible<<','<<rejected<<','<<best.protectedRmsDb<<','<<best.worstRecoveryDb<<'\n';file.flush();
        ++rows;if(best.objective<baseline.objective-1e-6)++improved;
        std::cout<<"mode "<<mode<<" scene "<<sceneId+1<<'/'<<scenes<<" source "<<sourceId<<" seed "<<scene.seed<<" attempt "<<retries+1<<" skips "<<retries<<" hits "<<observation.validHits<<" eligible "<<eligible<<" rejected "<<rejected<<" actualDSP "<<baseline.objective<<" -> "<<best.objective<<std::endl;
    }
    std::cout<<"TEACHER "<<rows<<" audio scenes, "<<improved<<" measured improvements; no perceptual optimum claimed\n";
    return rows==scenes?0:6;
}
