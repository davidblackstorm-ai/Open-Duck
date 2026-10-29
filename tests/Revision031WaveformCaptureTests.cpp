#include "PluginProcessor.h"
#include <iostream>
#include <fstream>
#include <set>
#include <cstring>
#include <sstream>

namespace {
constexpr double sampleRate = 48000.;
constexpr int blockSize = 257;
int passed = 0, failed = 0;
void check(bool ok, const std::string& name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    ok ? ++passed : ++failed;
}
void set(OpenDuckProcessor& p, const juce::String& id, float value) {
    auto* parameter = p.state.getParameter(id);
    if (!parameter) throw std::runtime_error("Missing parameter " + id.toStdString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
struct Head : juce::AudioPlayHead {
    double bpm = 145., ppq = 0.;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo result; result.setBpm(bpm); result.setPpqPosition(ppq); result.setIsPlaying(true); return result;
    }
};
float peak(const OpenDuckProcessor::ScopeFrame& f, bool main = false) {
    float result = 0;
    for (int i = 0; i < 512; ++i) result = std::max(result, std::max(std::abs(main ? f.mainMin[i] : f.sideMin[i]), std::abs(main ? f.mainMax[i] : f.sideMax[i])));
    return result;
}
float tailPeak(const OpenDuckProcessor::ScopeFrame& f, bool main) {
    float result = 0;
    const int begin = int(.055 / f.periodSeconds * 512), end = int(.070 / f.periodSeconds * 512);
    for (int i = begin; i <= end && i < 512; ++i) result = std::max(result, std::max(std::abs(main ? f.mainMin[i] : f.sideMin[i]), std::abs(main ? f.mainMax[i] : f.sideMax[i])));
    return result;
}
double hitDuration(const OpenDuckProcessor::ScopeFrame& f, bool main) {
    const float threshold = peak(f, main) * .15f;
    int first = -1, last = -1;
    for (int i = 0; i < 512; ++i) if (std::max(std::abs(main ? f.mainMin[i] : f.sideMin[i]), std::abs(main ? f.mainMax[i] : f.sideMax[i])) > threshold) {
        if (first < 0) first = i; last = i;
    }
    return first >= 0 ? (last - first + 1) * f.periodSeconds / 512. : 0.;
}
struct Observation {
    uint64_t initialGeneration = 0, finalGeneration = 0, finalTrigger = 0;
    bool seed = false, low = false, high = false;
    float latestPeak = 0, latestTail = 0;
    double period = 0, latestDuration = 0;
    std::set<uint64_t> generations;
};
juce::var scenario(int trigger, double bpm) {
    auto owned = std::make_unique<OpenDuckProcessor>(); auto& p = *owned;
    Head head; head.bpm = bpm; p.setPlayHead(&head);
    auto layout = p.getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    if (!p.setBusesLayout(layout)) throw std::runtime_error("Stereo SC layout rejected");
    // This regression exercises the legacy six-clock engine, including disabled sectors.
    // The measured single-engine model has its independent MAIN scope tested in Revision041.
    set(p, "profileModel4", 0);
    // ON is irrelevant to input/SC telemetry; only one engine needs to process audio.
    for (int mode = 0; mode < 6; ++mode) {
        set(p, "on" + juce::String(mode), mode == 0 ? 1.f : 0.f);
        p.setSectorParameter(mode, "trigger", float(trigger));
        p.setSectorParameter(mode, "speed", 2);
        p.setSectorParameter(mode, "loop", 0);
        p.setSectorParameter(mode, "threshold", -36);
        p.setSectorParameter(mode, "detectorAttack", .1f);
        p.setSectorParameter(mode, "detectorRelease", 2);
        p.setSectorParameter(mode, "holdoff", 100);
    }
    p.prepareToPlay(sampleRate, blockSize);
    const int periodSamples = int(std::round(sampleRate * 60. / bpm));
    const int seedSample = 4800;
    const int trainStart = seedSample + 3 * periodSamples;
    const int trainCount = 9;
    const int endSample = trainStart + trainCount * periodSamples + p.getLatencySamples() + 2000;
    std::array<Observation, 6> observations;
    uint64_t outputHash = 14695981039346656037ull;
    auto hitSignal = [&](int sample) {
        int age = sample - seedSample; float amplitude = .14f;
        if (sample >= trainStart) {
            const int hit = (sample - trainStart) / periodSamples;
            if (hit >= trainCount) return 0.f;
            age = (sample - trainStart) % periodSamples;
            amplitude = hit % 2 ? .8f : .4f;
        }
        if (age < 0 || age >= 3840) return 0.f;
        return age < 192 ? amplitude : amplitude * .30f;
    };
    auto addNote = [&](juce::MidiBuffer& midi, int start, int count, int at) {
        if (at >= start && at < start + count) midi.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8)100), at - start);
        if (at + 960 >= start && at + 960 < start + count) midi.addEvent(juce::MidiMessage::noteOff(10, 36), at + 960 - start);
    };
    for (int start = 0; start < endSample; start += blockSize) {
        const int count = std::min(blockSize, endSample - start);
        juce::AudioBuffer<float> audio(4, count); juce::MidiBuffer midi;
        for (int i = 0; i < count; ++i) {
            const float hit = hitSignal(start + i);
            const float input = trigger == 3 ? hit : float(.15 * std::sin(2 * juce::MathConstants<double>::pi * 997 * (start + i) / sampleRate));
            audio.setSample(0, i, input); audio.setSample(1, i, input);
            audio.setSample(2, i, hit); audio.setSample(3, i, -hit);
        }
        if (trigger == 2) {
            addNote(midi, start, count, seedSample);
            for (int hit = 0; hit < trainCount; ++hit) addNote(midi, start, count, trainStart + hit * periodSamples);
        }
        head.ppq = start / sampleRate * bpm / 60.; p.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < count; ++i) {
            const float value = audio.getSample(c, i); unsigned char bytes[sizeof(float)]; std::memcpy(bytes, &value, sizeof(value));
            for (unsigned char byte : bytes) { outputHash ^= byte; outputHash *= 1099511628211ull; }
        }
        for (int mode = 0; mode < 6; ++mode) {
            const auto f = p.getScopeFrame(mode); auto& o = observations[(size_t)mode];
            if (!f.complete) continue;
            if (start < trainStart) { o.seed = peak(f, trigger == 3) > .1f; o.initialGeneration = f.generation; }
            if (start >= trainStart && o.generations.insert(f.generation).second) {
                const float maximum = peak(f, trigger == 3);
                o.low |= maximum > .35f && maximum < .5f;
                o.high |= maximum > .7f;
                o.finalGeneration = f.generation; o.finalTrigger = f.triggerSample;
                o.latestPeak = maximum; o.latestTail = tailPeak(f, trigger == 3); o.period = f.periodSeconds; o.latestDuration = hitDuration(f, trigger == 3);
            }
        }
    }
    juce::Array<juce::var> modes;
    for (int mode = 0; mode < 6; ++mode) {
        const auto& o = observations[(size_t)mode];
        const std::string prefix = "mode" + std::to_string(mode) + " trigger" + std::to_string(trigger) + " BPM" + std::to_string(int(bpm)) + ": ";
        check(o.seed, prefix + "isolated seed has a real completed waveform");
        check(o.finalGeneration > o.initialGeneration && o.low && o.high && o.finalTrigger >= uint64_t(trainStart), prefix + "repeated accepted hits publish both distinct recent kick payloads");
        check(o.latestTail > .08f && std::abs(o.period - 60. / bpm) < 1e-8 && std::abs(o.latestDuration - .080) < 3 * o.period / 512., prefix + "recent complete waveform retains80ms physical width and55-70ms tail on its actual musical time scale");
        auto* row = new juce::DynamicObject;
        row->setProperty("mode", mode); row->setProperty("seedComplete", o.seed);
        row->setProperty("seedGeneration", (juce::int64)o.initialGeneration); row->setProperty("finalGeneration", (juce::int64)o.finalGeneration);
        row->setProperty("finalTriggerSample", (juce::int64)o.finalTrigger); row->setProperty("observedDistinctGenerations", int(o.generations.size()));
        row->setProperty("sawLowMarker", o.low); row->setProperty("sawHighMarker", o.high);
        row->setProperty("latestPeak", o.latestPeak); row->setProperty("tail55to70msPeak", o.latestTail); row->setProperty("capturedPeriodSeconds", o.period); row->setProperty("measuredHitDurationSeconds", o.latestDuration);
        modes.add(juce::var(row));
    }
    auto* result = new juce::DynamicObject; result->setProperty("trigger", trigger); result->setProperty("bpm", bpm);
    result->setProperty("periodSamples", periodSamples); result->setProperty("latencySamples", p.getLatencySamples()); result->setProperty("trainStartSample", trainStart); result->setProperty("modes", modes);
    std::ostringstream hash; hash << std::hex << outputHash; result->setProperty("outputDiagnosticFNV64", juce::String(hash.str()));
    return juce::var(result);
}
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf; juce::ScopedJuceInitialiser_GUI init;
    juce::Array<juce::var> runs;
    try { for (const double bpm : {145., 168.}) for (const int trigger : {1, 3, 2}) runs.add(scenario(trigger, bpm)); }
    catch (const std::exception& error) { check(false, std::string("exception: ") + error.what()); }
    if (argc > 1) {
        auto* report = new juce::DynamicObject; report->setProperty("scope", "actual native processor; synthetic positive-polarity pulse with80ms tail; no GUI, DAW, AI or audio device");
        report->setProperty("runs", runs); report->setProperty("passed", passed); report->setProperty("failed", failed);
        const juce::File output(juce::String::fromUTF8(argv[1])); output.replaceWithText(juce::JSON::toString(juce::var(report), true));
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
