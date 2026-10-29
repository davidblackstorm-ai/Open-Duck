#include "AssistTrainingData.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace
{
std::vector<std::string> cells(const std::string &line)
{
    std::stringstream input(line);std::string item;std::vector<std::string> result;
    while(std::getline(input,item,','))result.push_back(item);return result;
}
bool exact(const odx::ai::Metrics &a,const odx::ai::Metrics &b)
{
    return a.objective==b.objective && a.overlap==b.overlap && a.preservation==b.preservation && a.artifacts==b.artifacts &&
           a.hitExtent==b.hitExtent && a.inputRms==b.inputRms && a.outputRms==b.outputRms && a.finite==b.finite;
}
}
int main(int argc,char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;std::string datasets,output;int onlyMode=-1,limit=0;
    bool render=true;
    for(int i=1;i+1<argc;i+=2){const std::string key=argv[i];if(key=="--datasets")datasets=argv[i+1];else if(key=="--output")output=argv[i+1];else if(key=="--mode")onlyMode=std::stoi(argv[i+1]);else if(key=="--limit")limit=std::stoi(argv[i+1]);else if(key=="--render")render=std::stoi(argv[i+1])!=0;}
    if(datasets.empty()||output.empty()||onlyMode>5||onlyMode<-1)return 2;
    int passed=0,failed=0,rowsTotal=0;std::atomic<bool> cancel{false};juce::Array<juce::var> reports;
    for(int mode=0;mode<6;++mode)
    {
        if(onlyMode>=0 && mode!=onlyMode)continue;
        const auto path=juce::File(juce::String(datasets)).getChildFile("mode-"+juce::String(mode)+".csv");
        std::ifstream file(path.getFullPathName().toStdString());std::string line;std::getline(file,line);
        auto names=cells(line);std::map<std::string,size_t> columns;for(size_t i=0;i<names.size();++i)columns[names[i]]=i;
        int rows=0,featureExact=0,monoMetricExact=0,archivedMetricMatch=0,antiphaseFinite=0;
        while(std::getline(file,line))
        {
            if(limit>0 && rows>=limit)break;auto values=cells(line);if(values.size()!=names.size()){++failed;continue;}
            auto read=[&](const std::string &name){return std::stof(values[columns.at(name)]);};
            const int sceneId=(int)read("scene"),sourceSeed=(int)read("sourceSeed");
            auto scene=odx::ai::training::synth(sceneId,mode,sourceSeed-sceneId*7919);auto context=odx::ai::training::context(mode,scene);
            const auto view=scene.view();const auto old=odx::ai::analyseAverageReference(view,mode,context),current=odx::ai::analyse(view,mode,context);
            bool features=current.analysedChannels==1 && current.features==old.features && current.mainPresent && current.sidechainPresent && current.validHits>=2;
            for(int k=0;k<odx::ai::featureCount;++k)features &= current.features[(size_t)k]==read("f"+std::to_string(k));
            featureExact+=features;odx::ai::Outputs encoded{};for(int k=0;k<odx::ai::outputCount;++k)encoded[(size_t)k]=read("y"+std::to_string(k));
            if(render)
            {
                const auto selected=odx::ai::decode(encoded,mode,context.baseline);bool metrics=true,archived=true;
                for(int candidate=0;candidate<2;++candidate)
                {
                    odx::ai::StereoRenderAudit audit;
                    const bool valid=odx::ai::auditCandidateStereo(mode,candidate==0?context.baseline:selected,view,context,audit,cancel);
                    metrics &= valid && audit.measured.analysedChannels==1 && audit.maximumChannelDifference==0 && exact(audit.measured,audit.averageReference);
                    const double previous=read(candidate==0?"baselineObjective":"teacherObjective");
                    // CSV stores nine significant digits; encode/decode can round one FP32 ULP.
                    archived &= valid && std::abs(audit.measured.objective-previous)<=std::max(1e-6,std::abs(previous)*1e-5);
                }
                monoMetricExact+=metrics;archivedMetricMatch+=archived;
                if(rows==0)
                {
                    std::vector<float> negativeMain=scene.main,negativeSide=scene.side;
                    for(auto &x:negativeMain)x=-x;for(auto &x:negativeSide)x=-x;
                    auto anti=view;anti.mainR=negativeMain.data();anti.sideR=negativeSide.data();
                    const auto stereo=odx::ai::analyse(anti,mode,context);odx::ai::StereoRenderAudit actual;
                    antiphaseFinite=stereo.analysedChannels==2 && stereo.mainPresent && stereo.sidechainPresent && stereo.validHits>=2 &&
                        odx::ai::auditCandidateStereo(mode,selected,anti,context,actual,cancel) && actual.measured.analysedChannels==2 && actual.measured.finite;
                }
            }
            ++rows;++rowsTotal;
            if(rows%12==0)std::cout<<"AUDIT mode="<<mode<<" rows="<<rows<<" featureExact="<<featureExact<<" metricExact="<<monoMetricExact<<std::endl;
        }
        const int expected=limit>0?limit:144;
        const bool ok=rows==expected && featureExact==rows && (!render || (monoMetricExact==rows && archivedMetricMatch==rows && antiphaseFinite==1));
        (ok?passed:failed)++;std::cout<<(ok?"PASS ":"FAIL ")<<"mode "<<mode<<" frozen mono parity and antiphase stereo audit"<<std::endl;
        auto *report=new juce::DynamicObject();report->setProperty("mode",mode);report->setProperty("rows",rows);report->setProperty("featureVectorsExactlyEqual",featureExact);
        report->setProperty("renderedBaselineAndSelected",render);report->setProperty("actualMonoMetricsExactlyEqual",monoMetricExact);
        report->setProperty("archivedCsvObjectivesWithinSerializationTolerance",archivedMetricMatch);report->setProperty("actualAntiphaseStereoFinite",antiphaseFinite==1);reports.add(juce::var(report));
    }
    auto *root=new juce::DynamicObject();root->setProperty("rows",rowsTotal);root->setProperty("modes",juce::var(reports));root->setProperty("passed",passed);root->setProperty("failed",failed);
    root->setProperty("method","Reconstruct exact generator seed from each frozen CSV, compare all96features exactly, actual-DSP baseline+selected mono new/reference metrics exactly, archived objective tolerance only for9-digit CSV/FP32 encode roundtrip; one actual antiphase render per mode.");
    if(!juce::File(juce::String(output)).replaceWithText(juce::JSON::toString(juce::var(root),true)))++failed;
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
