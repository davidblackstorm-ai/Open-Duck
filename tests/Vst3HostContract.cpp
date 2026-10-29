#include <JuceHeader.h>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// This executable deliberately does not link OpenDuckProX. It loads the produced VST3 module
// through JUCE's VST3 host wrapper, with no audio device and no installation or plugin scan path.
namespace
{
constexpr double sampleRate = 48000;
constexpr int blockSize = 256;
constexpr float inputLevel = .15f;
constexpr int revision3Latency = 10320 + 6 * 128 + 1024; // 48kHz: bounded offset/lookahead + FIR + STFT.

class HostPosition final : public juce::AudioPlayHead
{
  public:
    double tempo = 120, ppq = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setBpm(tempo);
        result.setPpqPosition(ppq);
        result.setIsPlaying(playing);
        result.setTimeSignature(TimeSignature{4, 4});
        return result;
    }
};

juce::AudioProcessorParameter &parameter(juce::AudioPluginInstance &instance, const juce::String &name)
{
    for (auto *p : instance.getParameters())
        if (p->getName(128) == name)
            return *p;
    throw std::runtime_error("VST3 parameter missing: " + name.toStdString());
}
void set(juce::AudioPluginInstance &instance, const juce::String &name, float normalised)
{
    parameter(instance, name).setValueNotifyingHost(normalised);
}
void resetAudio(juce::AudioPluginInstance &instance, HostPosition &host)
{
    instance.releaseResources();
    instance.setPlayHead(&host);
    instance.setNonRealtime(true);
    instance.setRateAndBufferSizeDetails(sampleRate, blockSize);
    instance.prepareToPlay(sampleRate, blockSize);
}
void ducking(juce::AudioPluginInstance &instance)
{
    for (int k = 0; k < 6; ++k)
    {
        set(instance, "on" + juce::String(k), k == 0 ? 1.0f : 0.0f);
        set(instance, "solo" + juce::String(k), 0);
    }
    set(instance, "bypass", 0);
    set(instance, "mix", 1);
    set(instance, "sync", 1);
    set(instance, "trigger", 0);
    set(instance, "v3Trigger0", 0);
    set(instance, "v3SpeedMode0", 0);
    set(instance, "v3Speed0", 2.f / 19.f); // quarter note, independent of editor snap.
    set(instance, "v3Smooth0", 0);
    set(instance, "curveShape0", 0);
    set(instance, "curveAmount0", 1);
    set(instance, "curveAttack0", 0);
    set(instance, "curveRelease0", (150.f - 5.f) / 1995.f);
    set(instance, "curvePhase0", .5f);
    set(instance, "curveTension0", .5f);
    set(instance, "curveLength0", 1);
    set(instance, "curveGrid0", 0);
    set(instance, "curveBand0", 0);
}
std::vector<float> render(juce::AudioPluginInstance &instance, HostPosition &host, double seconds,
                          bool kick = false, bool note = false, bool advance = true)
{
    const int total = (int)std::ceil(seconds * sampleRate);
    const double startPpq = host.ppq;
    const int channels =
        juce::jmax(2, juce::jmax(instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels()));
    juce::AudioBuffer<float> audio(channels, blockSize);
    std::vector<float> result;
    result.reserve((size_t)total);
    const int samplesPerBeat = (int)std::round(sampleRate * 60 / host.tempo);
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int count = juce::jmin(blockSize, total - offset);
        audio.setSize(channels, count, false, false, true);
        audio.clear();
        for (int i = 0; i < count; ++i)
        {
            audio.setSample(0, i, inputLevel);
            audio.setSample(1, i, inputLevel);
            if (kick && channels >= 4)
            {
                const float value = ((offset + i) % samplesPerBeat) < 96 ? .7f : 0;
                audio.setSample(2, i, value);
                audio.setSample(3, i, value);
            }
        }
        host.ppq = startPpq + (advance && host.playing ? offset * host.tempo / (60 * sampleRate) : 0);
        juce::MidiBuffer midi;
        if (note && offset == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 37);
        instance.processBlock(audio, midi);
        result.insert(result.end(), audio.getReadPointer(0), audio.getReadPointer(0) + count);
    }
    host.ppq = startPpq + (advance && host.playing ? total * host.tempo / (60 * sampleRate) : 0);
    return result;
}
double rms(const std::vector<float> &audio, int start = 2000, int end = -1)
{
    end = end < 0 ? (int)audio.size() : juce::jmin(end, (int)audio.size());
    start = juce::jlimit(0, end, start);
    double energy = 0;
    for (int i = start; i < end; ++i)
        energy += (double)audio[(size_t)i] * audio[(size_t)i];
    return std::sqrt(energy / juce::jmax(1, end - start));
}
bool finite(const std::vector<float> &audio)
{
    for (const auto value : audio)
        if (!std::isfinite(value) || std::abs(value) > 1)
            return false;
    return true;
}
int ducks(const std::vector<float> &audio)
{
    int count = 0;
    bool armed = false;
    for (size_t i = 8000; i < audio.size(); ++i)
    {
        if (audio[i] > inputLevel * .8f)
            armed = true;
        if (armed && audio[i] < inputLevel * .3f)
        {
            ++count;
            armed = false;
        }
    }
    return count;
}
std::vector<float> parameters(juce::AudioPluginInstance &instance)
{
    std::vector<float> result;
    for (auto *p : instance.getParameters())
        result.push_back(p->getValue());
    return result;
}
bool sameParameters(juce::AudioPluginInstance &instance, const std::vector<float> &expected)
{
    const auto current = parameters(instance);
    if (current.size() != expected.size())
        return false;
    for (size_t k = 0; k < current.size(); ++k)
        if (std::abs(current[k] - expected[k]) > .00001f)
            return false;
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 2 && argc != 4)
    {
        std::cerr << "Usage: OpenDuckHostContract <explicit VST3 bundle path> [--export-init <file>]\n";
        return 2;
    }
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *description)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << description << "\n";
        ok ? ++passed : ++failed;
    };
    try
    {
        const juce::File file(juce::String::fromUTF8(argv[1]));
        if (!file.exists())
            throw std::runtime_error("Specified VST3 path does not exist");
        juce::AudioPluginFormatManager formats;
        auto *format = new juce::VST3PluginFormat();
        formats.addFormat(format);
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format->findAllTypesForFile(descriptions, file.getFullPathName());
        if (descriptions.isEmpty())
            throw std::runtime_error("No VST3 component found at explicit path");
        auto create = [&]()
        {
            juce::String error;
            auto result = formats.createPluginInstance(*descriptions[0], sampleRate, blockSize, error);
            if (!result)
                throw std::runtime_error("VST3 creation failed: " + error.toStdString());
            return result;
        };
        HostPosition host;
        auto instance = create();
        if (argc == 4)
        {
            if (juce::String::fromUTF8(argv[2]) != "--export-init")
                throw std::runtime_error("Unknown export option");
            juce::MemoryBlock initial;
            instance->getStateInformation(initial);
            const juce::File destination(juce::String::fromUTF8(argv[3]));
            if (!destination.replaceWithData(initial.getData(), initial.getSize()))
                throw std::runtime_error("Could not export actual loaded binary INIT state");
            std::cout << "EXPORTED INIT from " << descriptions[0]->version << " at " << file.getFullPathName() << "\n";
            return 0;
        }
        ducking(*instance);
        resetAudio(*instance, host);
        check(instance->getName().contains("OpenDuck") && instance->getParameters().size() >= 131,
              "explicit-path loader creates the produced VST3 binary and its full parameter interface");
        check(instance->getLatencySamples() == revision3Latency, "VST3 wrapper reports the revision3 bounded 12112-sample PDC");

        host.ppq = 0;
        host.tempo = 120;
        auto slow = render(*instance, host, 2.1);
        resetAudio(*instance, host);
        host.ppq = 0;
        host.tempo = 180;
        auto fast = render(*instance, host, 2.1);
        std::cout << "Actual VST3 host tempo duck counts 120=" << ducks(slow) << " 180=" << ducks(fast)
                  << "\n";
        check(finite(slow) && finite(fast) && ducks(slow) >= 2 && ducks(fast) > ducks(slow),
              "real VST3 process context carries host BPM and changes the audible cycle rate");
        host.playing = false;
        const auto stopped = render(*instance, host, .3);
        check(rms(stopped, revision3Latency + 1024) > inputLevel * .97,
              "host transport stop releases ducking through the VST3 wrapper");
        host.playing = true;
        host.tempo = 120;
        resetAudio(*instance, host);
        host.ppq = 0;
        const auto early = render(*instance, host, revision3Latency / sampleRate + .08);
        resetAudio(*instance, host);
        host.ppq = .75;
        const auto seek = render(*instance, host, revision3Latency / sampleRate + .08);
        check(rms(seek, revision3Latency + 1800) > rms(early, revision3Latency + 1800) * 2,
              "host PPQ seek changes the gain envelope in the loaded VST3");

        instance->releaseResources();
        auto layout = instance->getBusesLayout();
        bool sidechain = layout.inputBuses.size() > 1;
        if (sidechain)
        {
            layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
            sidechain = instance->setBusesLayout(layout);
        }
        check(sidechain, "loaded VST3 negotiates its real stereo sidechain bus");
        set(*instance, "v3Trigger0", 1.f / 3.f);
        set(*instance, "curveRelease0", (150.0f - 5) / 1995);
        host.ppq = 0;
        resetAudio(*instance, host);
        const auto silent = render(*instance, host, .5);
        resetAudio(*instance, host);
        host.ppq = 0;
        const auto kick = render(*instance, host, .5, true);
        check(sidechain && finite(kick) && rms(silent, revision3Latency + 1500, revision3Latency + 6500) > inputLevel * .98 &&
                  rms(kick, revision3Latency + 1500, revision3Latency + 6500) < rms(silent, revision3Latency + 1500, revision3Latency + 6500) * .9,
              "real VST3 sidechain audio triggers ducking while a silent sidechain does not");
        set(*instance, "v3Trigger0", 2.f / 3.f);
        resetAudio(*instance, host);
        host.ppq = 0;
        const auto noMidi = render(*instance, host, .5);
        resetAudio(*instance, host);
        host.ppq = 0;
        const auto midi = render(*instance, host, .5, false, true);
        check(instance->acceptsMidi() && rms(noMidi, revision3Latency + 1500, revision3Latency + 6500) > inputLevel * .98 &&
                  rms(midi, revision3Latency + 1500, revision3Latency + 6500) < rms(noMidi, revision3Latency + 1500, revision3Latency + 6500) * .9,
              "VST3 event bus carries a sample-offset MIDI note and does not trigger without it");

        for (int k = 0; k < 6; ++k)
        {
            set(*instance, "curveShape" + juce::String(k), (float)(k + 1) / 16);
            set(*instance, "curvePhase" + juce::String(k), .1f + .14f * (float)k);
            set(*instance, "curveAmount" + juce::String(k), .25f + .1f * (float)k);
        }
        set(*instance, "selected", .8f);
        render(*instance, host, .03);
        const auto expected = parameters(*instance);
        juce::MemoryBlock state;
        instance->getStateInformation(state);
        auto restored = create();
        restored->setStateInformation(state.getData(), (int)state.getSize());
        resetAudio(*restored, host);
        render(*restored, host, .03);
        check(state.getSize() > 100 && sameParameters(*restored, expected),
              "VST3 component/controller state restores six distinct curve slots in a fresh binary instance");
        bool editors = true;
        for (int n = 0; n < 3; ++n)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(restored->createEditorIfNeeded());
            editors &= editor != nullptr;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        check(editors && sameParameters(*restored, expected),
              "three VST3 editor recreation cycles preserve the loaded session rather than applying INIT");

        std::array<HostPosition, 8> positions;
        std::array<std::unique_ptr<juce::AudioPluginInstance>, 8> instances;
        bool independent = true;
        for (size_t k = 0; k < instances.size(); ++k)
        {
            instances[k] = create();
            ducking(*instances[k]);
            for (int engine = 0; engine < 6; ++engine)
                set(*instances[k], "on" + juce::String(engine), 1);
            set(*instances[k], "curveAmount0", .1f + .1f * (float)k);
            resetAudio(*instances[k], positions[k]);
            independent &= finite(render(*instances[k], positions[k], .5));
        }
        for (size_t k = 0; k < instances.size(); ++k)
            independent &= std::abs(parameter(*instances[k], "curveAmount0").getValue() -
                                    (.1f + .1f * (float)k)) < .00001f;
        check(independent,
              "eight independently loaded VST3 instances process six engines with separate parameter states");
        for (auto &plugin : instances)
            plugin->releaseResources();
        restored->releaseResources();
        instance->releaseResources();
    }
    catch (const std::exception &error)
    {
        std::cerr << "HOST CONTRACT ERROR " << error.what() << "\n";
        ++failed;
    }
    std::cout << "HOST CONTRACT RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
