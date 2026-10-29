#include "../Source/PluginProcessor.h"
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

// Actual processor lifecycle only. No audio device/editor/DAW is launched.
// The host coalesces latency restarts and reparses preparation non-recursively;
// eight attempts bound a broken plugin instead of hanging the test process.
namespace
{
int passed = 0, failed = 0;
void check(bool ok, const juce::String& name)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    ok ? ++passed : ++failed;
}
void set(OpenDuckProcessor& p, const char* id, float value)
{
    auto* parameter = p.state.getParameter(id);
    if (parameter == nullptr) throw std::runtime_error("Missing public parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct Host final : juce::AudioProcessorListener
{
    explicit Host(OpenDuckProcessor& processor) : p(processor) { p.addListener(this); }
    ~Host() override { p.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details) override
    {
        if (!details.latencyChanged) return;
        if (traceCount < static_cast<int>(trace.size())) trace[static_cast<size_t>(traceCount)] = processor->getLatencySamples();
        ++traceCount;
        pending = true;
    }
    struct Result { bool stable = false, onlyFinalLatencyReported = true; int notifications = 0, attempts = 0; };
    Result prepare(double rate, int block, int expected, const juce::String& label)
    {
        Result result;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            pending = false; traceCount = 0;
            const int before = p.getLatencySamples();
            p.releaseResources();
            p.setRateAndBufferSizeDetails(rate, block);
            p.prepareToPlay(rate, block);
            ++result.attempts; result.notifications += traceCount;
            std::cout << "TRACE " << label << " attempt=" << attempt << " before=" << before
                      << " final=" << p.getLatencySamples() << " notifications=[";
            for (int i = 0; i < traceCount && i < static_cast<int>(trace.size()); ++i)
            {
                if (i) std::cout << ',';
                std::cout << trace[static_cast<size_t>(i)];
                result.onlyFinalLatencyReported &= trace[static_cast<size_t>(i)] == expected;
            }
            result.onlyFinalLatencyReported &= traceCount <= static_cast<int>(trace.size());
            std::cout << "]\n";
            if (!pending) { result.stable = true; break; }
        }
        return result;
    }
    OpenDuckProcessor& p;
    std::array<int, 64> trace{};
    int traceCount = 0;
    bool pending = false;
};

bool loadHistorical(OpenDuckProcessor& p, const juce::File& folder, int schema)
{
    juce::MemoryBlock binary;
    const auto file = folder.getChildFile(schema == 1 ? "legacy-v0.1.0-init.odpx" : "legacy-v0.2.0-init.odpx");
    if (!file.loadFileAsData(binary)) return false;
    auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    if (xml && xml->hasTagName("VST3PluginState"))
    {
        auto* component = xml->getChildByName("IComponent");
        juce::MemoryBlock chunk;
        if (!component || !chunk.fromBase64Encoding(component->getAllSubText())) return false;
        xml = juce::AudioProcessor::getXmlFromBinary(chunk.getData(), static_cast<int>(chunk.getSize()));
    }
    if (!xml || xml->getIntAttribute("schema", 1) != schema || !xml->hasTagName(p.state.state.getType())) return false;
    juce::AudioProcessor::copyXmlToBinary(*xml, binary);
    p.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    for (int mode = 0; mode < 6; ++mode) if (p.usesRevision3(mode)) return false;
    return true;
}

void impulseDelay(OpenDuckProcessor& p, int block, int expected, double rate)
{
    int first = -1; bool finite = true, otherSamplesZero = true; float peak = 0;
    juce::MidiBuffer midi;
    for (int offset = 0; offset < expected + block * 2; offset += block)
    {
        const int count = juce::jmin(block, expected + block * 2 - offset);
        juce::AudioBuffer<float> audio(2, count); audio.clear();
        if (offset == 0) { audio.setSample(0, 0, .25f); audio.setSample(1, 0, .25f); }
        p.processBlock(audio, midi);
        for (int i = 0; i < count; ++i)
        {
            const int position = offset + i;
            const float value = audio.getSample(0, i);
            finite &= std::isfinite(value) && std::isfinite(audio.getSample(1, i));
            if (std::abs(value) > 1e-6f && first < 0) first = position;
            if (position == expected) peak = value;
            else otherSamplesZero &= std::abs(value) < 1e-6f;
        }
    }
    std::cout << "MEASURE initial dry impulse sr=" << rate << " first=" << first << " reported="
              << p.getLatencySamples() << " expected=" << expected << " peak=" << peak << '\n';
    check(finite && first == expected && p.getLatencySamples() == expected && std::abs(peak - .25f) < 1e-6f && otherSamplesZero,
          "fresh revision3 prepared audio begins at its actual advertised PDC at " + juce::String(rate));
}

void scenario(int schema, const juce::File& fixtures)
{
    auto owner = std::make_unique<OpenDuckProcessor>(); auto& p = *owner;
    const juce::String label = schema == 3 ? "revision3" : "historical schema" + juce::String(schema);
    if (schema != 3)
    {
        const bool loaded = loadHistorical(p, fixtures, schema);
        check(loaded, label + " loads the authentic archived processor fixture");
        if (!loaded) return;
    }
    // MIX0 gives an unfiltered reference through the declared delay, making
    // the first-sample timing assertion independent of the ducking envelope.
    set(p, "mix", 0);
    Host host(p);
    struct Configuration { double rate; int modernLatency; };
    for (const auto config : {Configuration{44100, 11274}, Configuration{48000, 12112}, Configuration{96000, 22432}})
    {
        const int expected = schema == 3 ? config.modernLatency : 1152;
        const int before = p.getLatencySamples();
        auto result = host.prepare(config.rate, 257, expected, label + " rate=" + juce::String(config.rate));
        check(result.stable && result.onlyFinalLatencyReported && p.getLatencySamples() == expected &&
                  result.notifications <= (before == expected ? 0 : 1),
              label + " converges under latency-driven host reprepare without intermediate PDC at " + juce::String(config.rate));
        if (schema == 3) impulseDelay(p, 257, expected, config.rate);
        for (int block : {64, 257, 1024})
        {
            result = host.prepare(config.rate, block, expected, label + " unchangedPDC block=" + juce::String(block));
            check(result.stable && result.attempts == 1 && result.notifications == 0 && p.getLatencySamples() == expected,
                  label + " same-rate preparation and buffer change issue no latency restart (" + juce::String(config.rate) + "/" + juce::String(block) + ")");
        }
    }
    p.releaseResources();
}
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const auto fixtures = argc > 1 ? juce::File(juce::String::fromUTF8(argv[1]))
            : juce::File(juce::String::fromUTF8(__FILE__)).getParentDirectory().getChildFile("fixtures");
        scenario(3, fixtures); scenario(1, fixtures); scenario(2, fixtures);
    }
    catch (const std::exception& error) { std::cout << "FAIL lifecycle exception: " << error.what() << '\n'; ++failed; }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
