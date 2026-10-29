// Native editor evidence from the dedicated real LAB WAV. No device or DAW.
#include "../tests/NewQualitySupport.h"
#include "../Source/PluginEditor.h"

juce::Component* named(juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren()) if (auto* found = named(*child, name)) return found;
    return nullptr;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        if (argc != 3) throw std::runtime_error("Arguments: dedicated kick WAV, existing/new preview directory");
        const juce::File source(juce::String::fromUTF8(argv[1])), output(juce::String::fromUTF8(argv[2]));
        if (!output.createDirectory().wasOk()) throw std::runtime_error("Cannot create preview directory");
        const auto kick = load(source);
        auto owner = std::make_unique<OpenDuckProcessor>(); auto& processor = *owner;
        Head head; processor.setPlayHead(&head);
        auto layout = processor.getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
        if (!processor.setBusesLayout(layout)) throw std::runtime_error("Cannot enable real stereo sidechain");
        for (int mode = 0; mode < 6; ++mode) { set(processor, "on" + juce::String(mode), mode == 0 ? 1.f : 0.f); set(processor, "solo" + juce::String(mode), 0); }
        set(processor, "selected", 0); set(processor, "mix", 100);
        processor.state.state.setProperty("editorScopeSource", 2, nullptr);
        processor.state.state.setProperty("editorScopeZoom", 1, nullptr);
        processor.prepareToPlay(kick.rate, 257);
        juce::MidiBuffer midi;
        bool completed = false;
        for (int start = 0; start < (int)kick.v[0].size(); start += 257)
        {
            const int count = juce::jmin(257, (int)kick.v[0].size() - start);
            juce::AudioBuffer<float> audio(4, count);
            for (int channel = 0; channel < 2; ++channel) for (int sample = 0; sample < count; ++sample)
            {
                const auto value = kick.v[(size_t)channel][(size_t)(start + sample)];
                audio.setSample(channel, sample, value);
                audio.setSample(channel + 2, sample, value);
            }
            head.ppq = start / kick.rate * 145.0 / 60.0;
            processor.processBlock(audio, midi);
            const auto frame = processor.getScopeFrame(0);
            if (frame.complete && frame.bins == 512 && frame.generation >= 2) { completed = true; break; }
        }
        if (!completed) throw std::runtime_error("Real WAV did not produce a complete native scope cycle");
        processor.setScopeFrozen(true);
        const auto frame = processor.getScopeFrame(0);
        float peak = 0; for (int i = 0; i < 512; ++i) peak = std::max({peak, std::abs(frame.sideMin[(size_t)i]), std::abs(frame.sideMax[(size_t)i])});
        if (peak < 1.e-4f || std::abs(frame.periodSeconds - 60.0 / 145.0) > .001) throw std::runtime_error("Missing real kick or wrong one-quarter-note scope period");
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1767, 890); editor->resized();
        if (auto* sourceControl = dynamic_cast<juce::ComboBox*>(named(*editor, "scopeSource")))
        {
            sourceControl->setSelectedId(1, juce::sendNotificationSync);
            sourceControl->setSelectedId(2, juce::sendNotificationSync);
        }
        if (auto* mode = dynamic_cast<juce::Button*>(named(*editor, "mode0"))) if (mode->onClick) mode->onClick();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        const auto destination = output.getChildFile("OpenDuck-Pro-X-Ducking-Single-Kick-Native.png");
        juce::TemporaryFile temporary(destination); auto stream = temporary.getFile().createOutputStream(); juce::PNGImageFormat png;
        if (stream == nullptr || !stream->setPosition(0) || !stream->truncate().wasOk() || !png.writeImageToStream(image, *stream)) throw std::runtime_error("Cannot write native preview PNG");
        stream->flush(); if (!stream->getStatus().wasOk()) throw std::runtime_error("Native preview PNG I/O failure"); stream.reset();
        if (!temporary.overwriteTargetFileWithTemporary()) throw std::runtime_error("Cannot commit native preview PNG");
        juce::DynamicObject::Ptr metadata = new juce::DynamicObject();
        metadata->setProperty("scope", "Native editor preview from the dedicated real LAB kick WAV; no DAW or AI quality result inferred.");
        metadata->setProperty("fixture", source.getFullPathName());
        metadata->setProperty("sampleRate", kick.rate); metadata->setProperty("scopeBins", frame.bins);
        metadata->setProperty("scopePeriodSeconds", frame.periodSeconds); metadata->setProperty("sidechainPeak", peak);
        metadata->setProperty("frozen", true); metadata->setProperty("mode", "Ducking");
        metadata->setProperty("pointsEnabled", processor.editPointsEnabled(0));
        metadata->setProperty("aiState", (int)processor.localAssistStatus(0).state);
        metadata->setProperty("png", destination.getFullPathName());
        output.getChildFile("single-kick-native-preview.json").replaceWithText(juce::JSON::toString(juce::var(metadata.get()), true));
        std::cout << "PASS real stereo kick captured in native 512-bin scope, " << frame.periodSeconds << " seconds\n";
        std::cout << "PNG " << destination.getFullPathName() << "\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
