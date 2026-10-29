#pragma once

#include "../Source/PluginEditor.h"

#include <set>
#include <cstring>
#include <limits>

namespace revisionTests
{
inline juce::File fixture(const char *name)
{
    return juce::File(juce::String::fromUTF8(__FILE__)).getParentDirectory()
        .getChildFile("fixtures").getChildFile(name);
}

inline bool loadLegacy(OpenDuckProcessor &processor)
{
    juce::MemoryBlock block;
    if (!fixture("legacy-v0.1.0-init.odpx").loadFileAsData(block))
        return false;
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    return true;
}

// Captured from the archived v0.2.0 VST3, rather than synthesised from current defaults.
// This deliberately exercises schema 2's 80% MIX, 0.75 spectral / 0.65 groove
// depths and 1 / 0.65 / 0.35 band depths on the unchanged compatibility DSP.
inline bool loadRevision2(OpenDuckProcessor &processor)
{
    juce::MemoryBlock block;
    if (!fixture("legacy-v0.2.0-init.odpx").loadFileAsData(block)) return false;
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    if (xml && xml->hasTagName("VST3PluginState"))
    {
        // The independent host exports the JUCE VST3 wrapper. IComponent starts
        // with the processor chunk, followed by JUCE's optional private footer.
        auto *component = xml->getChildByName("IComponent");
        juce::MemoryBlock componentState;
        if (!component || !componentState.fromBase64Encoding(component->getAllSubText())) return false;
        xml = juce::AudioProcessor::getXmlFromBinary(componentState.getData(), static_cast<int>(componentState.getSize()));
    }
    if (!xml || xml->getIntAttribute("schema", -1) != 2 ||
        !xml->hasTagName(processor.state.state.getType())) return false;
    juce::AudioProcessor::copyXmlToBinary(*xml, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    bool correct = std::abs(processor.value("mix") - 80) < .001f &&
                   std::abs(processor.value("spectralDepth") - .75f) < .001f &&
                   std::abs(processor.value("grooveDepth") - .65f) < .001f &&
                   std::abs(processor.value("bandMid") - .65f) < .001f &&
                   std::abs(processor.value("bandHigh") - .35f) < .001f;
    for (int mode = 0; mode < 6; ++mode)
        correct &= processor.usesModernCurves(mode) && !processor.usesRevision3(mode);
    return correct;
}

inline juce::Component *findNamed(juce::Component &root, const juce::String &name)
{
    if (root.getName() == name)
        return &root;
    for (auto *child : root.getChildren())
        if (auto *found = findNamed(*child, name))
            return found;
    return nullptr;
}

inline bool equalCurve(const OpenDuckProcessor::CurveSnapshot &a, const OpenDuckProcessor::CurveSnapshot &b)
{
    bool equal = a.count == b.count && a.shape == b.shape && a.grid == b.grid && a.presetId == b.presetId &&
                 a.custom == b.custom && a.linear == b.linear && a.edit == b.edit &&
                 a.freeDraw == b.freeDraw && a.band == b.band && a.legacy == b.legacy && a.modern3 == b.modern3;
    for (auto pair : {std::pair<float, float>{a.phase, b.phase}, {a.length, b.length},
                      {a.tension, b.tension}, {a.amount, b.amount}, {a.attack, b.attack},
                      {a.release, b.release}, {a.focus, b.focus}, {a.riseSlope, b.riseSlope}})
        equal &= std::abs(pair.first - pair.second) < 0.001f;
    for (int point = 0; point < juce::jmin(256, a.count); ++point)
        equal &= std::abs(a.points[(size_t)point].x - b.points[(size_t)point].x) < 1e-6f &&
                 std::abs(a.points[(size_t)point].y - b.points[(size_t)point].y) < 1e-6f &&
                 a.points[(size_t)point].segment == b.points[(size_t)point].segment &&
                 std::abs(a.points[(size_t)point].curvature - b.points[(size_t)point].curvature) < 1e-6f;
    return equal;
}

template <typename Check>
void catalogAndCurves(Check &&check)
{
    const auto &presets = OpenDuckProcessor::factoryPresets();
    std::array<int, 7> counts{};
    std::set<juce::String> names, parameterSets;
    bool valid = presets.size() == 700;
    for (size_t index = 0; index < presets.size(); ++index)
    {
        const auto &preset = presets[index];
        valid &= preset.id == static_cast<int>(index) && preset.category >= 0 && preset.category < 7 &&
                 preset.shape >= 0 && preset.shape < 16 && preset.grid >= 0 && preset.grid <= 6 &&
                 std::isfinite(preset.phase) && std::abs(preset.phase) <= 100 &&
                 std::isfinite(preset.length) && preset.length > 0 && preset.length <= 1 &&
                 std::isfinite(preset.amount) && preset.amount >= 0 && preset.amount <= 100 &&
                 std::isfinite(preset.attack) && preset.attack >= 0.1f && preset.attack <= 100 &&
                 std::isfinite(preset.release) && preset.release >= 5 && preset.release <= 2000;
        if (preset.category >= 0 && preset.category < 7)
            ++counts[(size_t)preset.category];
        names.insert(juce::String(preset.category) + "/" + preset.name);
        juce::String parameters;
        for (float value : {static_cast<float>(preset.shape), static_cast<float>(preset.grid),
                            preset.phase, preset.length, preset.tension, preset.amount,
                            preset.mix, preset.attack, preset.release})
            parameters += juce::String(value, 6) + ";";
        parameterSets.insert(parameters);
    }
    check(valid && std::all_of(counts.begin(), counts.end(), [](int count) { return count == 100; }),
          "factory library contains 700 valid behaviors in seven categories of 100");
    check(names.size() == 700 && parameterSets.size() == 700,
          "all 700 categorized preset names and actual parameter combinations are distinct");

    auto processorOwner = std::make_unique<OpenDuckProcessor>();
    auto &processor = *processorOwner;
    bool initial = processor.currentBehaviorPresetId() == 0 && processor.getCurveSnapshot().shape == 0 &&
                   processor.value("selected") == 0 && std::abs(processor.value("mix") - 100) < .001f &&
                   std::abs(processor.getCurveSnapshot().amount - 100) < .001f;
    for (int mode = 0; mode < 6; ++mode)
        initial &= processor.usesModernCurves(mode) && processor.usesRevision3(mode) &&
                   !processor.editPointsEnabled(mode) && !processor.freeDrawEnabled(mode) &&
                   processor.getCurveSnapshot(mode).shape == 0 &&
                   processor.currentBehaviorPresetId(mode) == OpenDuckProcessor::sectorPresetIds(mode).front();
    check(initial, "new instances start in INIT with top-left shape and point/free-draw editing disabled in every mode");
    check(loadRevision2(processor), "archived v0.2.0 INIT loads all six schema 2 compatibility slots at MIX80");
    bool application = true;
    for (int id = 0; id < 700; ++id)
    {
        processor.applyBehaviorPreset(id, 3);
        const auto actual = processor.getCurveSnapshot(3);
        const auto &expected = presets[(size_t)id];
        application &= actual.presetId == id && actual.shape == expected.shape &&
                       actual.grid == expected.grid && std::abs(actual.phase - expected.phase) < 0.001f &&
                       std::abs(actual.length - expected.length) < 0.001f &&
                       std::abs(actual.amount - expected.amount) < 0.001f &&
                       std::abs(actual.tension - expected.tension) < 0.001f &&
                       std::abs(actual.attack - expected.attack) < 0.001f &&
                       std::abs(actual.release - expected.release) < 0.001f && !actual.edit && !actual.freeDraw;
    }
    check(application, "all 700 schema 2 presets apply their advertised engine parameters and disable optional editing");
    check(processor.currentBehaviorPresetId(0) == 0 && processor.getCurveSnapshot(0).shape == 0 &&
              processor.currentBehaviorPresetId(3) == 699,
          "preset selection changes only the target mode curve slot");
    juce::MemoryBlock compatibilitySaved; processor.getStateInformation(compatibilitySaved);
    auto reloaded = std::make_unique<OpenDuckProcessor>();
    reloaded->setStateInformation(compatibilitySaved.getData(), static_cast<int>(compatibilitySaved.getSize()));
    bool compatibilityExact = std::abs(reloaded->value("mix") - 80) < .001f;
    for (int mode = 0; mode < 6; ++mode)
        compatibilityExact &= !reloaded->usesRevision3(mode) && reloaded->usesModernCurves(mode) &&
                              equalCurve(processor.getCurveSnapshot(mode), reloaded->getCurveSnapshot(mode));
    check(compatibilityExact, "schema 2 curves retain their compatibility audio routing and exact slot state after current-version save/reopen");
    bool bounded = true;
    std::set<juce::String> profiles;
    for (int shape = 0; shape < 16; ++shape)
    {
        OpenDuckProcessor::CurveSnapshot curve;
        curve.shape = shape;
        juce::String profile;
        for (int sample = 0; sample <= 1000; ++sample)
        {
            const float value = OpenDuckProcessor::sampleCurve(curve, sample / 1000.f);
            bounded &= std::isfinite(value) && value >= 0 && value <= 1;
            if (sample % 10 == 0)
                profile += juce::String(value, 6) + ";";
        }
        profiles.insert(profile);
    }
    check(bounded && profiles.size() == 16, "all 16 factory shapes are finite, bounded and have distinct envelope profiles");

    OpenDuckProcessor::CurveSnapshot saw;
    saw.custom = true; saw.linear = true; saw.count = 5;
    saw.points[0] = {0, 0}; saw.points[1] = {.249f, 1}; saw.points[2] = {.25f, 0};
    saw.points[3] = {.749f, 1}; saw.points[4] = {1, 0};
    const float peak = OpenDuckProcessor::sampleCurve(saw, .249f);
    const float notch = OpenDuckProcessor::sampleCurve(saw, .25f);
    check(peak > .99f && notch < .01f && OpenDuckProcessor::sampleCurve(saw, .125f) > .45f,
          "free-draw linear curve preserves a sharp user-drawn sawtooth rather than forcing a bell");
}

template <typename Check>
void sessionPersistence(Check &&check)
{
    auto sourceOwner = std::make_unique<OpenDuckProcessor>();
    auto &source = *sourceOwner;
    for (int mode = 0; mode < 6; ++mode)
    {
        source.applyBehaviorPreset(37 + mode * 101, mode);
        auto curve = source.getCurveSnapshot(mode);
        curve.shape = mode * 2; curve.phase = static_cast<float>(-25 + mode * 10);
        curve.length = .5f + .08f * mode; curve.tension = static_cast<float>(-50 + 20 * mode);
        curve.amount = static_cast<float>(61 + 5 * mode); curve.attack = static_cast<float>(2 + 4 * mode);
        curve.release = static_cast<float>(80 + 19 * mode); curve.grid = mode % 7;
        curve.focus = static_cast<float>(80 * (mode + 1)); curve.band = mode % 2 != 0;
        curve.linear = mode % 2 == 0; curve.count = 4;
        curve.points[0] = {0, .05f * mode}; curve.points[1] = {.17f + .03f * mode, .9f};
        curve.points[2] = {.7f, .12f * mode}; curve.points[3] = {1, .8f};
        source.setCustomCurve(mode, curve);
        source.setEditPointsEnabled(mode % 2 != 0, mode);
        source.setFreeDrawEnabled(mode == 5, mode);
    }
    set(source, "selected", 4); set(source, "mix", 71); set(source, "trigger", 2);
    set(source, "on2", 1); set(source, "solo4", 1);
    std::array<OpenDuckProcessor::CurveSnapshot, 6> expected;
    for (int mode = 0; mode < 6; ++mode)
        expected[(size_t)mode] = source.getCurveSnapshot(mode);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    check(xml != nullptr && xml->getIntAttribute("schema") == 3,
          "new project state is explicitly serialized with schema version 3");
    auto restoredOwner = std::make_unique<OpenDuckProcessor>();
    auto &restored = *restoredOwner;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    bool exact = restored.value("selected") == 4 && std::abs(restored.value("mix") - 71) < .001f &&
                 restored.value("trigger") == 2 && restored.value("on2") == 1 && restored.value("solo4") == 1;
    for (int mode = 0; mode < 6; ++mode)
        exact &= equalCurve(expected[(size_t)mode], restored.getCurveSnapshot(mode));
    check(exact, "project roundtrip preserves all six independent curve slots, XY nodes, flags and host parameters");
    for (int reopen = 0; reopen < 3; ++reopen)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        editor->setSize(1400, 710);
    }
    bool reopened = restored.value("selected") == 4;
    for (int mode = 0; mode < 6; ++mode)
        reopened &= equalCurve(expected[(size_t)mode], restored.getCurveSnapshot(mode));
    check(reopened, "three editor close/reopen cycles preserve the session instead of returning to INIT");
    auto independentOwner = std::make_unique<OpenDuckProcessor>();
    auto &independent = *independentOwner;
    const auto independentBefore = independent.getCurveSnapshot(2);
    restored.applySectorPreset(OpenDuckProcessor::sectorPresetIds(2).back(), 2);
    check(equalCurve(source.getCurveSnapshot(2), expected[2]) &&
              equalCurve(independentBefore, independent.getCurveSnapshot(2)) &&
              independent.currentBehaviorPresetId(2) == OpenDuckProcessor::sectorPresetIds(2).front() &&
              !equalCurve(restored.getCurveSnapshot(2), expected[2]),
          "separate plugin instances and restored copies do not share mutable curve or preset state");

    if (xml)
    {
        xml->setAttribute("schema", 999);
        juce::MemoryBlock future;
        juce::AudioProcessor::copyXmlToBinary(*xml, future);
        const auto before = restored.getCurveSnapshot(2);
        restored.setStateInformation(future.getData(), static_cast<int>(future.getSize()));
        check(equalCurve(before, restored.getCurveSnapshot(2)), "unknown future state schema is ignored without resetting existing work");
    }
    auto corrupt = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    if (corrupt)
        if (auto *curves = corrupt->getChildByName("Curves"))
            if (auto *curve = curves->getChildByName("Curve"))
            {
                curve->setAttribute("count", 257);
                juce::MemoryBlock damaged;
                juce::AudioProcessor::copyXmlToBinary(*corrupt, damaged);
                const auto before = restored.getCurveSnapshot(2);
                const auto beforeMix = restored.value("mix");
                restored.setStateInformation(damaged.getData(), static_cast<int>(damaged.getSize()));
                check(equalCurve(before, restored.getCurveSnapshot(2)) && restored.value("mix") == beforeMix,
                      "corrupt curve node counts reject the entire saved state without partial parameter replacement");
            }
    auto compatibilityOwner = std::make_unique<OpenDuckProcessor>();
    auto &compatibility = *compatibilityOwner;
    loadLegacy(compatibility);
    bool legacy = true;
    for (int mode = 0; mode < 6; ++mode)
        legacy &= !compatibility.usesModernCurves(mode);
    juce::MemoryBlock legacySaved;
    compatibility.getStateInformation(legacySaved);
    auto reloadedLegacyOwner = std::make_unique<OpenDuckProcessor>();
    auto &reloadedLegacy = *reloadedLegacyOwner;
    reloadedLegacy.setStateInformation(legacySaved.getData(), static_cast<int>(legacySaved.getSize()));
    for (int mode = 0; mode < 6; ++mode)
        legacy &= !reloadedLegacy.usesModernCurves(mode);
    check(legacy, "legacy session keeps its compatibility DSP path after save and reopen");
}

template <typename Check>
void customCurveValidation(Check &&check)
{
    auto processorOwner = std::make_unique<OpenDuckProcessor>();
    auto &processor = *processorOwner;
    auto curve = processor.getCurveSnapshot(1);
    curve.count = 6; curve.custom = true;
    curve.points[0] = {.8f, .7f}; curve.points[1] = {.2f, .4f}; curve.points[2] = {.2f, .9f};
    curve.points[3] = {-1, 2}; curve.points[4] = {1.5f, -.3f};
    curve.points[5] = {std::numeric_limits<float>::quiet_NaN(), .5f};
    processor.setCustomCurve(1, curve);
    const auto actual = processor.getCurveSnapshot(1);
    bool safe = actual.count >= 0 && actual.count <= 256;
    float previous = -1;
    for (int point = 0; point < actual.count; ++point)
    {
        const auto value = actual.points[(size_t)point];
        safe &= std::isfinite(value.x) && std::isfinite(value.y) && value.x >= 0 && value.x <= 1 &&
                value.y >= 0 && value.y <= 1 && value.x > previous;
        previous = value.x;
    }
    for (int sample = 0; sample <= 1000; ++sample)
    {
        const auto value = OpenDuckProcessor::sampleCurve(actual, sample / 1000.f);
        safe &= std::isfinite(value) && value >= 0 && value <= 1;
    }
    check(safe, "unsorted, duplicate, out-of-range and non-finite custom points cannot enter the DSP unsanitized");
    auto dense = processor.getCurveSnapshot(4);
    dense.custom = true; dense.linear = true; dense.count = 256;
    for (int point = 0; point < dense.count; ++point)
        dense.points[(size_t)point] = {point / 255.f, (point % 2) ? 1.f : 0.f};
    processor.setCustomCurve(4, dense);
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    auto restoredOwner = std::make_unique<OpenDuckProcessor>();
    auto &restored = *restoredOwner;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(restored.getCurveSnapshot(4).count == 256 && equalCurve(processor.getCurveSnapshot(4), restored.getCurveSnapshot(4)),
          "maximum 256-point free-draw curve survives project serialization without node loss");
    processor.undo.clearUndoHistory();
    const auto before = processor.getCurveSnapshot(0);
    auto bump = before;
    bump.count = 5; bump.custom = true; bump.linear = false;
    bump.points[0] = {0, .5f}; bump.points[1] = {.25f, .5f}; bump.points[2] = {.5f, 1};
    bump.points[3] = {.75f, .5f}; bump.points[4] = {1, .5f};
    processor.setCustomCurve(0, bump, "User curve gesture");
    const auto changed = processor.getCurveSnapshot(0);
    const bool undoWorked = processor.undo.undo() && equalCurve(before, processor.getCurveSnapshot(0));
    const bool redoWorked = processor.undo.redo() && equalCurve(changed, processor.getCurveSnapshot(0));
    check(undoWorked && redoWorked, "curve gesture undo and redo restore both XY data and the host parameter selection");
}

inline std::vector<float> renderModern(OpenDuckProcessor &processor, int blocks = 64)
{
    processor.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    std::vector<float> result;
    result.reserve(static_cast<size_t>(blocks * 512));
    std::mt19937 random(412);
    std::uniform_real_distribution<float> noise(-.08f, .08f);
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < 512; ++sample)
        {
            const float time = (block * 512 + sample) / 48000.f;
            const float input = .2f * std::sin(juce::MathConstants<float>::twoPi * 1000 * time) +
                                .1f * std::sin(juce::MathConstants<float>::twoPi * 83 * time) + noise(random);
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample(channel, sample, input);
        }
        processor.processBlock(audio, midi);
        for (int sample = 0; sample < 512; ++sample)
            result.push_back(audio.getSample(0, sample));
    }
    return result;
}

template <typename Check>
void modernDsp(Check &&check)
{
    bool allRespond = true;
    for (int mode = 0; mode < 6; ++mode)
    {
        auto duckedOwner = std::make_unique<OpenDuckProcessor>();
        auto &ducked = *duckedOwner;
        auto openOwner = std::make_unique<OpenDuckProcessor>();
        auto &open = *openOwner;
        auto configure = [&](OpenDuckProcessor &processor, float gain)
        {
            loadRevision2(processor);
            set(processor, "sync", 0); set(processor, "mix", 100); set(processor, "threshold", -60);
            for (int engine = 0; engine < 6; ++engine)
                set(processor, ("on" + juce::String(engine)).toRawUTF8(), engine == mode ? 1.f : 0.f);
            auto curve = processor.getCurveSnapshot(mode);
            curve.custom = true; curve.linear = true; curve.count = 2; curve.amount = 100;
            curve.attack = .1f; curve.release = 5; curve.phase = 0; curve.length = 1;
            curve.points[0] = {0, gain}; curve.points[1] = {1, gain};
            processor.setCustomCurve(mode, curve);
        };
        configure(ducked, 0); configure(open, 1);
        const auto a = renderModern(ducked), b = renderModern(open);
        double duckedEnergy = 0, openEnergy = 0;
        for (size_t sample = 4096; sample < a.size(); ++sample)
        {
            duckedEnergy += a[sample] * a[sample];
            openEnergy += b[sample] * b[sample];
        }
        const bool responds = openEnergy > 1 && duckedEnergy < openEnergy * .9;
        allRespond &= responds;
        std::cout << "Modern engine " << mode << " flat-curve energy ratio "
                  << duckedEnergy / juce::jmax(1e-20, openEnergy) << "\n";
    }
    check(allRespond, "all six schema 2 engines, including Morph and Transient, audibly honor their own custom curve");

    auto selectedClassicOwner = std::make_unique<OpenDuckProcessor>();
    auto &selectedClassic = *selectedClassicOwner;
    auto selectedTransientOwner = std::make_unique<OpenDuckProcessor>();
    auto &selectedTransient = *selectedTransientOwner;
    loadRevision2(selectedClassic); loadRevision2(selectedTransient);
    for (int mode = 0; mode < 6; ++mode)
    {
        selectedClassic.applyBehaviorPreset(57 + mode * 103, mode);
        selectedTransient.applyBehaviorPreset(57 + mode * 103, mode);
        set(selectedClassic, ("on" + juce::String(mode)).toRawUTF8(), 1);
        set(selectedTransient, ("on" + juce::String(mode)).toRawUTF8(), 1);
    }
    set(selectedClassic, "sync", 0); set(selectedTransient, "sync", 0);
    set(selectedClassic, "selected", 0); set(selectedTransient, "selected", 5);
    const auto classic = renderModern(selectedClassic), transient = renderModern(selectedTransient);
    check(classic == transient, "changing only the visible editor mode produces bit-identical audio with six engines active");

    bool valid = true;
    std::mt19937 random(993);
    for (double sampleRate : {44100.0, 48000.0, 96000.0})
        for (int blockSize : {1, 17, 64, 257, 1024})
        {
            auto processorOwner = std::make_unique<OpenDuckProcessor>();
            auto &processor = *processorOwner;
            loadRevision2(processor);
            set(processor, "sync", 0);
            for (int mode = 0; mode < 6; ++mode)
                set(processor, ("on" + juce::String(mode)).toRawUTF8(), 1);
            processor.prepareToPlay(sampleRate, blockSize);
            juce::AudioBuffer<float> audio(2, blockSize);
            juce::MidiBuffer midi;
            for (int iteration = 0; iteration < 30; ++iteration)
            {
                const int mode = iteration % 6;
                processor.applyBehaviorPreset(static_cast<int>(random() % 700), mode);
                set(processor, OpenDuckProcessor::curveParameterId("phase", mode).toRawUTF8(),
                    static_cast<float>(static_cast<int>(random() % 201) - 100));
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < blockSize; ++sample)
                        audio.setSample(channel, sample, .2f);
                processor.processBlock(audio, midi);
                for (int sample = 0; sample < blockSize; ++sample)
                    valid &= std::isfinite(audio.getSample(0, sample)) && std::abs(audio.getSample(0, sample)) < 1;
            }
        }
    check(valid, "schema 2 factory/preset/phase automation stays finite at three sample rates and five block sizes");
}

template <typename Check>
void bandFocus(Check &&check)
{
    auto focusedOwner = std::make_unique<SpectralEngine>();
    auto &focused = *focusedOwner;
    std::array<SpectralEngine::FocusMask, 6> masks{};
    masks[0].depth = .8f; masks[0].cutoff = 160;
    double bassEnergy = 0, trebleEnergy = 0;
    for (int sample = 0; sample < 48000; ++sample)
    {
        float input[2]{std::sin(static_cast<float>(juce::MathConstants<double>::twoPi * 80 * sample / 48000)),
                       std::sin(static_cast<float>(juce::MathConstants<double>::twoPi * 6000 * sample / 48000))};
        float output[2]{};
        focused.process(input, 0, output, 2, 0, 40, 8000, 48000, &masks);
        if (sample > 4096)
        {
            bassEnergy += output[0] * output[0]; trebleEnergy += output[1] * output[1];
        }
    }
    check(trebleEnergy > 1000 && bassEnergy < trebleEnergy * .2,
          "160 Hz BAND FOCUS attenuates an 80 Hz tone while preserving a 6 kHz tone");
    auto bypassedOwner = std::make_unique<SpectralEngine>();
    auto &bypassed = *bypassedOwner;
    auto zeroMaskOwner = std::make_unique<SpectralEngine>();
    auto &zeroMask = *zeroMaskOwner;
    masks.fill({});
    std::mt19937 random(302);
    std::uniform_real_distribution<float> noise(-.5f, .5f);
    bool identical = true;
    for (int sample = 0; sample < 16000; ++sample)
    {
        float input[2]{noise(random), noise(random)}, a[2]{}, b[2]{};
        bypassed.process(input, 0, a, 2, .6f, 100, 3000, 48000);
        zeroMask.process(input, 0, b, 2, .6f, 100, 3000, 48000, &masks);
        identical &= a[0] == b[0] && a[1] == b[1];
    }
    check(identical, "disabled BAND FOCUS masks preserve bit-identical legacy spectral processing");
}

template <typename Check>
void editorControls(Check &&check)
{
    auto processorOwner = std::make_unique<OpenDuckProcessor>();
    auto &processor = *processorOwner;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool present = true;
    for (const char *name : {"behaviorPrevious", "behaviorNext", "behaviorMenu", "editPoints", "freeDraw", "curveCanvas"})
        present &= findNamed(*editor, name) != nullptr;
    for (int shape = 0; shape < 16; ++shape)
        present &= findNamed(*editor, "shape" + juce::String(shape)) != nullptr;
    check(present, "native editor exposes previous/next/dropdown library controls, all 16 shapes and opt-in curve tools");
    auto invoke = [&](const char *name)
    {
        auto *button = dynamic_cast<juce::Button *>(findNamed(*editor, name));
        if (button == nullptr || !button->onClick)
            return false;
        button->onClick();
        return true;
    };
    const auto sectorIds = OpenDuckProcessor::sectorPresetIds(0);
    const bool previous = invoke("behaviorPrevious");
    const int afterPrevious = processor.currentBehaviorPresetId();
    const bool next = invoke("behaviorNext");
    check(previous && next && afterPrevious == sectorIds.back() && processor.currentBehaviorPresetId() == sectorIds.front(),
          "current library arrows wrap through the selected sector and return to its INIT");
    std::array<OpenDuckProcessor::CurveSnapshot, 6> before;
    for (int mode = 0; mode < 6; ++mode)
        before[(size_t)mode] = processor.getCurveSnapshot(mode);
    const bool selected = invoke("mode5");
    bool unchanged = selected && processor.value("selected") == 5;
    for (int mode = 0; mode < 6; ++mode)
        unchanged &= equalCurve(before[(size_t)mode], processor.getCurveSnapshot(mode)) &&
                     processor.value(("on" + juce::String(mode)).toRawUTF8()) == (mode == 0 ? 1.f : 0.f);
    check(unchanged, "editor mode buttons change the visible page while preserving every curve and engine ON/OFF state");
}

inline juce::MouseEvent curveEvent(juce::Component &component, juce::Point<float> position,
                                  int modifiers, juce::Point<float> start, bool dragged = false, int clicks = 1)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(modifiers),
            1.f, 0.f, 0.f, 0.f, 0.f, &component, &component, now, start, now, clicks, dragged};
}

template <typename Check>
void nativeCurveGestures(Check &&check)
{
    auto processorOwner = std::make_unique<OpenDuckProcessor>();
    auto &processor = *processorOwner;
    check(loadRevision2(processor), "schema 2 editor gesture fixture loads without upgrading audio");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(1648, 830); editor->resized();
    auto *canvas = findNamed(*editor, "curveCanvas");
    if (canvas == nullptr)
    {
        check(false, "native curve canvas is available for mouse gesture regression checks");
        return;
    }
    const auto position = [&](float x, float y)
    {
        return juce::Point<float>(canvas->getWidth() * x, canvas->getHeight() * y);
    };
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    constexpr int controlLeft = juce::ModifierKeys::ctrlModifier | left;
    auto centre = position(.5f, .55f);
    const auto initial = processor.getCurveSnapshot();
    canvas->mouseDoubleClick(curveEvent(*canvas, centre, left, centre, false, 2));
    const bool doubleClickIgnored = equalCurve(initial, processor.getCurveSnapshot());
    const auto start = position(.35f, .55f), end = position(.5f, .55f);
    canvas->mouseDown(curveEvent(*canvas, start, left, start));
    canvas->mouseDrag(curveEvent(*canvas, end, left, start, true));
    canvas->mouseUp(curveEvent(*canvas, end, 0, start, true));
    check(doubleClickIgnored && !processor.getCurveSnapshot().custom &&
              processor.getCurveSnapshot().phase > 0 && processor.getCurveSnapshot().phase < 8 &&
              !processor.editPointsEnabled() && !processor.usesRevision3(0),
          "schema 2 INIT ignores point creation; gentle dragging moves the whole curve without upgrading audio");

    const auto resetFlat = [&]
    {
        auto flat = processor.getCurveSnapshot();
        flat.phase = 0; flat.length = 1; flat.tension = 0; flat.amount = 100;
        flat.custom = true; flat.linear = false; flat.count = 2;
        flat.points[0] = {0, .5f}; flat.points[1] = {1, .5f};
        flat.edit = true; flat.freeDraw = false;
        processor.setCustomCurve(0, flat);
        processor.setEditPointsEnabled(true, 0);
    };
    resetFlat();
    canvas->mouseDoubleClick(curveEvent(*canvas, centre, left, centre, false, 2));
    check(processor.getCurveSnapshot().custom && processor.getCurveSnapshot().count == 3,
          "double click adds a user-positioned XY point only after EDIT POINTS is enabled");

    resetFlat();
    auto legacyIncline = processor.getCurveSnapshot();
    legacyIncline.points[0].y = .25f; legacyIncline.points[1].y = .75f;
    processor.setCustomCurve(0, legacyIncline); processor.undo.clearUndoHistory();
    const auto wheelBefore = processor.getCurveSnapshot();
    const auto controlPlot = static_cast<OpenDuckEditor &>(*editor).getCurvePlotBounds();
    const auto controlCentre = controlPlot.getCentre();
    std::array<bool, 6> revision3Before{};
    for (int mode = 0; mode < 6; ++mode) revision3Before[(size_t)mode] = processor.usesRevision3(mode);
    const auto unchangedRevision3 = [&]
    {
        bool unchanged = true;
        for (int mode = 0; mode < 6; ++mode) unchanged &= processor.usesRevision3(mode) == revision3Before[(size_t)mode];
        return unchanged && !processor.usesRevision3(0);
    };
    juce::MouseWheelDetails wheel{0, .1f, false, false, false};
    canvas->mouseWheelMove(curveEvent(*canvas, controlCentre, juce::ModifierKeys::ctrlModifier, controlCentre), wheel);
    const auto wheelUp = processor.getCurveSnapshot();
    wheel.deltaY = -.1f;
    canvas->mouseWheelMove(curveEvent(*canvas, controlCentre, juce::ModifierKeys::ctrlModifier, controlCentre), wheel);
    const auto wheelDown = processor.getCurveSnapshot();
    check(controlPlot.contains(controlCentre) && wheelBefore.count == 2 &&
              equalCurve(wheelBefore, wheelUp) && equalCurve(wheelBefore, wheelDown) &&
              wheelBefore.points[0].reversed == wheelDown.points[0].reversed &&
              wheelBefore.points[1].reversed == wheelDown.points[1].reversed &&
              OpenDuckProcessor::sampleCurve(wheelBefore, .4f) == OpenDuckProcessor::sampleCurve(wheelUp, .4f) &&
              OpenDuckProcessor::sampleCurve(wheelBefore, .4f) == OpenDuckProcessor::sampleCurve(wheelDown, .4f) &&
              !processor.undo.canUndo() && unchangedRevision3(),
          "schema 2 Ctrl wheel inside the plot preserves its two anchors, segment data and flags without unsupported curvature or audio upgrade");

    resetFlat();
    legacyIncline = processor.getCurveSnapshot();
    legacyIncline.points[0].y = .25f; legacyIncline.points[1].y = .75f;
    processor.setCustomCurve(0, legacyIncline); processor.undo.clearUndoHistory();
    const auto dragBefore = processor.getCurveSnapshot();
    const auto up = controlCentre.translated(0, -controlPlot.getHeight() * .15f);
    canvas->mouseDown(curveEvent(*canvas, controlCentre, controlLeft, controlCentre));
    canvas->mouseDrag(curveEvent(*canvas, up, controlLeft, controlCentre, true));
    canvas->mouseUp(curveEvent(*canvas, up, 0, controlCentre, true));
    const auto dragUp = processor.getCurveSnapshot();
    const auto down = controlCentre.translated(0, controlPlot.getHeight() * .15f);
    canvas->mouseDown(curveEvent(*canvas, controlCentre, controlLeft, controlCentre));
    canvas->mouseDrag(curveEvent(*canvas, down, controlLeft, controlCentre, true));
    canvas->mouseUp(curveEvent(*canvas, down, 0, controlCentre, true));
    const auto dragDown = processor.getCurveSnapshot();
    check(controlPlot.contains(controlCentre) && controlPlot.contains(up) && controlPlot.contains(down) &&
              dragBefore.count == 2 && equalCurve(dragBefore, dragUp) && equalCurve(dragBefore, dragDown) &&
              dragBefore.points[0].reversed == dragDown.points[0].reversed &&
              dragBefore.points[1].reversed == dragDown.points[1].reversed &&
              OpenDuckProcessor::sampleCurve(dragBefore, .4f) == OpenDuckProcessor::sampleCurve(dragUp, .4f) &&
              OpenDuckProcessor::sampleCurve(dragBefore, .4f) == OpenDuckProcessor::sampleCurve(dragDown, .4f) &&
              !processor.undo.canUndo() && unchangedRevision3(),
          "schema 2 Ctrl drag inside the plot preserves its two anchors, segment data and flags without automatic points or audio upgrade");

    resetFlat(); processor.setFreeDrawEnabled(true, 0);
    const auto first = position(.2f, .78f);
    canvas->mouseDown(curveEvent(*canvas, first, left, first));
    for (auto point : {position(.4f, .25f), position(.6f, .78f), position(.8f, .25f)})
        canvas->mouseDrag(curveEvent(*canvas, point, left, first, true));
    canvas->mouseUp(curveEvent(*canvas, position(.8f, .25f), 0, first, true));
    const auto drawn = processor.getCurveSnapshot();
    check(drawn.custom && drawn.linear && drawn.freeDraw && drawn.count > 32 &&
              OpenDuckProcessor::sampleCurve(drawn, .4f) > OpenDuckProcessor::sampleCurve(drawn, .6f) + .3f,
          "FREE DRAW mouse path preserves a freely drawn alternating sawtooth contour");

    // Revision 3 uses a separate Advanced overlay, so header controls no longer
    // consume or share the waveform canvas hit region.
    auto layoutOwner = std::make_unique<OpenDuckProcessor>();
    auto &layoutProcessor = *layoutOwner;
    std::unique_ptr<juce::AudioProcessorEditor> layoutEditor(layoutProcessor.createEditor());
    layoutEditor->setSize(1767, 890); layoutEditor->resized();
    layoutEditor->setVisible(true); // Offscreen component hit-testing requires JUCE visibleFlag; no native peer is created.
    auto *detailsButton = dynamic_cast<juce::Button *>(findNamed(*layoutEditor, "details"));
    auto *grooveButton = dynamic_cast<juce::Button *>(findNamed(*layoutEditor, "mode4"));
    auto *closeButton = dynamic_cast<juce::Button *>(findNamed(*layoutEditor, "advancedClose"));
    auto *pane = findNamed(*layoutEditor, "advancedPane");
    auto *layoutCanvas = findNamed(*layoutEditor, "curveCanvas");
    if (!detailsButton || !grooveButton || !closeButton || !pane || !layoutCanvas ||
        !detailsButton->onClick || !grooveButton->onClick || !closeButton->onClick)
    {
        check(false, "Groove Advanced overlay gesture regression controls are available");
        return;
    }
    detailsButton->onClick(); grooveButton->onClick();
    const auto grooveBefore = layoutProcessor.getCurveSnapshot(4);
    const auto canvasCentreInEditor = layoutEditor->getLocalPoint(layoutCanvas, layoutCanvas->getLocalBounds().getCentre());
    auto *hit = layoutEditor->getComponentAt(canvasCentreInEditor);
    check(pane->isVisible() && hit != layoutCanvas && (hit == pane || pane->isParentOf(hit)) &&
              equalCurve(grooveBefore, layoutProcessor.getCurveSnapshot(4)),
          "opening Advanced then selecting Groove routes covered graph hits into the overlay without curve edits");
    closeButton->onClick();
    const auto plotStart = juce::Point<float>(layoutCanvas->getWidth() * .35f, layoutCanvas->getHeight() * .80f);
    const auto plotEnd = juce::Point<float>(layoutCanvas->getWidth() * .50f, layoutCanvas->getHeight() * .80f);
    layoutCanvas->mouseDown(curveEvent(*layoutCanvas, plotStart, left, plotStart));
    layoutCanvas->mouseDrag(curveEvent(*layoutCanvas, plotEnd, left, plotStart, true));
    layoutCanvas->mouseUp(curveEvent(*layoutCanvas, plotEnd, 0, plotStart, true));
    const float phaseChange = layoutProcessor.getCurveSnapshot(4).phase - grooveBefore.phase;
    check(!pane->isVisible() && phaseChange > 0 && phaseChange < 8,
          "closing Advanced restores gentle Groove whole-curve phase dragging");
}

template <typename Check>
void legacyCompatibility(Check &&check)
{
    auto legacyOwner = std::make_unique<OpenDuckProcessor>();
    auto &legacy = *legacyOwner;
    check(loadLegacy(legacy), "archived v0.1.0 session fixture is available and loads");
    juce::StringArray ids{
        "amount", "attack", "release", "tension", "shift", "mix", "swing", "freeBpm",
        "threshold", "hold", "detectorAttack", "detectorRelease", "morph", "spectralLow",
        "spectralHigh", "bandLow", "bandMid", "bandHigh", "crossLow", "crossHigh",
        "hysteresis", "grooveDepth", "spectralDepth", "bandAttack0", "bandRelease0",
        "bandAttack1", "bandRelease1", "bandAttack2", "bandRelease2", "sync", "bypass"};
    for (int mode = 0; mode < 6; ++mode)
    {
        ids.add("on" + juce::String(mode));
        ids.add("solo" + juce::String(mode));
    }
    ids.addArray({"selected", "shape", "trigger", "grid"});
    for (int step = 0; step < 16; ++step)
        ids.add("step" + juce::String(step));
    for (int point = 0; point < 8; ++point)
        ids.add("point" + juce::String(point));
    bool orderPreserved = legacy.getParameters().size() >= ids.size();
    for (int index = 0; index < ids.size() && orderPreserved; ++index)
    {
        auto *parameter = dynamic_cast<juce::AudioProcessorParameterWithID *>(legacy.getParameters()[index]);
        orderPreserved &= parameter != nullptr && parameter->paramID == ids[index];
    }
    check(orderPreserved, "all 71 legacy parameter IDs retain their original host automation order");
    bool rangesPreserved = true;
    auto range = [&](const char *id, float minimum, float maximum)
    {
        auto *parameter = legacy.state.getParameter(id);
        rangesPreserved &= parameter != nullptr && parameter->convertFrom0to1(0) == minimum &&
                           parameter->convertFrom0to1(1) == maximum;
    };
    range("amount", 0, 100); range("attack", 0.1f, 100); range("release", 5, 2000);
    range("tension", -100, 100); range("shift", -100, 100); range("mix", 0, 100);
    range("swing", 0, 75); range("freeBpm", 30, 300); range("threshold", -60, 0);
    range("hold", 0, 500); range("detectorAttack", 0.1f, 20); range("detectorRelease", 1, 200);
    range("morph", 0, 1); range("spectralLow", 20, 2000); range("spectralHigh", 2001, 20000);
    range("bandLow", 0, 1); range("bandMid", 0, 1); range("bandHigh", 0, 1);
    range("crossLow", 40, 500); range("crossHigh", 1000, 10000); range("hysteresis", 1, 18);
    range("grooveDepth", 0, 1); range("spectralDepth", 0, 1);
    for (int mode = 0; mode < 3; ++mode)
    {
        range(("bandAttack" + juce::String(mode)).toRawUTF8(), 0.1f, 100);
        range(("bandRelease" + juce::String(mode)).toRawUTF8(), 5, 2000);
    }
    range("shape", 0, 5); range("selected", 0, 5); range("trigger", 0, 2); range("grid", 0, 6);
    for (int point = 0; point < 8; ++point)
        range(("point" + juce::String(point)).toRawUTF8(), 0, 1);
    check(rangesPreserved, "legacy continuous ranges and choice indices remain unchanged");

    set(legacy, "sync", 0);
    set(legacy, "shape", 3);
    set(legacy, "attack", 0.1f);
    set(legacy, "release", 5);
    legacy.prepareToPlay(48000, 512);
    juce::WavAudioFormat wav;
    auto stream = fixture("legacy-v0.1.0-audio-stress-48k.wav").createInputStream();
    auto reader = stream ? std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(stream.release(), true)) : nullptr;
    bool referenceValid = reader != nullptr && reader->lengthInSamples == 384000 && reader->sampleRate == 48000;
    check(referenceValid, "archived v0.1.0 24-bit audio reference contains all 384000 stereo samples");
    double maximumError = 0;
    if (referenceValid)
    {
        juce::AudioBuffer<float> audio(2, 512), reference(2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 750; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    audio.setSample(channel, sample,
                        static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * 1000 *
                                                         (block * 512 + sample) / 48000)));
            legacy.processBlock(audio, midi);
            reader->read(&reference, 0, 512, block * 512, true, true);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 512; ++sample)
                    maximumError = juce::jmax(maximumError,
                        static_cast<double>(std::abs(audio.getSample(channel, sample) - reference.getSample(channel, sample))));
        }
    }
    std::cout << "Legacy archived-render maximum error " << maximumError << "\n";
    check(referenceValid && maximumError <= 1.3e-7,
          "schema 1 audio matches independent v0.1.0 render to 24-bit PCM precision");
}
}
