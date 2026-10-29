#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// Independent host: deliberately does NOT link OpenDuckProX. The supplied
// final bundle is loaded through JUCE's actual VST3 component/controller/view.
// A parent-process watchdog is REQUIRED: a blocked native call cannot be timed
// out by a timer on the same GUI thread. No audio device/preferences are used.
namespace
{
int passed = 0, failed = 0;
void check(bool ok, const juce::String& text)
{
    std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; ok ? ++passed : ++failed;
}
void step(const juce::String& text) { std::cout << "STEP " << text << '\n'; }
void pump(int milliseconds) { juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds); }

struct Pulse final : juce::Timer
{
    Pulse() { startTimer(10); }
    ~Pulse() override { stopTimer(); }
    void timerCallback() override { ++ticks; }
    int ticks = 0;
};
struct Window final : juce::DocumentWindow
{
    Window() : DocumentWindow("OpenDuck 0.3.1 isolated VST3 QA - closes automatically", juce::Colours::black,
                              DocumentWindow::closeButton, true)
    { setUsingNativeTitleBar(true); setResizable(true, false); }
    void closeButtonPressed() override { closedByUser = true; setVisible(false); }
    bool closedByUser = false;
};
struct Host final : juce::AudioProcessorListener
{
    explicit Host(juce::AudioPluginInstance& processor) : p(processor) { p.addListener(this); }
    ~Host() override { p.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details) override
    {
        if (!details.latencyChanged) return;
        const int index = traceCount.fetch_add(1);
        if (index < static_cast<int>(trace.size())) trace[static_cast<size_t>(index)].store(processor->getLatencySamples());
        pending.store(true);
    }
    bool prepare(double rate, int block, int expected, const juce::String& label)
    {
        int notifications = 0; bool allFinal = true, stable = false;
        const int initial = p.getLatencySamples();
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            pending.store(false); traceCount.store(0);
            step(label + " prepare " + juce::String(attempt));
            p.releaseResources(); p.setNonRealtime(true);
            p.setRateAndBufferSizeDetails(rate, block); p.prepareToPlay(rate, block);
            pump(40); // include queued component restarts, without re-entering prepare from a callback
            const int count = traceCount.load(); notifications += count;
            std::cout << "TRACE " << label << " final=" << p.getLatencySamples() << " notifications=[";
            for (int i = 0; i < count && i < static_cast<int>(trace.size()); ++i)
            { if (i) std::cout << ','; const int v = trace[static_cast<size_t>(i)].load(); std::cout << v; allFinal &= v == expected; }
            std::cout << "]\n";
            allFinal &= count <= static_cast<int>(trace.size());
            if (!pending.load()) { stable = true; break; }
        }
        return stable && allFinal && p.getLatencySamples() == expected && notifications <= (initial == expected ? 0 : 1);
    }
    juce::AudioPluginInstance& p;
    std::array<std::atomic<int>, 64> trace{};
    std::atomic<int> traceCount{0};
    std::atomic<bool> pending{false};
};

std::unique_ptr<juce::XmlElement> componentXml(const juce::MemoryBlock& state)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    if (xml && xml->hasTagName("VST3PluginState"))
    {
        auto* component = xml->getChildByName("IComponent"); juce::MemoryBlock inner;
        if (!component || !inner.fromBase64Encoding(component->getAllSubText())) return {};
        xml = juce::AudioProcessor::getXmlFromBinary(inner.getData(), static_cast<int>(inner.getSize()));
    }
    return xml;
}
juce::MemoryBlock historicalState(const juce::File& fixtures, int schema)
{
    juce::MemoryBlock state;
    if (!fixtures.getChildFile(schema == 1 ? "legacy-v0.1.0-init.odpx" : "legacy-v0.2.0-init.odpx").loadFileAsData(state))
        throw std::runtime_error("Authentic historical fixture missing");
    auto inner = componentXml(state);
    if (!inner || !inner->hasTagName("OpenDuckState") || inner->getIntAttribute("schema", 1) != schema)
        throw std::runtime_error("Historical fixture is not the requested actual processor schema");
    auto outer = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    if (outer && outer->hasTagName("VST3PluginState")) return state;
    // A historical raw processor chunk is wrapped in JUCE's documented host
    // state container. The archived component bytes are preserved unchanged.
    juce::XmlElement wrapper("VST3PluginState");
    wrapper.createNewChildElement("IComponent")->addTextElement(state.toBase64Encoding());
    juce::MemoryBlock wrapped; juce::AudioProcessor::copyXmlToBinary(wrapper, wrapped); return wrapped;
}
std::vector<float> parameters(juce::AudioPluginInstance& p)
{
    std::vector<float> result; for (auto* parameter : p.getParameters()) result.push_back(parameter->getValue()); return result;
}
bool sameParameters(juce::AudioPluginInstance& p, const std::vector<float>& expected)
{
    const auto actual = parameters(p); if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i) if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i]) > 1e-5f) return false;
    return true;
}
juce::String geometry(const juce::MemoryBlock& state)
{
    auto xml = componentXml(state); if (!xml) return {};
    juce::String result;
    for (const auto* name : {"Curves", "Bands3", "ShapeMemories3"})
    { auto* child = xml->getChildByName(name); if (!child) return {}; result += child->toString(); }
    return result;
}
bool validRouting(juce::AudioPluginInstance& p, bool modern)
{
    juce::MemoryBlock state; p.getStateInformation(state); auto xml = componentXml(state);
    return xml && xml->hasTagName("OpenDuckState") && xml->getIntAttribute("schema", 0) == 4 &&
           xml->getIntAttribute("revision3Mask", -1) == (modern ? 63 : 0) &&
           xml->getStringAttribute("audioRouting") == (modern ? "serial-0.3" : "legacy-budget");
}
bool renderFinite(juce::AudioPluginInstance& p, int block)
{
    juce::MidiBuffer midi; bool valid = true;
    const int channels = juce::jmax(2, juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    for (int n = 0; n < 8; ++n)
    {
        juce::AudioBuffer<float> audio(channels, block); audio.clear();
        for (int c = 0; c < juce::jmin(2, channels); ++c)
            for (int i = 0; i < block; ++i) audio.setSample(c, i, .1f * std::sin((n * block + i) * .05f));
        p.processBlock(audio, midi);
        for (int c = 0; c < juce::jmin(2, channels); ++c)
            for (int i = 0; i < block; ++i) valid &= std::isfinite(audio.getSample(c, i));
    }
    return valid;
}
bool hasEmbeddedHwnd(juce::Component& component)
{
   #if JUCE_WINDOWS
    if (auto* embedded = dynamic_cast<juce::HWNDComponent*>(&component)) if (embedded->getHWND() != nullptr) return true;
   #endif
    for (auto* child : component.getChildren()) if (hasEmbeddedHwnd(*child)) return true;
    return false;
}

template<class Factory>
void scenario(int schema, Factory&& create, const juce::File& fixtures, Window& window, Pulse& pulse)
{
    const bool modern=schema>=3, measured=schema==4;
    const juce::String label = measured?"measured4-SYNC-zeroPDC":schema == 3 ? "legacy-analytic4" : "historical" + juce::String(schema);
    step(label + " create component"); auto p = create();
    if(schema==3){for(auto* parameter:p->getParameters())if(parameter->getName(128)=="profileModel4")parameter->setValueNotifyingHost(0);pump(40);}
    if (!modern)
    {
        const auto state = historicalState(fixtures, schema);
        p->setStateInformation(state.getData(), static_cast<int>(state.getSize())); pump(40);
    }
    check(validRouting(*p, modern), label + " actual component state restores the intended audio branch");
    Host host(*p);
    // JUCE's VST3 factory ignores createPluginInstance's sample-rate/block
    // arguments. Its first prepare reads the component's existing latency
    // before setActive() prepares that component at the requested sample rate.
    // Activate at the declared creation rate explicitly, measuring this real
    // bootstrap with the SAME strict listener/bounded-convergence check. Then
    // the four configurations below measure actual rate changes from a prepared
    // host, rather than combining bootstrap and a 48k -> 44.1k transition.
    // No notification is discarded and the existing79 assertions stay intact.
    check(host.prepare(48000, 256, measured?0:modern ? 12112 : 1152, label + " bootstrap 48000/256"),
          label + " actual first VST3 activation publishes the correct initial latency and settles");
    struct Config { double rate; int block, modernPdc; };
    const std::array<Config, 4> configs{{{44100, 64, 11274}, {48000, 257, 12112}, {96000, 1024, 22432}, {48000, 256, 12112}}};
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        for (const auto config : configs)
        {
            const auto test = label + " cycle" + juce::String(cycle) + " " + juce::String(config.rate) + "/" + juce::String(config.block);
            check(host.prepare(config.rate, config.block, measured?0:modern ? config.modernPdc : 1152, test),
                  test + " VST3 latency restart settles without a prepare loop");
            check(renderFinite(*p, config.block), test + " processes finite audio through the loaded VST3");
        }
        const auto expected = parameters(*p);
        juce::MemoryBlock before; p->getStateInformation(before); const auto beforeGeometry = geometry(before);
        const int ticks = pulse.ticks;
        step(label + " create/attach native editor " + juce::String(cycle));
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorIfNeeded());
        if (!editor) throw std::runtime_error("Actual VST3 editor unavailable");
        struct DetachOnExit { Window& w; ~DetachOnExit() { w.clearContentComponent(); } } detach{window};
        window.setContentNonOwned(editor.get(), true); window.centreWithSize(1200, 650); window.setVisible(true);
        editor->setVisible(true); editor->repaint(); pump(150);
        const auto* peer = window.getPeer();
        check(!window.closedByUser && window.isOnDesktop() && peer && peer->getNativeHandle() &&
                  editor->getPeer() && editor->isShowing() && hasEmbeddedHwnd(*editor) && pulse.ticks > ticks + 2,
              label + " actual VST3 view attaches to a live native HWND peer and GUI timers continue");
        for (const auto size : {juce::Point<int>{1050, 600}, juce::Point<int>{1320, 740}})
        {
            step(label + " native resize " + juce::String(size.x)); const int heartbeat = pulse.ticks;
            window.setSize(size.x, size.y); window.repaint(); editor->repaint(); pump(100);
            check(editor->getWidth() > 0 && editor->getHeight() > 0 && editor->isShowing() && pulse.ticks > heartbeat,
                  label + " native peer resize returns and keeps the host message loop responsive");
        }
        step(label + " detach/destroy editor");
        window.clearContentComponent(); editor->setVisible(false); editor.reset(); pump(60);
        juce::MemoryBlock after; p->getStateInformation(after);
        check(p->getActiveEditor() == nullptr && sameParameters(*p, expected) && !beforeGeometry.isEmpty() && geometry(after) == beforeGeometry,
              label + " native detach/editor recreation preserves processor parameters and curve geometry");
    }
    juce::MemoryBlock saved; p->getStateInformation(saved); const auto expected = parameters(*p); const auto savedGeometry = geometry(saved);
    step(label + " restore into a second actual VST3 component"); auto restored = create();
    restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize())); pump(40);
    Host restoredHost(*restored);
    const bool settled = restoredHost.prepare(48000, 256, measured?0:modern ? 12112 : 1152, label + " restored");
    juce::MemoryBlock roundtrip; restored->getStateInformation(roundtrip);
    check(settled && validRouting(*restored, modern) && sameParameters(*restored, expected) &&
              !savedGeometry.isEmpty() && geometry(roundtrip) == savedGeometry,
          label + " actual wrapper save/reload restores audio routing and state without a latency restart loop");
    restored->releaseResources(); p->releaseResources();
}
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3 || argc > 4) { std::cerr << "Usage: Revision031Vst3LifecycleTests <explicit bundle> <fixture directory> [expected version]\n"; return 2; }
    try
    {
        const juce::File bundle(juce::String::fromUTF8(argv[1])), fixtures(juce::String::fromUTF8(argv[2]));
        if (!bundle.exists() || !fixtures.isDirectory()) throw std::runtime_error("Explicit bundle/fixture directory missing");
        step("scan explicit module " + bundle.getFullPathName());
        juce::AudioPluginFormatManager formats; auto* format = new juce::VST3PluginFormat(); formats.addFormat(format);
        juce::OwnedArray<juce::PluginDescription> descriptions; format->findAllTypesForFile(descriptions, bundle.getFullPathName());
        if (descriptions.size() != 1) throw std::runtime_error("Expected exactly one VST3 component in explicit bundle");
        const juce::String version = argc == 4 ? juce::String::fromUTF8(argv[3]) : "0.4.1";
        check(descriptions[0]->name=="Open Duck" && descriptions[0]->version == version,
              "explicit-path loader identifies the requested actual OpenDuck VST3 version");
        auto create = [&]() { juce::String error; auto p = formats.createPluginInstance(*descriptions[0], 48000, 256, error);
            if (!p) throw std::runtime_error("VST3 instantiation failed: " + error.toStdString()); return p; };
        Window window; Pulse pulse;
        scenario(4, create, fixtures, window, pulse);scenario(3, create, fixtures, window, pulse); scenario(1, create, fixtures, window, pulse); scenario(2, create, fixtures, window, pulse);
        {
            class Position final:public juce::AudioPlayHead {public:double ppq=0;juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;p.setBpm(120);p.setPpqPosition(ppq);p.setIsPlaying(true);return p;}} clockA,clockB;
            auto first=create(),second=create();first->setPlayHead(&clockA);second->setPlayHead(&clockB);juce::AudioProcessorParameter *position=nullptr,*duration=nullptr;
            // New INIT is intentionally neutral. Explicit Power ON is part of
            // this automation test; it must measure the audible envelope.
            for(auto* parameter:first->getParameters())if(parameter->getName(128)=="bypass")parameter->setValueNotifyingHost(0);
            for(auto* parameter:second->getParameters()){if(parameter->getName(128)=="bypass")parameter->setValueNotifyingHost(0);if(parameter->getName(128)=="MIX")parameter->setValueNotifyingHost(.5f);}
            bool legacyID=false;for(auto* parameter:first->getParameters()){if(parameter->getName(128)=="MIX")position=parameter;if(parameter->getName(128)=="PHASE Duration")duration=parameter;if(parameter->getName(128)=="MIX Position")legacyID=true;}
            check(position&&duration&&legacyID,"actual VST3 publishes MIX depth and PHASE Duration while retaining the historical MIX Position automation ID");
            if(position&&duration){Host a(*first),b(*second);bool prepared=a.prepare(48000,256,0,"automation4 A")&&b.prepare(48000,256,0,"automation4 B");
                juce::AudioBuffer<float> x(2,256),y(2,256);juce::MidiBuffer events;double difference=0;int measured=0;bool reflected=true;
                for(int block=0;block<512;++block){clockA.ppq=clockB.ppq=block*256.0/48000*2;const float value=block%64<32?.25f:.75f;position->setValueNotifyingHost(value);duration->setValueNotifyingHost(block%128<64?.3f:.7f);reflected&=std::abs(position->getValue()-value)<1e-5;
                    for(int c=0;c<2;++c)for(int i=0;i<256;++i){const float input=.2f*std::sin(float((block*256+i)*.017));x.setSample(c,i,input);y.setSample(c,i,input);}first->processBlock(x,events);second->processBlock(y,events);
                    if(block>64)for(int i=0;i<256;++i){const double delta=x.getSample(0,i)-y.getSample(0,i);difference+=delta*delta;++measured;}}
                check(prepared&&reflected,"512 actual wrapper automation blocks reflect the requested MIX values");
                const double rms=std::sqrt(difference/juce::jmax(1,measured));std::cout<<"MEASURE wrapper automation residual RMS "<<rms<<std::endl;
                check(std::isfinite(rms)&&rms>.005,"new MIX/PHASE host automation changes the real loaded VST3 audio independently of a second instance");
                first->releaseResources();second->releaseResources();
            }
        }
        window.setVisible(false); window.clearContentComponent();
    }
    catch (const std::exception& error) { std::cout << "FAIL VST3 lifecycle exception: " << error.what() << '\n'; ++failed; }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}

