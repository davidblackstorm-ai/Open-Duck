#include <JuceHeader.h>
#include <cmath>
#include <fstream>
#include <iostream>

// Independent black-box VST3 host: loads only the explicitly named installed
// reference. Does not link OpenDuck, open an audio device, scan plugin folders,
// touch DAW preferences, or inspect/decompile the reference executable.
class ProbeClock final : public juce::AudioPlayHead {
public:
    double sampleRate=48000, tempo=120;
    int64_t sample=0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;p.setBpm(tempo);p.setTimeInSamples(sample);
        p.setTimeInSeconds(sample/sampleRate);p.setPpqPosition(sample*tempo/(60*sampleRate));
        p.setIsPlaying(true);p.setTimeSignature(TimeSignature{4,4});
        p.setPpqPositionOfLastBarStart(std::floor(sample*tempo/(240*sampleRate))*4);
        return p;
    }
};
static juce::var object(){return new juce::DynamicObject;}
static void put(juce::var& v,const juce::Identifier& key,const juce::var& value){v.getDynamicObject()->setProperty(key,value);}
class PublicPresetWindow final:public juce::DocumentWindow,private juce::Timer {
public:
    PublicPresetWindow(juce::AudioPluginInstance& p,const juce::File& folder,int seconds)
      :DocumentWindow("Reference Functional Probe - public curve controls",juce::Colours::black,closeButton),plugin(p),out(folder),end(juce::Time::getMillisecondCounterHiRes()+1000.*seconds){
        setUsingNativeTitleBar(true);auto* editor=plugin.createEditorIfNeeded();
        if(!editor)throw std::runtime_error("Reference editor unavailable");
        setContentOwned(editor,true);centreWithSize(editor->getWidth(),editor->getHeight());setVisible(true);capture();startTimer(250);
    }
    ~PublicPresetWindow()override{stopTimer();clearContentComponent();}
    void closeButtonPressed()override{capture();setVisible(false);stopTimer();juce::MessageManager::getInstance()->stopDispatchLoop();}
private:
    void capture(){juce::MemoryBlock state;plugin.getStateInformation(state);if(state!=previous){out.getChildFile("public-gesture-"+juce::String(sequence++).paddedLeft('0',3)+".bin").replaceWithData(state.getData(),state.getSize());previous=std::move(state);}}
    void timerCallback()override{
        // VST3 editor-to-component messages are serviced by processing. Silence
        // is intentional: it commits controls without inventing an input signal.
        juce::AudioBuffer<float> silent(juce::jmax(2,juce::jmax(plugin.getTotalNumInputChannels(),plugin.getTotalNumOutputChannels())),256);silent.clear();
        juce::MidiBuffer midi;plugin.processBlock(silent,midi);capture();
        if(juce::Time::getMillisecondCounterHiRes()>=end)closeButtonPressed();
    }
    juce::AudioPluginInstance& plugin;juce::File out;juce::MemoryBlock previous;int sequence=0;double end;
};
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        if(argc==4&&juce::String::fromUTF8(argv[1])=="--decode-tree"){
            juce::MemoryBlock bytes;if(!juce::File(juce::String::fromUTF8(argv[2])).loadFileAsData(bytes))throw std::runtime_error("Named serialized settings file missing");
            auto tree=juce::ValueTree::readFromData(bytes.getData(),bytes.getSize());if(!tree.isValid())throw std::runtime_error("Settings are not a JUCE value tree");
            juce::File(juce::String::fromUTF8(argv[3])).replaceWithText(tree.toXmlString());
            std::cout<<"PASS decoded serialized plugin settings tree "<<tree.getType().toString()<<"\n";return 0;
        }
        if(argc<3){std::cerr<<"Usage: ReferenceFunctionalProbe <explicit-vst3-path> <output-folder> [config.json]\n";return 2;}
        const juce::File module(juce::String::fromUTF8(argv[1])),out(juce::String::fromUTF8(argv[2]));
        if(!module.exists())throw std::runtime_error("Reference file missing");
        out.createDirectory();
        juce::var config=object();if(argc>3)config=juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[3])).loadFileAsString());
        const double sr=double(config.getProperty("sampleRate",48000)),bpm=double(config.getProperty("bpm",120));
        constexpr int blockSize=256;
        juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions,module.getFullPathName());
        if(descriptions.isEmpty())throw std::runtime_error("Named VST3 exposes no loadable audio plugin");
        juce::String error;auto plugin=format.createInstanceFromDescription(*descriptions[0],sr,blockSize,error);
        if(!plugin)throw std::runtime_error(error.toStdString());
        const auto stateFile=config.getProperty("stateFile",juce::String{}).toString();
        if(stateFile.isNotEmpty()){
            juce::MemoryBlock state;if(!juce::File(stateFile).loadFileAsData(state))throw std::runtime_error("Named reference state missing");
            plugin->setStateInformation(state.getData(),int(state.getSize()));
        }
        // Public preset-state experiment only: replace user-editable settings in
        // the JUCE component-state envelope. Never reads executable internals.
        const auto settingsXml=config.getProperty("settingsXml",juce::String{}).toString();
        if(settingsXml.isNotEmpty()){
            const auto xml=juce::XmlDocument::parse(juce::File(settingsXml));
            if(!xml)throw std::runtime_error("Named settings XML cannot be parsed");
            const auto tree=juce::ValueTree::fromXml(*xml);
            if(!tree.isValid()||tree.getType().toString()!="PluginState")throw std::runtime_error("Unexpected public settings tree");
            juce::MemoryOutputStream raw;tree.writeToStream(raw);
            juce::MemoryOutputStream component;component.write("#zip#",6);
            {juce::GZIPCompressorOutputStream zipped(component,9,15);zipped.write(raw.getData(),raw.getDataSize());}
            juce::XmlElement wrapper("VST3PluginState");wrapper.createNewChildElement("IComponent")->addTextElement(component.getMemoryBlock().toBase64Encoding());
            juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(wrapper,state);
            plugin->setStateInformation(state.getData(),int(state.getSize()));
        }
        ProbeClock clock;clock.sampleRate=sr;clock.tempo=bpm;
        plugin->setPlayHead(&clock);plugin->setNonRealtime(true);
        bool layoutSupported=false,layoutApplied=false;
        if(plugin->getBusCount(true)>0&&plugin->getBusCount(false)>0){
            auto layout=plugin->getBusesLayout();layout.inputBuses.getReference(0)=juce::AudioChannelSet::stereo();
            layout.outputBuses.getReference(0)=juce::AudioChannelSet::stereo();
            for(int b=1;b<layout.inputBuses.size();++b)layout.inputBuses.getReference(b)=juce::AudioChannelSet::stereo();
            layoutSupported=plugin->checkBusesLayoutSupported(layout);
            if(layoutSupported)layoutApplied=plugin->setBusesLayout(layout);
        }
        if(auto* preset=config.getProperty("parameters",juce::var{}).getArray())for(const auto& row:*preset){
            const auto name=row.getProperty("name",juce::String{}).toString();const int requested=int(row.getProperty("index",-1));
            juce::AudioProcessorParameter* found=nullptr;
            for(auto* p:plugin->getParameters())if((name.isNotEmpty()&&p->getName(256)==name)||(requested>=0&&p->getParameterIndex()==requested)){found=p;break;}
            if(!found)throw std::runtime_error("Configuration parameter missing: "+name.toStdString());
            found->beginChangeGesture();found->setValueNotifyingHost(juce::jlimit(0.f,1.f,float(row.getProperty("value",0))));found->endChangeGesture();
        }
        juce::var metadata=object();put(metadata,"module",module.getFullPathName());put(metadata,"name",plugin->getName());
        put(metadata,"manufacturer",descriptions[0]->manufacturerName);put(metadata,"version",descriptions[0]->version);
        put(metadata,"sampleRate",sr);put(metadata,"bpm",bpm);put(metadata,"audioDeviceOpened",false);
        put(metadata,"requestedLayoutSupported",layoutSupported);put(metadata,"requestedLayoutApplied",layoutApplied);
        juce::Array<juce::var> buses;
        for(bool input:{true,false})for(int b=0;b<plugin->getBusCount(input);++b){
            auto* bus=plugin->getBus(input,b);auto row=object();put(row,"direction",input?"input":"output");put(row,"index",b);
            put(row,"name",bus->getName());put(row,"enabled",bus->isEnabled());
            put(row,"channels",bus->getNumberOfChannels());put(row,"layout",bus->getCurrentLayout().getDescription());buses.add(row);
        }
        put(metadata,"buses",buses);
        juce::Array<juce::var> parameters;
        for(auto* p:plugin->getParameters()){
            auto row=object();put(row,"index",p->getParameterIndex());put(row,"name",p->getName(256));put(row,"label",p->getLabel());
            put(row,"value",p->getValue());put(row,"text",p->getCurrentValueAsText());put(row,"steps",p->getNumSteps());
            if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(p))put(row,"id",identified->paramID);
            parameters.add(row);
        }
        put(metadata,"parameters",parameters);put(metadata,"inputChannels",plugin->getTotalNumInputChannels());put(metadata,"outputChannels",plugin->getTotalNumOutputChannels());
        if(bool(config.getProperty("showEditor",false))){
            out.getChildFile("reference-metadata.json").replaceWithText(juce::JSON::toString(metadata,true));
            plugin->setRateAndBufferSizeDetails(sr,blockSize);plugin->prepareToPlay(sr,blockSize);
            PublicPresetWindow window(*plugin,out,int(config.getProperty("editorSeconds",180)));
            juce::MessageManager::getInstance()->runDispatchLoop();
            plugin->releaseResources();
            std::cout<<"PASS public editor gesture states exported; no audio device opened\n";return 0;
        }
        if(bool(config.getProperty("exportState",false))){juce::MemoryBlock state;plugin->getStateInformation(state);out.getChildFile("reference-state.bin").replaceWithData(state.getData(),state.getSize());put(metadata,"stateBytes",int(state.getSize()));}
        if(bool(config.getProperty("listOnly",argc==3))){out.getChildFile("reference-metadata.json").replaceWithText(juce::JSON::toString(metadata,true));std::cout<<"PASS reference metadata "<<parameters.size()<<" parameters\n";return 0;}
        plugin->setRateAndBufferSizeDetails(sr,blockSize);plugin->prepareToPlay(sr,blockSize);
        const int warm=int(config.getProperty("warmupSamples",juce::roundToInt(sr*double(config.getProperty("warmupSeconds",2)))));
        const int total=int(config.getProperty("samples",juce::roundToInt(sr*double(config.getProperty("seconds",2)))));
        const int channels=juce::jmax(2,juce::jmax(plugin->getTotalNumInputChannels(),plugin->getTotalNumOutputChannels()));
        const auto signal=config.getProperty("signal","constant").toString();const bool sidechain=bool(config.getProperty("sidechain",false));
        const double frequency=double(config.getProperty("frequency",80));const float level=float(config.getProperty("level",.15));
        const int impulseSample=int(config.getProperty("impulseSample",warm+512));
        const auto inputAt=[&](int sample){
            if(signal=="tone")return level*float(std::sin(2*juce::MathConstants<double>::pi*frequency*sample/sr));
            if(signal=="impulse")return sample==impulseSample?level:0.f;
            return level;
        };
        juce::AudioBuffer<float> audio(channels,blockSize);juce::MidiBuffer midi;
        std::ofstream csv(out.getChildFile("reference-output.csv").getFullPathName().toStdString(),std::ios::binary);csv<<"sample,seconds,ppq,input,output\n";csv.precision(10);
        double minimum=1e9,maximum=-1e9,peak=0;int nonfinite=0;
        for(int start=0;start<warm+total;start+=blockSize){
            const int count=juce::jmin(blockSize,warm+total-start);audio.setSize(channels,count,false,false,true);audio.clear();midi.clear();
            if(auto* events=config.getProperty("midiEvents",juce::var{}).getArray())for(const auto& event:*events){
                const int at=int(event.getProperty("sample",0));if(at<start||at>=start+count)continue;
                const int note=int(event.getProperty("note",60)),channel=int(event.getProperty("channel",1));
                midi.addEvent(bool(event.getProperty("on",true))?juce::MidiMessage::noteOn(channel,note,float(event.getProperty("velocity",1))):juce::MidiMessage::noteOff(channel,note),at-start);
            }
            for(int i=0;i<count;++i){const double time=(start+i)/sr;const float input=inputAt(start+i);
                audio.setSample(0,i,input);audio.setSample(1,i,input);
                if(sidechain&&channels>=4){const double t=std::fmod(time,60/bpm);const float kick=float(.8*std::exp(-t*45)*std::sin(2*juce::MathConstants<double>::pi*60*t));audio.setSample(2,i,kick);audio.setSample(3,i,kick);}}
            clock.sample=start;plugin->processBlock(audio,midi);
            for(int i=0;i<count;++i)if(start+i>=warm){const auto n=start+i;const double time=n/sr;const float y=audio.getSample(0,i);const float input=inputAt(n);
                if(!std::isfinite(y))++nonfinite;minimum=juce::jmin(minimum,double(y));maximum=juce::jmax(maximum,double(y));peak=juce::jmax(peak,std::abs(double(y)));csv<<n-warm<<','<<time<<','<<time*bpm/60<<','<<input<<','<<y<<'\n';}
        }
        plugin->releaseResources();put(metadata,"latencySamples",plugin->getLatencySamples());put(metadata,"minimumOutput",minimum);put(metadata,"maximumOutput",maximum);put(metadata,"peakOutput",peak);put(metadata,"nonfiniteSamples",nonfinite);put(metadata,"config",config);
        out.getChildFile("reference-metadata.json").replaceWithText(juce::JSON::toString(metadata,true));
        if(nonfinite||!csv.good())throw std::runtime_error("Non-finite audio or output file error");
        std::cout<<"PASS actual reference VST3 rendered "<<total<<" samples; min "<<minimum<<" max "<<maximum<<" latency "<<plugin->getLatencySamples()<<"\n";
    } catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
    return 0;
}
