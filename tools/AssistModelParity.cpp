#include "AssistTrainingData.h"
#include "NeuralModels.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

int main(int argc,char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::string golden,report;int scenes=2;
    for(int i=1;i+1<argc;i+=2){const std::string key=argv[i];if(key=="--golden")golden=argv[i+1];else if(key=="--audio-report")report=argv[i+1];else if(key=="--heldout-scenes")scenes=std::stoi(argv[i+1]);}
    const auto started=std::chrono::steady_clock::now();
    int passed=0,failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';(ok?passed:failed)++;};
    odx::ai::Controller availability;const bool matchingModels=availability.available();
    check(matchingModels,"Six embedded models match the r2 DSP/guard contract");
    if(!golden.empty())
    {
        std::ifstream file(golden);std::string line;std::getline(file,line);double maxError=0;int rows=0;std::array<int,6> coverage{};
        while(std::getline(file,line))
        {
            std::stringstream cells(line);std::string item;std::vector<float> values;
            while(std::getline(cells,item,','))values.push_back(std::stof(item));
            if(values.size()!=1+odx::ai::featureCount+odx::ai::outputCount){++failed;continue;}
            odx::ai::Features x{};for(int n=0;n<odx::ai::featureCount;++n)x[(size_t)n]=values[(size_t)(1+n)];
            const int mode=(int)values[0];if(mode<0||mode>=6){++failed;continue;}++coverage[(size_t)mode];
            const auto y=odx::ai::neural::infer(mode,x);
            for(int n=0;n<odx::ai::outputCount;++n)maxError=std::max(maxError,(double)std::abs(y[(size_t)n]-values[(size_t)(1+odx::ai::featureCount+n)]));++rows;
        }
        std::cout<<"Parity max_abs_error "<<maxError<<" on "<<rows<<" heldout feature vectors\n";
        check(rows>=6 && std::all_of(coverage.begin(),coverage.end(),[](int n){return n>0;}) && maxError<3e-5,"C++ inference matches training export for every trained sector");
    }
    if(!report.empty() && matchingModels)
    {
        auto metricsJson=[](const odx::ai::Metrics &m){auto *v=new juce::DynamicObject();
            v->setProperty("objective",m.objective);v->setProperty("overlap",m.overlap);
            v->setProperty("offHitPreservationLoss",m.preservation);v->setProperty("derivativeArtifactProxy",m.artifacts);
            v->setProperty("attenuationExtent",m.hitExtent);v->setProperty("inputRms",m.inputRms);v->setProperty("outputRms",m.outputRms);
            v->setProperty("finite",m.finite);v->setProperty("analysedChannels",m.analysedChannels);
            v->setProperty("preservationMeasured",m.preservationMeasured);v->setProperty("protectedFraction",m.protectedFraction);v->setProperty("protectedRmsDb",m.protectedRmsDb);
            juce::Array<juce::var> bands;for(auto db:m.protectedBandDb)bands.add(db);v->setProperty("protectedBandDb",juce::var(bands));v->setProperty("protectedBandMask",(int)m.protectedBandMask);
            v->setProperty("recoveryRegions",m.recoveryRegions);v->setProperty("worstRecoveryDb",m.worstRecoveryDb);v->setProperty("protectedAttacks",m.protectedAttacks);v->setProperty("worstAttackDb",m.worstAttackDb);return juce::var(v);};
        juce::Array<juce::var> results;std::atomic<bool> cancel{false};
        for(int mode=0;mode<6;++mode)
        {
            juce::Array<juce::var> sceneResults;
            juce::var antiphaseResult;bool antiphaseValid=false;
            int modelWins=0,rawModelWins=0,modelRejected=0,refinedWins=0,valid=0;bool acceptedGuards=true;double baselineSum=0,modelSum=0,refinedSum=0;
            for(int n=0;n<scenes;++n)
            {
                const int sceneId=24000+(n/2)*12+10+n%2;
                int retries=0;auto scene=odx::ai::training::synth(sceneId,mode,142030);auto context=odx::ai::training::context(mode,scene);
                auto observation=odx::ai::analyse(scene.view(),mode,context);
                auto coverage=odx::ai::measure(scene.view(),scene.main.data(),scene.main.data(),(int)scene.main.size(),context,mode);
                while((!observation.mainPresent || !observation.sidechainPresent || observation.validHits<2 || !coverage.preservationMeasured) && retries<15)
                {
                    ++retries;scene=odx::ai::training::synth(sceneId,mode,142030+retries*104729);
                    context=odx::ai::training::context(mode,scene);observation=odx::ai::analyse(scene.view(),mode,context);
                    coverage=odx::ai::measure(scene.view(),scene.main.data(),scene.main.data(),(int)scene.main.size(),context,mode);
                }
                if(!observation.mainPresent || !observation.sidechainPresent || observation.validHits<2 || !coverage.preservationMeasured)continue;
                const auto output=odx::ai::neural::infer(mode,observation.features);auto candidate=odx::ai::decode(output,mode,context.baseline);
                odx::ai::Metrics baseline,model;
                if(!odx::ai::renderCandidate(mode,context.baseline,scene.view(),context,baseline,cancel)||!odx::ai::renderCandidate(mode,candidate,scene.view(),context,model,cancel))continue;
                auto bestMetrics=baseline;double best=baseline.objective;int bestAttempt=-1;
                for(int attempt=0;attempt<8;++attempt)
                {
                    const auto perturbed=odx::ai::refinementCandidate(output,attempt);
                    odx::ai::Metrics metric;if(odx::ai::renderCandidate(mode,odx::ai::decode(perturbed,mode,context.baseline),scene.view(),context,metric,cancel) && odx::ai::assessCandidate(baseline,metric).admissible && metric.objective<best){best=metric.objective;bestMetrics=metric;bestAttempt=attempt;}
                }
                if(best>=baseline.objective-1e-6){best=baseline.objective;bestMetrics=baseline;bestAttempt=-1;}
                const auto modelGuard=odx::ai::assessCandidate(baseline,model);
                ++valid;rawModelWins+=model.objective<baseline.objective-1e-6;modelWins+=modelGuard.admissible&&model.objective<baseline.objective-1e-6;modelRejected+=!modelGuard.admissible;refinedWins+=best<baseline.objective-1e-6;
                acceptedGuards &=bestAttempt<0||odx::ai::assessCandidate(baseline,bestMetrics).admissible;
                baselineSum+=baseline.objective;modelSum+=model.objective;refinedSum+=best;
                auto *detail=new juce::DynamicObject();detail->setProperty("scene",sceneId);detail->setProperty("family",scene.family);detail->setProperty("sourceSeed",scene.seed);detail->setProperty("attempt",retries+1);detail->setProperty("validHits",observation.validHits);
                detail->setProperty("bpm",scene.bpm);detail->setProperty("durationSeconds",scene.main.size()/scene.rate);
                detail->setProperty("baseline",metricsJson(baseline));detail->setProperty("modelOnly",metricsJson(model));
                detail->setProperty("modelOnlyGuardPassed",modelGuard.admissible);detail->setProperty("modelOnlyGuardReason",juce::String(modelGuard.reason));
                detail->setProperty("accepted",metricsJson(bestMetrics));detail->setProperty("acceptedAttempt",bestAttempt);
                detail->setProperty("abstained",bestAttempt<0);sceneResults.add(juce::var(detail));
                if(n==0)
                {
                    std::vector<float> negativeMain(scene.main.size()),negativeSide(scene.side.size());
                    std::transform(scene.main.begin(),scene.main.end(),negativeMain.begin(),[](float v){return -v;});
                    std::transform(scene.side.begin(),scene.side.end(),negativeSide.begin(),[](float v){return -v;});
                    const odx::ai::CaptureView stereo{scene.main.data(),negativeMain.data(),scene.side.data(),negativeSide.data(),(int)scene.main.size(),scene.rate,scene.bpm};
                    const auto stereoObservation=odx::ai::analyse(stereo,mode,context);
                    const auto stereoOutput=odx::ai::neural::infer(mode,stereoObservation.features);
                    const auto stereoCandidate=odx::ai::decode(stereoOutput,mode,context.baseline);
                    odx::ai::Metrics stereoBaseline,stereoModel;
                    antiphaseValid=stereoObservation.mainPresent && stereoObservation.sidechainPresent && stereoObservation.validHits>=2
                        && stereoObservation.analysedChannels==2
                        && std::all_of(stereoObservation.features.begin(),stereoObservation.features.end(),[](float v){return std::isfinite(v);})
                        && std::all_of(stereoOutput.begin(),stereoOutput.end(),[](float v){return std::isfinite(v);})
                        && odx::ai::renderCandidate(mode,context.baseline,stereo,context,stereoBaseline,cancel)
                        && odx::ai::renderCandidate(mode,stereoCandidate,stereo,context,stereoModel,cancel)
                        && stereoBaseline.finite && stereoModel.finite
                        && stereoBaseline.analysedChannels==2 && stereoModel.analysedChannels==2;
                    auto *stereoDetail=new juce::DynamicObject();stereoDetail->setProperty("scene",sceneId);stereoDetail->setProperty("family",scene.family);stereoDetail->setProperty("sourceSeed",scene.seed);
                    stereoDetail->setProperty("rightChannels","Exact negatives of actual heldout main/sidechain left channels");
                    stereoDetail->setProperty("validHits",stereoObservation.validHits);stereoDetail->setProperty("analysedChannels",stereoObservation.analysedChannels);
                    stereoDetail->setProperty("baseline",metricsJson(stereoBaseline));stereoDetail->setProperty("modelOnly",metricsJson(stereoModel));
                    stereoDetail->setProperty("passed",antiphaseValid);antiphaseResult=juce::var(stereoDetail);
                }
            }
            auto *row=new juce::DynamicObject();row->setProperty("mode",mode);row->setProperty("heldoutScenes",valid);row->setProperty("modelOnlyWins",modelWins);row->setProperty("modelAndRefinementWins",refinedWins);
            row->setProperty("modelOnlyObjectiveWinsBeforeGuard",rawModelWins);row->setProperty("modelOnlyGuardRejected",modelRejected);row->setProperty("allAcceptedPreservationGuardsPassed",acceptedGuards);
            row->setProperty("baselineObjectiveMean",baselineSum/std::max(1,valid));row->setProperty("modelOnlyObjectiveMean",modelSum/std::max(1,valid));row->setProperty("acceptedObjectiveMean",refinedSum/std::max(1,valid));row->setProperty("scenes",juce::var(sceneResults));
            row->setProperty("antiphaseHeldout",antiphaseResult);results.add(juce::var(row));
            check(valid==scenes && acceptedGuards && refinedSum<=baselineSum+1e-8,"Actual-DSP heldout evaluation finite; preservation guards pass before any accepted improvement");
            check(antiphaseValid,"Independent heldout antiphase stereo features, inference and actual-DSP metrics remain valid");
            std::cout<<"mode "<<mode<<" model wins "<<modelWins<<'/'<<valid<<"; refined accepted "<<refinedWins<<'/'<<valid<<std::endl;
        }
        auto *root=new juce::DynamicObject();root->setProperty("modelVersion",odx::ai::neural::version);root->setProperty("method","Actual compiled DSP, synthesis families 10/11 excluded from train/validation; model-only separated from bounded refinement; not a listening test");root->setProperty("modes",juce::var(results));
        root->setProperty("elapsedSeconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
        check(juce::File(juce::String(report)).replaceWithText(juce::JSON::toString(juce::var(root),true)),"Actual-DSP heldout JSON report written");
    }
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
