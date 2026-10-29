// Pure display-contract checks can run with /DODX_SCOPE_PURE_TESTS.
// The normal target additionally exercises the real JUCE component metadata
// against a real PCM fixture. No audio/DSP result is inferred from the pure tests.
#include "../Source/ScopeWaveformView.h"
#include <iostream>
#include <string>
#ifndef ODX_SCOPE_PURE_TESTS
#include "PluginProcessor.h"
#include "PluginEditor.h"
#endif

namespace
{
int passed = 0, failed = 0;
void check(bool value, const std::string& name)
{
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    value ? ++passed : ++failed;
}
bool near(double a, double b, double tolerance = 1.e-5) { return std::abs(a - b) <= tolerance; }
void pureContracts()
{
    using namespace odx::view;
    std::array<ScopeFreshness, 6> clocks{};
    for (int mode = 0; mode < 6; ++mode)
    {
        auto& clock = clocks[(size_t)mode];
        const std::string tag = " sector " + std::to_string(mode);
        bool states = clock.observe(10, .2f, .4, true, true, false, 1000) == ScopeState::held;
        states &= clock.observe(10, .3f, .4, true, true, false, 1033) == ScopeState::live;
        states &= clock.observe(10, .3f, .4, true, true, false, 1284) == ScopeState::held;
        states &= clock.observe(11, .3f, .4, true, true, false, 1300) == ScopeState::live;
        states &= clock.observe(11, .3f, .4, true, true, true, 9000) == ScopeState::frozen;
        states &= clock.observe(11, .3f, .4, true, false, true, 9010) == ScopeState::periodMismatch;
        check(states, "injected clock distinguishes observed motion, held, new generation, Freeze and period mismatch" + tag);
        // A moving cursor alone cannot keep a picture live indefinitely.
        ScopeFreshness stale; stale.observe(4, .1f, .4, true, true, false, 100);
        check(stale.observe(4, .6f, .4, true, true, false, 1101) == ScopeState::held,
              "phase activity does not disguise an unpublished stale frame" + tag);
        const double period = 60. / (mode % 2 ? 168 : 145);
        const auto axis = scopeAxis(period, .25f, 4, .75f);
        bool units = near(axis.startMs, period * 250) && near(axis.endMs, period * 500)
            && near(axis.cursorMs, period * 750) && !axis.cursorInView && near(axis.cursorPosition, 2);
        for (size_t i = 0; i < 5; ++i) units &= near(axis.ticksMs[i], period * (250 + 62.5 * i));
        check(units, "millisecond quarters, zoom window and actual off-screen cursor share one axis" + tag);
    }
    clocks[0].observe(99, .4f, .4, true, true, false, 9020);
    bool independent = clocks[0].generation == 99;
    for (size_t i = 1; i < clocks.size(); ++i) independent &= clocks[i].generation == 11;
    check(independent, "a new frame in one sector leaves the five other display clocks unchanged");
    check(scopePeriodsMatch(60. / 145, 60. / 145) && !scopePeriodsMatch(60. / 145, 60. / 168)
          && !scopePeriodsMatch(0, .5) && !scopePeriodsMatch(std::numeric_limits<double>::quiet_NaN(), .5),
          "changed tempo and invalid periods cannot relabel a captured waveform");
    ScopeFreshness empty;
    check(empty.observe(0, 0, .5, false, false, false, 1000) == ScopeState::waiting,
          "an uncaptured initial buffer is WAITING rather than a fictitious held frame");

    std::array<float, 512> low{}, high{};
    for (int i = 0; i < 512; ++i)
    {
        const double t = i / 512.;
        const float value = t < .25 ? float(std::exp(-t / .02)) : 0;
        low[(size_t)i] = -value; high[(size_t)i] = value;
    }
    const auto hit = isolateSingleHit(low, high, 1.0);
    const double duration = singleHitDurationMs(low, high, hit, 1.0);
    // exp(-t/.02) reaches amplitude .01 at 92.103ms, not power .0001 at184.2ms.
    check(hit.detectedOnsets == 1 && duration > 90 && duration < 96,
          "HIT duration uses -40dB amplitude 20log10 and quiet-tail confirmation");
    for (int i = 32; i < 512; ++i) { low[(size_t)i] = 0; high[(size_t)i] = 0; }
    const auto cut = isolateSingleHit(low, high, 1.0);
    check(singleHitDurationMs(low, high, cut, 1.0) > 0
          && singleHitDurationMs(low, high, cut, 1.0, 32) < 0,
          "uncaptured zero-filled bins cannot certify a -40dB tail beyond an early End Marker");
    low.fill(0); high.fill(0); low[510] = low[511] = -1; high[510] = high[511] = 1;
    SingleHitSpan crossing; crossing.startBin = 505; crossing.onsetBin = 510;
    crossing.binCount = 40; crossing.detectedOnsets = 1; crossing.hasAudio = true;
    check(singleHitDurationMs(low, high, crossing, 1.0) > 0
          && singleHitDurationMs(low, high, crossing, 1.0, 511) < 0,
          "a circular tail estimate rejects any uncaptured bin across the wrap");
    for (int i = 0; i < 512; ++i) { low[(size_t)i] = -.5f; high[(size_t)i] = .5f; }
    auto sustained = isolateSingleHit(low, high, 1.0);
    check(sustained.hasAudio && sustained.detectedOnsets == 0
          && singleHitDurationMs(low, high, sustained, 1.0) < 0,
          "steady material has no fabricated kick duration");
    for (int i = 256; i < 512; ++i) { low[(size_t)i] = 0; high[(size_t)i] = 0; }
    const auto partialSteady = isolateSingleHit(low, high, 1.0, 256);
    check(partialSteady.hasAudio && partialSteady.detectedOnsets == 0 && partialSteady.binCount == 0,
          "a partial constant signal has no invented attack at its unobserved capture border");
    bool partialTones = true;
    for (double frequency : {20., 50., 440.})
    {
        low.fill(0); high.fill(0);
        for (int bin = 0; bin < 256; ++bin) for (int sample = 0; sample < 64; ++sample)
        {
            const float value = (float)std::sin((bin + sample / 64.) / 512 * frequency * 6.283185307179586);
            low[(size_t)bin] = std::min(low[(size_t)bin], value); high[(size_t)bin] = std::max(high[(size_t)bin], value);
        }
        const auto tone = isolateSingleHit(low, high, 1.0, 256);
        partialTones &= tone.hasAudio && tone.detectedOnsets == 0 && tone.binCount == 0;
    }
    check(partialTones, "partial20/50/440Hz steady tones cannot use missing prehistory as a kick onset");
    low.fill(0); high.fill(0);
    check(isolateSingleHit(low, high, 1.0).binCount == 0,
          "silence has no visible attack or HIT estimate");
    for (int i = 0; i < 512; ++i)
    {
        const int age = i % 128;
        high[(size_t)i] = age < 100 ? 1.f : .02f;
        low[(size_t)i] = -high[(size_t)i];
    }
    // Deliberately never reaches -40dB before the next hit.
    auto truncated = isolateSingleHit(low, high, 1.0);
    check(singleHitDurationMs(low, high, truncated, 1.0) < 0,
          "a tail that does not settle before the next attack is not assigned a duration");
}

#ifndef ODX_SCOPE_PURE_TESTS
juce::Component* find(juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren()) if (auto* result = find(*child, name)) return result;
    return nullptr;
}
juce::MouseEvent mouse(juce::Component& component, juce::Point<float> position, int modifiers,
                       juce::Point<float> start, bool dragged = false)
{
    const auto time = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(modifiers),
            1.f, 0.f, 0.f, 0.f, 0.f, &component, &component, time, start, time, 1, dragged};
}
bool sameCurve(const OpenDuckProcessor::CurveSnapshot& a, const OpenDuckProcessor::CurveSnapshot& b)
{
    if (a.count != b.count || a.shape != b.shape || a.custom != b.custom || a.linear != b.linear
        || a.edit != b.edit || a.freeDraw != b.freeDraw || a.modern3 != b.modern3
        || a.phase != b.phase || a.length != b.length || a.tension != b.tension
        || a.amount != b.amount || a.attack != b.attack || a.release != b.release || a.riseSlope != b.riseSlope) return false;
    for (int i = 0; i < a.count; ++i)
    {
        const auto& x = a.points[(size_t)i]; const auto& y = b.points[(size_t)i];
        if (x.x != y.x || x.y != y.y || x.segment != y.segment || x.curvature != y.curvature || x.reversed != y.reversed) return false;
    }
    return true;
}
bool sameAnchors(const OpenDuckProcessor::CurveSnapshot& a, const OpenDuckProcessor::CurveSnapshot& b)
{
    auto copy = b; copy.tension = a.tension;
    for (int i = 0; i < a.count && i < copy.count; ++i) copy.points[(size_t)i].curvature = a.points[(size_t)i].curvature;
    return sameCurve(a, copy);
}
void curvatureGestures(OpenDuckProcessor& p, juce::AudioProcessorEditor& editor)
{
    auto* canvas = find(editor, "curveCanvas"); if (!canvas) { check(false, "curvature gesture canvas exists"); return; }
    bool isolated = true;
    for (int mode = 0; mode < 6; ++mode)
    {
        std::array<OpenDuckProcessor::CurveSnapshot, 6> otherBefore;
        for (int other = 0; other < 6; ++other) otherBefore[(size_t)other] = p.getCurveSnapshot(other);
        auto* button = dynamic_cast<juce::Button*>(find(editor, "mode" + juce::String(mode)));
        auto seed = p.getCurveSnapshot(mode); seed.shape = 0; seed.count = 0; seed.custom = false;
        seed.linear = false; seed.edit = false; seed.freeDraw = false; seed.phase = 0; seed.length = 1;
        seed.tension = 0; seed.amount = 100; seed.attack = 10; seed.release = 150; seed.riseSlope = 1;
        const auto exercise = [&](OpenDuckProcessor::CurveSnapshot curve, bool wheel, bool up)
        {
            p.setCurveSnapshot(mode, curve, "Curvature gesture fixture"); button->onClick(); p.undo.clearUndoHistory();
            const auto before = p.getCurveSnapshot(mode);
            const float cycle = p.rawToPreviewCycle(mode, .4f);
            const float original = p.previewCurveGain(mode, cycle);
            const auto plot = static_cast<OpenDuckEditor&>(editor).getCurvePlotBounds();
            const auto point = juce::Point<float>(plot.getX() + plot.getWidth() * .45f, plot.getCentreY());
            constexpr int ctrl = juce::ModifierKeys::ctrlModifier, left = juce::ModifierKeys::leftButtonModifier;
            bool returnedToStart = true;
            if (wheel)
            {
                const juce::MouseWheelDetails detail{0, up ? .1f : -.1f, false, false, false};
                canvas->mouseWheelMove(mouse(*canvas, point, ctrl, point), detail);
            }
            else
            {
                const auto end = point.translated(0, up ? -12.f : 12.f);
                canvas->mouseDown(mouse(*canvas, point, ctrl | left, point));
                canvas->mouseDrag(mouse(*canvas, point + (end - point) * .5f, ctrl | left, point, true));
                canvas->mouseDrag(mouse(*canvas, point, ctrl | left, point, true));
                returnedToStart = sameCurve(before, p.getCurveSnapshot(mode));
                canvas->mouseDrag(mouse(*canvas, end, ctrl | left, point, true));
                canvas->mouseUp(mouse(*canvas, end, 0, point, true));
            }
            const auto after = p.getCurveSnapshot(mode);
            const float result = p.previewCurveGain(mode, cycle);
            bool local = sameAnchors(before, after);
            if (before.custom)
            {
                local &= before.tension == after.tension;
                for (int i = 0; i < before.count; ++i) if (i != 1)
                    local &= before.points[(size_t)i].curvature == after.points[(size_t)i].curvature;
            }
            const bool bent = up ? result > original + 1.e-6f : result < original - 1.e-6f;
            const bool undo = p.undo.undo() && sameCurve(before, p.getCurveSnapshot(mode));
            return local && bent && undo && returnedToStart;
        };
        bool factory = true;
        for (bool wheel : {true, false}) for (bool up : {true, false}) factory &= exercise(seed, wheel, up);
        check(factory, "Ctrl wheel/drag bends the factory with points OFF, no auto anchors and one Undo per gesture sector " + std::to_string(mode));
        for (int variant = 0; variant < 3; ++variant)
        {
            auto custom = seed; custom.custom = true; custom.shape = 16; custom.count = 4;
            custom.edit = variant != 2; custom.freeDraw = variant == 1; custom.linear = variant == 1;
            custom.points[0] = {0, .1f, 5}; custom.points[1] = {.05f, .25f, 0};
            custom.points[2] = {.95f, .75f, 3}; custom.points[3] = {1, .9f, 4};
            if (variant == 1) for (int i = 0; i < 4; ++i) custom.points[(size_t)i].y = 1 - custom.points[(size_t)i].y;
            if (variant == 2) custom.points[1].reversed = true;
            bool curves = true;
            for (bool wheel : {true, false}) for (bool up : {true, false}) curves &= exercise(custom, wheel, up);
            check(curves, std::string("Ctrl wheel/drag changes only the existing ")
                  + (variant == 0 ? "rising" : variant == 1 ? "falling linear/free-draw" : "reversed points-OFF")
                  + " segment, preserves anchors/types/flags and supports one Undo sector " + std::to_string(mode));
        }
        auto flat = seed; flat.custom = true; flat.shape = 16; flat.count = 2; flat.edit = true;
        flat.points[0] = {0, .5f}; flat.points[1] = {1, .5f};
        p.setCurveSnapshot(mode, flat, "Flat no-op fixture"); button->onClick(); p.undo.clearUndoHistory();
        const auto before = p.getCurveSnapshot(mode);
        const auto plot = static_cast<OpenDuckEditor&>(editor).getCurvePlotBounds();
        const auto point = juce::Point<float>(plot.getX() + plot.getWidth() * .45f, plot.getCentreY());
        constexpr int ctrl = juce::ModifierKeys::ctrlModifier, left = juce::ModifierKeys::leftButtonModifier;
        for (bool up : {true, false})
        {
            const juce::MouseWheelDetails detail{0, up ? .1f : -.1f, false, false, false};
            canvas->mouseWheelMove(mouse(*canvas, point, ctrl, point), detail);
            const auto end = point.translated(0, up ? -12.f : 12.f);
            canvas->mouseDown(mouse(*canvas, point, ctrl | left, point)); canvas->mouseDrag(mouse(*canvas, end, ctrl | left, point, true));
            canvas->mouseUp(mouse(*canvas, end, 0, point, true));
        }
        check(sameCurve(before, p.getCurveSnapshot(mode)) && !p.undo.canUndo(),
              "flat two-anchor Ctrl gestures are an explicit no-op with no stored ineffective curvature or auto points sector " + std::to_string(mode));
        for (int other = 0; other < 6; ++other) if (other != mode)
            isolated &= sameCurve(otherBefore[(size_t)other], p.getCurveSnapshot(other));
    }
    check(isolated, "curvature wheel and drag gestures leave all five other sectors unchanged");
}
void nativeContracts(const juce::File& fixtureFile, const juce::File& output)
{
    struct Clock final : juce::AudioPlayHead
    {
        double ppq = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info; info.setBpm(145); info.setPpqPosition(ppq); info.setIsPlaying(true); return info;
        }
    } clock;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(fixtureFile.createInputStream().release(), true));
    check(reader != nullptr, "real kick PCM fixture is available for native UI checks");
    if (!reader) return;
    juce::AudioBuffer<float> fixture(2, (int)reader->lengthInSamples);
    reader->read(&fixture, 0, fixture.getNumSamples(), 0, true, true);
    auto processor = std::make_unique<OpenDuckProcessor>(); auto& p = *processor;
    p.setPlayHead(&clock); auto layout = p.getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    check(p.setBusesLayout(layout), "native waveform fixture uses a real stereo sidechain bus");
    for (int mode = 0; mode < 6; ++mode) { p.setSectorParameter(mode, "trigger", 0); p.setSectorParameter(mode, "speed", 2); }
    p.prepareToPlay(reader->sampleRate, 257);
    std::cout << "INFO native processor prepared\n";
    int position = 0;
    bool sendInitialNote = false;
    auto feed = [&](int samples)
    {
        const int until = position + samples;
        while (position < until)
        {
            const int count = std::min(257, until - position); juce::AudioBuffer<float> audio(4, count); juce::MidiBuffer midi;
            if (sendInitialNote && position == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)100), 0);
            for (int i = 0; i < count; ++i) for (int ch = 0; ch < 2; ++ch)
            {
                const float value = fixture.getSample(ch, (position + i) % fixture.getNumSamples());
                audio.setSample(ch, i, value * .4f); audio.setSample(ch + 2, i, value);
            }
            clock.ppq = position / reader->sampleRate * 145 / 60;
            p.processBlock(audio, midi); position += count;
        }
    };
    feed((int)(reader->sampleRate * 3));
    std::cout << "INFO real audio fed; constructing staged editor\n";
    // Construct the staged class with its current sizeof. A C-only diagnostic
    // may link the old read-only processor archive, whose createEditor() was
    // compiled with the previous UI layout. Production rebuilds both sources.
    std::unique_ptr<juce::AudioProcessorEditor> editor = std::make_unique<OpenDuckEditor>(p);
    auto* source = dynamic_cast<juce::ComboBox*>(find(*editor, "scopeSource"));
    auto* zoom = dynamic_cast<juce::ComboBox*>(find(*editor, "scopeZoom"));
    auto* canvas = find(*editor, "curveCanvas");
    check(source && zoom && canvas, "real scope controls and metadata component exist");
    if (!source || !zoom || !canvas) return;
    for (int mode = 0; mode < 6; ++mode)
    {
        const auto beforeView = p.getCurveSnapshot(mode);
        auto* button = dynamic_cast<juce::Button*>(find(*editor, "mode" + juce::String(mode)));
        if (!button || !button->onClick) { check(false, "native mode callback exists"); continue; }
        button->onClick(); const auto initial = p.getScopeFrame(mode);
        feed((int)(reader->sampleRate * .035)); source->onChange();
        const auto frame = p.getScopeFrame(mode); const auto& properties = canvas->getProperties();
        const std::string tag = " sector " + std::to_string(mode);
        check(frame.complete && frame.bins == 512 && (int)properties["scopeMode"] == mode
              && (juce::int64)properties["scopeGeneration"] == (juce::int64)frame.generation
              && near((double)properties["scopeCapturedPeriodMs"], frame.periodSeconds * 1000)
              && near((double)properties["scopeCursorMs"], frame.phase * p.cycleSeconds(mode) * 1000, .0001)
              && properties["scopeState"].toString() == "LIVE" && (bool)properties["scopePeriodMatches"],
              "native cursor/freshness/period metadata follows actual processed frame" + tag);
        zoom->setSelectedId(3, juce::sendNotificationSync);
        check(near((double)properties["scopeViewEndMs"] - (double)properties["scopeViewStartMs"], p.cycleSeconds(mode) * 250)
              && near((double)properties["scopeTickMs4"], (double)properties["scopeViewEndMs"]),
              "native 4x zoom reports real milliseconds without stretching one hit" + tag);
        const double reference = (double)properties["scopeReferencePeak"];
        source->setSelectedId(3, juce::sendNotificationSync);
        check(near((double)properties["scopeReferencePeak"], reference), "OUTPUT preserves the common main/SC amplitude reference" + tag);
        source->setSelectedId(2, juce::sendNotificationSync);
        p.setScopeFrozen(true); source->onChange();
        check(properties["scopeState"].toString() == "FROZEN", "native Freeze reports an actual frozen frame" + tag);
        p.setSectorParameter(mode, "speed", 0); source->onChange();
        check(!(bool)properties["scopePeriodMatches"] && (int)properties["scopeVisibleOnsets"] == 0
              && properties["scopeState"].toString() == "WAITING"
              && near((double)properties["scopeCapturedPeriodMs"], frame.periodSeconds * 1000),
              "period change hides the old waveform rather than relabelling or stretching it" + tag);
        p.setSectorParameter(mode, "speed", 2); p.setScopeFrozen(false); zoom->setSelectedId(1, juce::sendNotificationSync); source->onChange();
        const auto afterView = p.getCurveSnapshot(mode);
        bool untouched = beforeView.shape == afterView.shape && beforeView.count == afterView.count
            && beforeView.custom == afterView.custom && beforeView.amount == afterView.amount
            && beforeView.phase == afterView.phase && beforeView.attack == afterView.attack
            && beforeView.release == afterView.release && beforeView.tension == afterView.tension
            && beforeView.length == afterView.length && p.value("mix") == 100;
        for (int i = 0; i < beforeView.count; ++i)
            untouched &= beforeView.points[(size_t)i].x == afterView.points[(size_t)i].x
                && beforeView.points[(size_t)i].y == afterView.points[(size_t)i].y
                && beforeView.points[(size_t)i].segment == afterView.points[(size_t)i].segment;
        check(untouched, "scope source, zoom, cursor and Freeze do not change the audio curve or MIX" + tag);
        (void)initial;
    }
    // A valid One Shot capture ends at End0.5, not necessarily at cycle1.
    // All three real sources remain visible; the missing half is never counted
    // as quiet data when estimating the tail duration.
    for (int mode = 0; mode < 6; ++mode)
    {
        p.setSectorParameter(mode, "trigger", 2); p.setSectorParameter(mode, "loop", 0);
        p.setSectorParameter(mode, "end", .5f); p.setSectorParameter(mode, "endHold", 0);
    }
    position = 0; sendInitialNote = true; p.prepareToPlay(reader->sampleRate, 257); feed((int)reader->sampleRate);
    sendInitialNote = false;
    for (int mode = 0; mode < 6; ++mode)
    {
        auto* button = dynamic_cast<juce::Button*>(find(*editor, "mode" + juce::String(mode))); button->onClick();
        const auto partial = p.getScopeFrame(mode); const auto& properties = canvas->getProperties();
        bool visible = partial.complete && partial.bins > 0 && partial.bins < 512;
        for (int sourceId : {1, 2, 3})
        {
            source->setSelectedId(sourceId, juce::sendNotificationSync); source->onChange();
            visible &= (bool)properties["scopeFrameComplete"] && (int)properties["scopeFrameBins"] == partial.bins
                && (int)properties["scopeVisibleOnsets"] == 1 && (double)properties["scopeHitDurationMs"] < 0;
        }
        check(visible, "real End0.5 One Shot keeps INPUT/SC/OUTPUT visible and leaves an unobserved tail unknown sector " + std::to_string(mode));
    }
    // Worst-case footer at minimum width: long physical period,8x zoom and a
    // genuine cursor outside the view, then a real parameter-period mismatch.
    for (int mode = 0; mode < 6; ++mode) { p.setSectorParameter(mode, "trigger", 0); p.setSectorParameter(mode, "end", 1); }
    p.setSectorParameter(5, "speedMode", 1); p.setSectorParameter(5, "hz", .05f);
    position = 0; p.prepareToPlay(reader->sampleRate, 257); feed((int)(reader->sampleRate * 25));
    editor->setSize(1010, 509); source->setSelectedId(2, juce::sendNotificationSync); zoom->setSelectedId(4, juce::sendNotificationSync);
    source->onChange(); editor->createComponentSnapshot(editor->getLocalBounds());
    auto& longProperties = canvas->getProperties();
    check((bool)longProperties["scopeFrameComplete"] && (bool)longProperties["scopePeriodMatches"]
          && (double)longProperties["scopeExpectedPeriodMs"] > 19999 && !(bool)longProperties["scopeCursorInView"]
          && longProperties["scopeNote"].toString().contains("OUTSIDE VIEW") && (bool)longProperties["scopeNoteFits"],
          "20-second cycle with8x zoom and an actual off-screen cursor fits the minimum-width footer");
    p.setSectorParameter(5, "hz", 40); source->onChange(); editor->createComponentSnapshot(editor->getLocalBounds());
    check(!(bool)longProperties["scopePeriodMatches"] && (int)longProperties["scopeVisibleOnsets"] == 0
          && longProperties["scopeNote"].toString().contains("NEW PERIOD") && (bool)longProperties["scopeNoteFits"],
          "long held frame versus short new target period is explicit and fits at minimum width");
    p.setSectorParameter(5, "speedMode", 0); p.setSectorParameter(5, "hz", 2);
    zoom->setSelectedId(1, juce::sendNotificationSync);
    auto* duckButton = dynamic_cast<juce::Button*>(find(*editor, "mode0")); duckButton->onClick();
    // Snapshot the genuine component at minimum size. This is an appearance
    // artifact, not a substitute for the assertions above.
    editor->setSize(1010, 509); source->onChange();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    const auto& metadata = canvas->getProperties();
    check(metadata["scopeNote"].toString().contains(" ms") && metadata["scopeNote"].toString().contains("VIEW")
          && (bool)metadata["scopeNoteFits"],
          "minimum-size native footer exposes millisecond view and cycle units without text overflow");
    output.createDirectory(); auto stream = output.getChildFile("native-waveform-width1010.png").createOutputStream();
    juce::PNGImageFormat png;
    const bool ready = stream && stream->setPosition(0) && stream->truncate().wasOk();
    check(ready && png.writeImageToStream(image, *stream), "minimum-size waveform screenshot written from the actual native component");
    curvatureGestures(p, *editor);
    p.releaseResources(); p.setPlayHead(nullptr);
}
#endif
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
#ifndef ODX_SCOPE_PURE_TESTS
    juce::ScopedJuceInitialiser_GUI gui;
#endif
    pureContracts();
#ifndef ODX_SCOPE_PURE_TESTS
    if (argc < 3) check(false, "usage: Revision031WaveformUiTests <real-kick.wav> <QA-output-directory>");
    else nativeContracts(juce::File(argv[1]), juce::File(argv[2]));
#else
    (void)argc; (void)argv;
#endif
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
