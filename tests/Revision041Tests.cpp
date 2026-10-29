#include "PluginProcessor.h"
#include "ScopeWaveformView.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr double sampleRate = 48000.;
constexpr int blockSize = 257;
int passes = 0, failures = 0;
juce::Array<juce::var> observations;

void check(bool value, const std::string& name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << std::endl;
    value ? ++passes : ++failures;
    auto* result = new juce::DynamicObject;
    result->setProperty("name", juce::String(name));
    result->setProperty("passed", value);
    observations.add(juce::var(result));
}

void set(OpenDuckProcessor& processor, const juce::String& id, float value) {
    auto* parameter = processor.state.getParameter(id);
    if (!parameter) throw std::runtime_error("Missing parameter: " + id.toStdString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

struct Clock final : juce::AudioPlayHead {
    int64_t sample = 0;
    double rate = sampleRate, tempo = 120.;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo result;
        result.setBpm(tempo);
        result.setPpqPosition(sample / rate * tempo / 60.);
        result.setTimeInSamples(sample);
        result.setIsPlaying(playing);
        return result;
    }
};

float framePeak(const OpenDuckProcessor::ScopeFrame& frame, bool sidechain = false) {
    float result = 0;
    for (int i = 0; i < juce::jlimit(0, 512, frame.bins); ++i) {
        result = std::max(result, std::abs(sidechain ? frame.sideMin[(size_t)i] : frame.mainMin[(size_t)i]));
        result = std::max(result, std::abs(sidechain ? frame.sideMax[(size_t)i] : frame.mainMax[(size_t)i]));
    }
    return result;
}

float testInput(int64_t sample, int channel) {
    if (sample < 0) return 0;
    const double time = sample / sampleRate;
    return float(.17 * std::sin(2 * juce::MathConstants<double>::pi * (channel ? 997 : 61) * time)
               + .04 * std::sin(2 * juce::MathConstants<double>::pi * 137 * time));
}

// Four actual PCM kick bursts per 4/4 bar at120BPM. They start away from the
// capture boundary, so a partial first bar cannot masquerade as a complete loop.
float fourKickInput(int64_t sample, int channel) {
    if (sample < 0) return 0;
    const int phase = int(sample % 96000);
    for (int hit = 0; hit < 4; ++hit) {
        const int age = phase - (6000 + hit * 24000);
        if (age >= 0 && age < 3840) {
            const double time = age / sampleRate;
            const float signal = float(.8 * std::exp(-time / .018)
                               * std::sin(2 * juce::MathConstants<double>::pi * (55 * time + 18 * time * std::exp(-time / .007))));
            return channel ? -signal : signal;
        }
    }
    return 0;
}

template<typename Signal>
void feed(OpenDuckProcessor& processor, Clock& clock, int64_t& position, int samples, Signal signal) {
    const auto end = position + samples;
    while (position < end) {
        const int count = int(std::min<int64_t>(blockSize, end - position));
        juce::AudioBuffer<float> audio(2, count);
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                audio.setSample(channel, i, signal(position + i, channel));
        clock.sample = position;
        processor.processBlock(audio, midi);
        position += count;
    }
}

struct NullMetrics { double maxError = 0, rmsError = 0; int measured = 0; };
NullMetrics dryDifference(OpenDuckProcessor& processor) {
    Clock clock;
    processor.setPlayHead(&clock);
    processor.prepareToPlay(sampleRate, blockSize);
    const int latency = processor.getLatencySamples();
    NullMetrics result;
    double squared = 0;
    for (int64_t position = 0; position < int64_t(sampleRate * 3); position += blockSize) {
        const int count = int(std::min<int64_t>(blockSize, int64_t(sampleRate * 3) - position));
        juce::AudioBuffer<float> audio(2, count);
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                audio.setSample(channel, i, testInput(position + i, channel));
        clock.sample = position;
        processor.processBlock(audio, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i) {
                const double error = audio.getSample(channel, i) - testInput(position + i - latency, channel);
                result.maxError = std::max(result.maxError, std::abs(error));
                squared += error * error;
                ++result.measured;
            }
    }
    result.rmsError = std::sqrt(squared / result.measured);
    // No dangling playhead may remain after this helper returns.
    processor.setPlayHead(nullptr);
    return result;
}

juce::MemoryBlock changedHistorical(const juce::MemoryBlock& original, float position, float bypass) {
    auto xml = juce::AudioProcessor::getXmlFromBinary(original.getData(), int(original.getSize()));
    if (!xml || xml->getIntAttribute("schema", 0) != 4) throw std::runtime_error("Expected actual schema4 fixture");
    auto tree = juce::ValueTree::fromXml(*xml);
    for (int i = 0; i < tree.getNumChildren(); ++i) {
        auto child = tree.getChild(i);
        const auto id = child.getProperty("id").toString();
        if (id == "position4") child.setProperty("value", position, nullptr);
        if (id == "bypass") child.setProperty("value", bypass, nullptr);
    }
    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), result);
    return result;
}

int grayInk(const juce::Image& image) {
    const float scale = image.getWidth() / 1728.f;
    const juce::Rectangle<int> area(juce::roundToInt(581 * scale), juce::roundToInt(250 * scale),
                                    juce::roundToInt(1065 * scale), juce::roundToInt(282 * scale));
    int pixels = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            const auto colour = image.getPixelAt(x, y);
            const int r = colour.getRed(), g = colour.getGreen(), b = colour.getBlue();
            if (r >= 65 && g >= 65 && b >= 65 && std::abs(r - g) < 35 && std::abs(r - b) < 45) ++pixels;
        }
    return pixels;
}

struct CyanExtent { int first = 1000000, last = -1; int height() const { return last >= first ? last - first + 1 : 0; } };
CyanExtent cyanCurveExtent(const juce::Image& image, float designX) {
    const float scale = image.getWidth() / 1728.f;
    CyanExtent result;
    const int x0 = int(std::floor((designX - 1) * scale)), x1 = int(std::ceil((designX + 1) * scale));
    const int y0 = int(std::floor((249 - 5) * scale)), y1 = int(std::ceil((249 + 5) * scale));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const auto colour = image.getPixelAt(x, y);
            if (colour.getGreen() >= 70 && colour.getBlue() >= 70
                    && colour.getGreen() - colour.getRed() >= 60 && colour.getBlue() - colour.getRed() >= 60) {
                result.first = std::min(result.first, y);
                result.last = std::max(result.last, y);
            }
        }
    return result;
}

juce::Image preview(OpenDuckProcessor& processor, const juce::File& output, int width, const juce::String& name, bool inspectDefaults = false) {
    processor.state.state.setProperty("templateAiView", false, nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (!editor) throw std::runtime_error("Native editor unavailable");
    editor->setSize(width, juce::roundToInt(width * 912.f / 1728));
    bool inside = true, mixZero = false, powerOff = false, commaReadout = false;
    for (int i = 0; i < editor->getNumChildComponents(); ++i) {
        auto* child = editor->getChildComponent(i);
        if (child->isVisible()) inside &= editor->getLocalBounds().contains(child->getBounds());
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            if (slider->getName() == "duckMix4") mixZero = std::abs(slider->getValue()) < 1.e-7;
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getName() == "power4") powerOff = !button->getToggleState();
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            const auto bounds = label->getBounds();
            if (bounds.getX() > width * .10f && bounds.getX() < width * .13f
                    && bounds.getY() > editor->getHeight() * .60f && bounds.getY() < editor->getHeight() * .70f)
                commaReadout = label->getText() == "0,0%";
        }
    }
    check(inside, "native visible controls fit width" + std::to_string(width));
    if (inspectDefaults) check(mixZero && powerOff && commaReadout, "native INIT shows MIX0,0% and visible Power off width" + std::to_string(width));
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
    auto stream = output.getChildFile(name + ".png").createOutputStream();
    juce::PNGImageFormat png;
    const bool written = stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
        && png.writeImageToStream(image, *stream);
    check(written, "actual native preview written " + name.toStdString());
    return image;
}

void defaultsAndState(const juce::File& output, const juce::File& fixtures) {
    auto initial = std::make_unique<OpenDuckProcessor>();
    check(initial->value("duckMix4") == 0 && initial->value("position4") == 50 && initial->value("bypass") == 1,
          "new instance starts MIX0 with existing visible Power off");
    auto frame = initial->getInputScopeFrame();
    check(frame.bins == 0 && frame.generation == 0 && framePeak(frame) == 0,
          "new MAIN scope has no invented waveform before real audio");
    juce::MemoryBlock initialState;
    initial->getStateInformation(initialState);
    check(output.getChildFile("Template-init.odpx").replaceWithData(initialState.getData(), initialState.getSize()),
          "new neutral INIT state exported");

    const auto neutral = dryDifference(*initial);
    std::cout << "MEASURE INIT max-dry-error " << neutral.maxError << " rms-dry-error " << neutral.rmsError << std::endl;
    check(neutral.measured > 200000 && neutral.maxError < 1.e-7 && neutral.rmsError < 1.e-8,
          "new INIT output nulls against latency-aligned stereo dry from startup");
    check(framePeak(initial->getInputScopeFrame()) > .15f && !initial->getInputScopeFrame().sidechain,
          "MAIN waveform captures actual input while Power off without sidechain");
    set(*initial,"bypass",0);
    const auto zeroDepth=dryDifference(*initial);
    check(initial->value("duckMix4")==0&&zeroDepth.maxError<1.e-6&&zeroDepth.rmsError<1.e-7,
          "Power on with MIX0 remains latency-aligned dry with no startup attenuation");

    set(*initial, "duckMix4", 69);
    set(*initial, "position4", 69);
    set(*initial, "duration4", 23);
    set(*initial, "curveTension0", -31);
    set(*initial, "bypass", 0);
    initial->applyTemplatePreset(0);
    check(initial->value("duckMix4") == 0 && initial->value("position4") == 50 && initial->value("bypass") == 1
              && initial->value("duration4") == 0 && initial->getCurveSnapshot(0).shape == 0,
          "explicit INIT restores MIX0 and visible Power off after active edits");
    auto restored = std::make_unique<OpenDuckProcessor>();
    restored->setStateInformation(initialState.getData(), int(initialState.getSize()));
    check(restored->value("duckMix4") == 0 && restored->value("position4") == 50 && restored->value("bypass") == 1,
          "new neutral state restores MIX and Power exactly");

    juce::MemoryBlock historical;
    const bool loaded = fixtures.getChildFile("legacy-v0.4.0-init.odpx").loadFileAsData(historical);
    check(loaded, "actual0.4.0 INIT state fixture available");
    if (loaded) {
        restored->setStateInformation(historical.getData(), int(historical.getSize()));
        check(restored->value("duckMix4") == 100 && restored->value("position4") == 50 && restored->value("bypass") == 0,
              "actual schema4 INIT retains its saved MIX50 and Power on");
        const auto oldZero = changedHistorical(historical, 0, 0);
        restored->setStateInformation(oldZero.getData(), int(oldZero.getSize()));
        const auto activeZero = dryDifference(*restored);
        check(restored->value("duckMix4") == 100 && restored->value("position4") == 0 && restored->value("bypass") == 0 && activeZero.rmsError > .005,
              "saved schema4 MIX0 with Power on keeps its original audible horizontal-position meaning");
        const auto oldOff = changedHistorical(historical, 27, 1);
        restored->setStateInformation(oldOff.getData(), int(oldOff.getSize()));
        std::cout << "MEASURE schema4 saved-position27 restored " << std::setprecision(9)
                  << restored->value("position4") << " bypass " << restored->value("bypass") << std::endl;
        check(std::abs(restored->value("position4") - 27) < .00001f && restored->value("bypass") == 1,
              "saved schema4 nondefault position restores within host float precision and Power off restores exactly");
    }
    set(*restored, "duckMix4", 63);
    set(*restored, "position4", 63);
    set(*restored, "duration4", -17);
    set(*restored, "curveTension0", 24);
    set(*restored, "bypass", 0);
    juce::MemoryBlock editedState;
    restored->getStateInformation(editedState);
    check(output.getChildFile("QA-StateRoundtrip.odpx").replaceWithData(editedState.getData(), editedState.getSize()),
          "active edited state exported for installer roundtrip QA");
    auto third = std::make_unique<OpenDuckProcessor>();
    third->setStateInformation(editedState.getData(), int(editedState.getSize()));
    check(third->value("duckMix4") == 63 && third->value("position4") == 63 && third->value("duration4") == -17
              && third->value("curveTension0") == 24 && third->value("bypass") == 0,
          "active parameters and visible Power survive exact save and reload");
    set(*third, "position4", 12);
    set(*third, "bypass", 1);
    check(restored->value("position4") == 63 && restored->value("bypass") == 0
              && initial->value("duckMix4") == 0 && initial->value("position4") == 50 && initial->value("bypass") == 1,
          "multiple instances keep independent MIX and Power states");
}

void inputWithoutSidechain(int trigger) {
    auto owned = std::make_unique<OpenDuckProcessor>();
    auto& processor = *owned;
    Clock clock;
    processor.setPlayHead(&clock);
    processor.setSectorParameter(0, "trigger", float(trigger));
    processor.prepareToPlay(sampleRate, blockSize);
    int64_t position = 0;
    const std::string prefix = "trigger" + std::to_string(trigger) + ": ";
    feed(processor, clock, position, processor.getLatencySamples() + 9600, testInput);
    auto frame = processor.getInputScopeFrame();
    check(frame.generation > 0 && frame.bins > 0 && !frame.complete && framePeak(frame) > .15f,
          prefix + "real partial MAIN waveform appears before the first full loop");
    feed(processor, clock, position, 96000 * 3, fourKickInput);
    frame = processor.getInputScopeFrame();
    const auto hit = odx::view::isolateSingleHit(frame.mainMin, frame.mainMax, frame.periodSeconds, frame.bins);
    check(frame.complete && frame.bins == 512 && std::abs(frame.periodSeconds - 2.) < 1.e-7,
          prefix + "default MAIN view captures a complete four-beat loop at120BPM");
    check(framePeak(frame) > .5f && framePeak(frame, true) == 0 && !frame.sidechain,
          prefix + "channel audio remains visible with absent sidechain and no MIDI notes");
    std::cout << "MEASURE " << prefix << "MAIN-peak " << framePeak(frame) << " detected-hits " << hit.detectedOnsets << std::endl;
    check(hit.detectedOnsets == 4 && hit.binCount > 0 && hit.binCount < 180,
          prefix + "full frame retains all four kicks and can isolate one without changing capture");
    const auto frozen = processor.getInputScopeFrame();
    const auto axis = odx::view::scopeAxis(frame.periodSeconds, .25f, 4.f, .35f);
    check(std::abs(axis.startMs - 500.) < 1.e-4 && std::abs(axis.endMs - 1000.) < 1.e-4
              && frozen.generation == frame.generation && frozen.mainMax == frame.mainMax,
          prefix + "quarter-loop display zoom keeps original physical timestamps and all captured samples");
}

void toneAndViews(const juce::File& output) {
    auto processor = std::make_unique<OpenDuckProcessor>();
    Clock clock;
    processor->setPlayHead(&clock);
    processor->prepareToPlay(sampleRate, blockSize);
    std::array<juce::Image, 3> blank;
    const std::array<int, 3> widths{{864, 1296, 1728}};
    const std::array<juce::String, 3> suffix{{"50pct", "75pct", "100pct"}};
    for (size_t i = 0; i < widths.size(); ++i)
        blank[i] = preview(*processor, output, widths[i], "template-init-" + suffix[i], true);
    int64_t position = 0;
    feed(*processor, clock, position, processor->getLatencySamples() + 96000 * 3, fourKickInput);
    for (size_t i = 0; i < widths.size(); ++i) {
        const auto image = preview(*processor, output, widths[i], "template-" + suffix[i]);
        const int extraInk = grayInk(image) - grayInk(blank[i]);
        std::cout << "MEASURE width" << widths[i] << " extra-gray-waveform-pixels " << extraInk << std::endl;
        check(extraInk > widths[i] / 5, "native MAIN graph paints gray-white real four-kick waveform width" + std::to_string(widths[i]));
        const auto plateau = cyanCurveExtent(image, 1500);
        const auto endpoint = cyanCurveExtent(image, 1648);
        const int centreRow = int(std::floor(249 * widths[i] / 1728.f));
        std::cout << "MEASURE width" << widths[i] << " cyan-plateau-rows " << plateau.first << ".." << plateau.last
                  << " cyan-endpoint-rows " << endpoint.first << ".." << endpoint.last << std::endl;
        check(plateau.height() >= int(std::ceil(6. * widths[i] / 1728)) && endpoint.height() >= plateau.height() - 1
                  && endpoint.first < centreRow && endpoint.last > centreRow,
              "native blue curve keeps its full upper and lower stroke at100percent and final endpoint width" + std::to_string(widths[i]));
    }
    processor->state.state.setProperty("templateCurveView4", 1, nullptr);
    preview(*processor, output, 1296, "template-single-hit-75pct");
    processor->state.state.setProperty("templateCurveView4", 2, nullptr);
    processor->state.state.setProperty("templateWaveZoom", 4.f, nullptr);
    processor->state.state.setProperty("templateWaveOffset", .25f, nullptr);
    preview(*processor, output, 1296, "template-quarter-loop-75pct");
    processor->state.state.setProperty("templateWaveZoom", 1.f, nullptr);
    processor->state.state.setProperty("templateWaveOffset", 0.f, nullptr);
    feed(*processor, clock, position, 96000 * 3, testInput);
    auto frame = processor->getInputScopeFrame();
    const auto tone = odx::view::isolateSingleHit(frame.mainMin, frame.mainMax, frame.periodSeconds, frame.bins);
    check(frame.complete && framePeak(frame) > .15f && tone.hasAudio && tone.detectedOnsets == 0,
          "continuous channel audio remains fully captured rather than filtered out as a non-kick");
    preview(*processor, output, 1296, "template-continuous-main-75pct");
    const auto toneGeneration = frame.generation;
    feed(*processor, clock, position, 96000 * 3, [](int64_t, int) { return 0.f; });
    frame = processor->getInputScopeFrame();
    check(frame.complete && frame.generation > toneGeneration && framePeak(frame) == 0,
          "real silence replaces previous channel audio instead of animating a fabricated signal");
    processor->setInputScopeBeats(1);
    feed(*processor, clock, position, 48000 * 2, testInput);
    const auto oneBeat = processor->getInputScopeFrame();
    check(oneBeat.complete && std::abs(oneBeat.periodSeconds - .5) < 1.e-7,
          "display can select a complete one-beat channel-audio window");
    feed(*processor, clock, position, 48000 * 2, fourKickInput);
    preview(*processor, output, 1728, "concept-one-cycle");
    set(*processor, "bandEnable4", 1);
    preview(*processor, output, 1728, "concept-band-on");
    set(*processor, "bandEnable4", 0);
    auto second = std::make_unique<OpenDuckProcessor>();
    Clock secondClock;
    second->setPlayHead(&secondClock);
    second->prepareToPlay(sampleRate, blockSize);
    second->setInputScopeBeats(8);
    int64_t secondPosition = 0;
    feed(*second, secondClock, secondPosition, second->getLatencySamples() + 48000 * 9, testInput);
    const auto eightBeats = second->getInputScopeFrame();
    check(eightBeats.complete && std::abs(eightBeats.periodSeconds - 4.) < 1.e-7
              && std::abs(processor->getInputScopeFrame().periodSeconds - .5) < 1.e-7,
          "instances retain independent one-beat and eight-beat waveform durations");
}

void transportEdges() {
    auto processor = std::make_unique<OpenDuckProcessor>();
    Clock clock;
    processor->setPlayHead(&clock);
    processor->prepareToPlay(sampleRate, blockSize);
    int64_t position = 30000;
    feed(*processor, clock, position, processor->getLatencySamples() + 9600, testInput);
    const auto partial = processor->getInputScopeFrame();
    bool noUnknownPrefix = true;
    for (int i = 0; i < partial.firstBin; ++i)
        noUnknownPrefix &= partial.mainMin[(size_t)i] == 0 && partial.mainMax[(size_t)i] == 0;
    check(!partial.complete && partial.firstBin > 50 && partial.bins > partial.firstBin
              && framePeak(partial) > .15f && noUnknownPrefix,
          "host starting midbar publishes real input only after its known firstBin");
    clock.playing = false;
    processor->prepareToPlay(sampleRate, blockSize);
    position = 0;
    feed(*processor, clock, position, processor->getLatencySamples() + 96000 * 2, testInput);
    const auto stopped = processor->getInputScopeFrame();
    check(stopped.complete && framePeak(stopped) > .15f,
          "live channel input is captured while host transport is stopped");
    processor->setPlayHead(nullptr);
    processor->prepareToPlay(sampleRate, blockSize);
    position = 0;
    feed(*processor, clock, position, processor->getLatencySamples() + 96000 * 2, testInput);
    const auto noHost = processor->getInputScopeFrame();
    check(noHost.complete && framePeak(noHost) > .15f,
          "channel waveform remains available with no host playhead at all");
}

void recordedFixture(const juce::File& fixtures) {
    const auto file = fixtures.getChildFile("audio/LAB-Kick-145.wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(), true));
    check(reader != nullptr, "real TEST LAB kick PCM fixture is available");
    if (!reader) return;
    juce::AudioBuffer<float> fixture(2, int(reader->lengthInSamples));
    reader->read(&fixture, 0, fixture.getNumSamples(), 0, true, true);
    auto processor = std::make_unique<OpenDuckProcessor>();
    Clock clock;
    clock.rate = reader->sampleRate;
    clock.tempo = 145;
    processor->setPlayHead(&clock);
    processor->prepareToPlay(clock.rate, blockSize);
    const int latency = processor->getLatencySamples();
    double maxError = 0;
    const int samples = int(clock.rate * 5);
    for (int position = 0; position < samples; position += blockSize) {
        const int count = std::min(blockSize, samples - position);
        juce::AudioBuffer<float> audio(2, count);
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                audio.setSample(channel, i, fixture.getSample(channel, (position + i) % fixture.getNumSamples()));
        clock.sample = position;
        processor->processBlock(audio, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i) {
                const int source = position + i - latency;
                const float expected = source >= 0 ? fixture.getSample(channel, source % fixture.getNumSamples()) : 0;
                maxError = std::max(maxError, double(std::abs(audio.getSample(channel, i) - expected)));
            }
    }
    const auto frame = processor->getInputScopeFrame();
    const float fixturePeak = fixture.getMagnitude(0, fixture.getNumSamples());
    const float capturedPeak = framePeak(frame);
    check(maxError < 1.e-7 && frame.complete && capturedPeak > 1.e-6f
              && std::abs(capturedPeak - fixturePeak) < 1.e-7f && !frame.sidechain
              && std::abs(frame.periodSeconds - 240. / 145.) < 1.e-7,
          "real44.1kHz kick PCM is visible as MAIN while neutral output remains exact dry at145BPM");
    std::cout << "MEASURE recorded-fixture max-dry-error " << maxError << " MAIN-peak " << capturedPeak
              << " actual-fixture-peak " << fixturePeak
              << " capture-seconds " << frame.periodSeconds << std::endl;
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    const auto output = argc > 1 ? juce::File(juce::String::fromUTF8(argv[1]))
                                 : juce::File::getCurrentWorkingDirectory().getChildFile("work/revision041/Preview");
    const auto fixtures = argc > 2 ? juce::File(juce::String::fromUTF8(argv[2]))
                                   : juce::File::getCurrentWorkingDirectory().getChildFile("tests/fixtures");
    output.createDirectory();
    try {
        defaultsAndState(output, fixtures);
        for (const int trigger : {0, 1, 2}) inputWithoutSidechain(trigger);
        toneAndViews(output);
        transportEdges();
        recordedFixture(fixtures);
    } catch (const std::exception& error) {
        check(false, std::string("exception: ") + error.what());
    }
    auto* report = new juce::DynamicObject;
    report->setProperty("scope", "actual processor audio and native component snapshots; dedicated test PCM only; no user media or audio device");
    report->setProperty("passed", passes);
    report->setProperty("failed", failures);
    report->setProperty("observations", observations);
    output.getChildFile("Revision041-results.json").replaceWithText(juce::JSON::toString(juce::var(report), true));
    std::cout << "RESULT Revision041 " << passes << " passed, " << failures << " failed" << std::endl;
    return failures ? 1 : 0;
}
