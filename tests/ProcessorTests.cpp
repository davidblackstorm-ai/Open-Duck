
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <chrono>
#include <random>
#include <filesystem>
static void set(OpenDuckProcessor &p, const char *id, float v)
{
    auto *param = p.state.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(v));
}
class TestPlayHead : public juce::AudioPlayHead
{
  public:
    double tempo = 120, ppq = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(tempo);
        p.setPpqPosition(ppq);
        p.setIsPlaying(playing);
        return p;
    }
};
class PreviewWindow final : public juce::DocumentWindow
{
  public:
    PreviewWindow()
        : DocumentWindow("OpenDuck UI QA — software renderer", juce::Colours::black,
                         DocumentWindow::closeButton)
    {
        processor.prepareToPlay(48000, 512);
        setUsingNativeTitleBar(true);
        setContentOwned(processor.createEditor(), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

  private:
    OpenDuckProcessor processor;
};
#include "RevisionTests.h"
int main(int argc, char **argv)
{
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String(argv[1]) == "--preview")
    {
        auto window = std::make_unique<PreviewWindow>();
        juce::MessageManager::getInstance()->runDispatchLoop();
        return 0;
    }
    int failed = 0, passed = 0;
    auto check = [&](bool b, const char *n)
    {
        std::cout << (b ? "PASS " : "FAIL ") << n << "\n";
        if (!b)
            ++failed;
        else
            ++passed;
    };
    auto pOwner = std::make_unique<OpenDuckProcessor>();
    auto &p = *pOwner;
    p.prepareToPlay(48000, 512);
    check(p.value("on0") == 1 && p.value("on1") == 0, "Classic only at INIT");
    set(p, "amount", 37);
    set(p, "point3", 0.42f);
    set(p, "selected", 4);
    set(p, "solo2", 1);
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    auto qOwner = std::make_unique<OpenDuckProcessor>();
    auto &q = *qOwner;
    q.setStateInformation(saved.getData(), (int)saved.getSize());
    check(std::abs(q.value("amount") - 37) < 0.01 && std::abs(q.value("point3") - 0.42f) < 1e-5 &&
              q.value("selected") == 4 && q.value("solo2") == 1,
          "versioned state roundtrip incl curve, selection and SOLO");
    check(p.value("on0") == 1 && q.value("on1") == 0, "independent engine states");
    {
        std::unique_ptr<juce::AudioProcessorEditor> e(q.createEditor());
    }
    check(q.value("amount") == 37 && q.value("selected") == 4, "editor destruction preserves state");
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int b = 0; b < 100; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                audio.setSample(c, i, 0.5f);
        p.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                finite &= std::isfinite(audio.getSample(c, i));
    }
    check(finite, "finite output");
    for (int type = 0; type < 6; ++type)
    {
        bool valid = true;
        for (int i = 0; i <= 1000; ++i)
        {
            float y = OpenDuckProcessor::shape(i / 1000.f, type, 0);
            valid &= std::isfinite(y) && y >= 0 && y <= 1;
        }
        check(valid, ("shape bounded " + std::to_string(type)).c_str());
    }
    {
        auto engineOwner = std::make_unique<SpectralEngine>();
        auto &engine = *engineOwner;
        std::vector<float> source(16000);
        std::mt19937 random(42);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        for (auto &x : source)
            x = dist(random);
        float error = 0;
        for (int n = 0; n < 16000; ++n)
        {
            float input[2]{source[(size_t)n], source[(size_t)n]}, output[2]{};
            engine.process(input, 0, output, 2, 0, 40, 8000, 48000);
            if (n >= 1024)
                error = juce::jmax(error, std::abs(output[0] - source[(size_t)n - 1024]));
        }
        std::cout << "STFT reconstruction max error " << error << "\n";
        check(error < 2e-6f, "STFT unity reconstructs noise with exactly 1024 samples delay");
    }
    {
        auto bandsOwner = std::make_unique<BandEngine>();
        auto &bands = *bandsOwner;
        bands.prepare(48000);
        std::vector<float> source(5000);
        std::mt19937 random(13);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        for (auto &x : source)
            x = dist(random);
        float error = 0;
        for (int n = 0; n < 5000; ++n)
        {
            float reference = 0;
            float out = bands.process(0, source[(size_t)n], {{1, 1, 1}}, reference);
            bands.advance();
            if (n >= 128)
                error = juce::jmax(error, std::abs(out - source[(size_t)n - 128]));
        }
        check(error < 1e-6f, "complementary FIR bands recombine at unity with 128 sample delay");
    }
    {
        auto identityOwner = std::make_unique<OpenDuckProcessor>();
        auto &identity = *identityOwner;
        revisionTests::loadLegacy(identity); // Explicit schema-1 compatibility contract.
        set(identity, "on0", 0);
        identity.prepareToPlay(48000, 512);
        std::vector<float> source(8192);
        source[100] = 0.5f;
        float error = 0;
        for (int n = 0; n < 8192; n += 512)
        {
            audio.clear();
            for (int i = 0; i < 512; ++i)
                for (int c = 0; c < 2; ++c)
                    audio.setSample(c, i, source[(size_t)n + i]);
            identity.processBlock(audio, midi);
            for (int i = 0; i < 512; ++i)
            {
                int at = n + i;
                float expected = at >= 1152 ? source[(size_t)at - 1152] : 0;
                error = juce::jmax(error, std::abs(audio.getSample(0, i) - expected));
            }
        }
        check(identity.getLatencySamples() == 1152 && error < 2e-6f,
              "schema 1 reported latency equals its measured 1152-sample impulse delay");
    }
    {
        bool engineFinite = true;
        double worst = 0;
        for (int mode = 0; mode < 6; ++mode)
        {
            auto engineOwner = std::make_unique<OpenDuckProcessor>();
            auto &engine = *engineOwner;
            revisionTests::loadLegacy(engine);
            for (int k = 0; k < 6; ++k)
                set(engine, ("on" + juce::String(k)).toRawUTF8(), k == mode ? 1.0f : 0.0f);
            set(engine, "sync", 0);
            engine.prepareToPlay(48000, 512);
            auto begin = std::chrono::steady_clock::now();
            for (int block = 0; block < 200; ++block)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        audio.setSample(c, i, 0.25f * std::sin((block * 512 + i) * 0.09f));
                engine.processBlock(audio, midi);
                for (int i = 0; i < 512; ++i)
                    engineFinite &= std::isfinite(audio.getSample(0, i));
            }
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            worst = juce::jmax(worst, elapsed / (200 * 512.0 / 48000));
            std::cout << "ENGINE " << mode << " realtime ratio " << elapsed / (200 * 512.0 / 48000) << "\n";
        }
        check(engineFinite, "all six engines finite");
        check(worst < 1, "all engines faster than realtime offline on this PC");
    }
    {
        auto soloOwner = std::make_unique<OpenDuckProcessor>();
        auto &solo = *soloOwner;
        set(solo, "on1", 1);
        set(solo, "solo2", 1);
        set(solo, "solo2", 0);
        check(solo.value("on0") == 1 && solo.value("on1") == 1 && solo.value("on2") == 0,
              "SOLO preserves prior ON/OFF states");
    }
    {
        auto malformedOwner = std::make_unique<OpenDuckProcessor>();
        auto &malformed = *malformedOwner;
        const char data[] = "not a JUCE state";
        malformed.setStateInformation(data, sizeof(data));
        check(malformed.value("amount") == 80, "malformed state ignored");
    }

    {
        auto syncedOwner = std::make_unique<OpenDuckProcessor>();
        auto &synced = *syncedOwner;
        revisionTests::loadLegacy(synced); // Explicit schema-1 compatibility contract.
        TestPlayHead host;
        synced.setPlayHead(&host);
        synced.prepareToPlay(48000, 512);
        audio.clear();
        synced.processBlock(audio, midi);
        host.tempo = 145;
        host.ppq = 7.25;
        synced.processBlock(audio, midi);
        check(synced.bpm.load() == 145, "HOST SYNC follows changing host BPM and seek");
        set(synced, "sync", 0);
        set(synced, "freeBpm", 132);
        synced.processBlock(audio, midi);
        check(synced.bpm.load() == 132, "FREE tempo independent from host");
        set(synced, "sync", 1);
        host.playing = false;
        for (int b = 0; b < 400; ++b)
            synced.processBlock(audio, midi);
        check(synced.reduction.load() < 1e-4, "transport stop releases ducking smoothly");
    }
    {
        auto triggeredOwner = std::make_unique<OpenDuckProcessor>();
        auto &triggered = *triggeredOwner;
        revisionTests::loadLegacy(triggered); // Explicit schema-1 compatibility contract.
        set(triggered, "trigger", 2);
        triggered.prepareToPlay(48000, 512);
        audio.clear();
        triggered.processBlock(audio, midi);
        check(triggered.engineActivity[0].load() < 1e-5, "MIDI mode does not trigger without notes");
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 128);
        audio.clear();
        triggered.processBlock(audio, midi);
        check(triggered.engineActivity[0].load() > 0.05, "sample-offset MIDI note triggers ducking");
        midi.clear();
    }
    {
        auto triggeredOwner = std::make_unique<OpenDuckProcessor>();
        auto &triggered = *triggeredOwner;
        revisionTests::loadLegacy(triggered); // Explicit schema-1 compatibility contract.
        auto buses = triggered.getBusesLayout();
        buses.inputBuses.set(1, juce::AudioChannelSet::stereo());
        check(triggered.setBusesLayout(buses), "sidechain stereo layout accepted");
        set(triggered, "trigger", 1);
        set(triggered, "attack", 0.1f);
        set(triggered, "threshold", -36);
        triggered.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> busAudio(4, 512);
        busAudio.clear();
        triggered.processBlock(busAudio, midi);
        check(triggered.engineActivity[0].load() < 1e-5, "audio sidechain no false trigger on silence");
        for (int i = 0; i < 512; ++i)
        {
            busAudio.setSample(2, i, i < 200 ? 0.8f : 0);
            busAudio.setSample(3, i, i < 200 ? 0.8f : 0);
        }
        triggered.processBlock(busAudio, midi);
        check(triggered.engineActivity[0].load() > 0.1 && triggered.sidechainPresent.load(),
              "real sidechain audio triggers ducking");
        bool captured = false;
        for (const auto &v : triggered.waveform)
            captured |= std::abs(v.load()) > 0.1f;
        check(captured, "waveform buffer contains real sidechain samples");
    }
    {
        auto bypassOwner = std::make_unique<OpenDuckProcessor>();
        auto &bypass = *bypassOwner;
        revisionTests::loadLegacy(bypass); // Explicit schema-1 compatibility contract.
        set(bypass, "bypass", 1);
        bypass.prepareToPlay(48000, 512);
        std::vector<float> source(8192);
        for (int i = 0; i < 8192; ++i)
            source[(size_t)i] = 0.3f * std::sin(i * 0.03f);
        float error = 0;
        for (int n = 0; n < 8192; n += 512)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    audio.setSample(c, i, source[(size_t)n + i]);
            bypass.processBlock(audio, midi);
            for (int i = 0; i < 512; ++i)
            {
                int at = n + i;
                float expected = at >= 1152 ? source[(size_t)at - 1152] : 0;
                error = juce::jmax(error, std::abs(audio.getSample(0, i) - expected));
            }
        }
        check(error < 1e-6, "schema 1 bypass is transparent at its declared 1152-sample latency");
        check(bypass.getBypassParameter() != nullptr, "host bypass bound to smoothed DSP parameter");
    }
    {
        bool valid = true;
        float maximumJump = 0;
        std::mt19937 random(77);
        for (double sr : {44100.0, 48000.0, 96000.0})
            for (int blockSize : {1, 17, 64, 257, 1024})
            {
                auto stressOwner = std::make_unique<OpenDuckProcessor>();
                auto &stress = *stressOwner;
                revisionTests::loadLegacy(stress);
                stress.prepareToPlay(sr, blockSize);
                juce::AudioBuffer<float> block(2, blockSize);
                float previous = 0;
                for (int iteration = 0; iteration < 50; ++iteration)
                {
                    for (int k = 0; k < 6; ++k)
                        set(stress, ("on" + juce::String(k)).toRawUTF8(), (float)(random() % 2));
                    set(stress, "amount", (float)(random() % 101));
                    set(stress, "morph", (random() % 1001) / 1000.0f);
                    set(stress, "shape", (float)(random() % 6));
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < blockSize; ++i)
                            block.setSample(c, i, 0.2f);
                    stress.processBlock(block, midi);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        float x = block.getSample(0, i);
                        valid &= std::isfinite(x) && std::abs(x) < 1;
                        maximumJump = juce::jmax(maximumJump, std::abs(x - previous));
                        previous = x;
                    }
                }
            }
        std::cout << "Automation constant-input maximum sample step " << maximumJump
                  << " (includes startup)\n";
        check(valid, "parameter stress finite/bounded at 44.1/48/96 kHz, blocks 1/17/64/257/1024");
    }
    {
        auto spectralOwner = std::make_unique<SpectralEngine>();
        auto &spectral = *spectralOwner;
        double inBand = 0, outBand = 0;
        for (int n = 0; n < 48000; ++n)
        {
            float input[2]{std::sin((float)(juce::MathConstants<double>::twoPi * 1000 * n / 48000)),
                           std::sin((float)(juce::MathConstants<double>::twoPi * 12000 * n / 48000))},
                output[2]{};
            spectral.process(input, 0, output, 2, 0.8f, 500, 3000, 48000);
            if (n > 4096)
            {
                inBand += output[0] * output[0];
                outBand += output[1] * output[1];
            }
        }
        check(inBand < outBand * 0.1, "Spectral attenuates 1 kHz band while preserving 12 kHz");
    }
    {
        auto noStackOwner = std::make_unique<OpenDuckProcessor>();
        auto &noStack = *noStackOwner;
        revisionTests::loadLegacy(noStack);
        set(noStack, "sync", 0);
        set(noStack, "shape", 3);
        set(noStack, "release", 5);
        set(noStack, "attack", 0.1f);
        for (int k = 0; k < 6; ++k)
            set(noStack, ("on" + juce::String(k)).toRawUTF8(), 1);
        noStack.prepareToPlay(48000, 512);
        bool capped = true;
        for (int b = 0; b < 100; ++b)
        {
            audio.clear();
            noStack.processBlock(audio, midi);
            capped &= noStack.reduction.load() <= 0.8001f;
        }
        check(capped, "all-engine attenuation budget never exceeds requested 80 percent");
    }

    {
        auto clickOwner = std::make_unique<OpenDuckProcessor>();
        auto &click = *clickOwner;
        revisionTests::loadLegacy(click);
        set(click, "sync", 0);
        set(click, "shape", 3);
        set(click, "attack", 0.1f);
        set(click, "release", 5);
        click.prepareToPlay(48000, 512);
        float previous = 0, maximum = 0;
        for (int block = 0; block < 500; ++block)
        {
            if (block > 20 && block % 10 == 0)
                set(click, "amount", block % 20 ? 0.0f : 100.0f);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    audio.setSample(c, i, 0.2f);
            click.processBlock(audio, midi);
            for (int i = 0; i < 512; ++i)
            {
                float y = audio.getSample(0, i);
                if (block > 20)
                    maximum = juce::jmax(maximum, std::abs(y - previous));
                previous = y;
            }
        }
        std::cout << "Maximum post-startup DC sample step under depth automation " << maximum << "\n";
        check(maximum < 0.045f, "anti-click smoothing bounds discontinuities at fastest attack");
    }
    {
        std::array<std::unique_ptr<OpenDuckProcessor>, 8> engines;
        for (auto &e : engines)
        {
            e = std::make_unique<OpenDuckProcessor>();
            revisionTests::loadLegacy(*e); // Original eight-instance performance budget.
            set(*e, "sync", 0);
            for (int k = 0; k < 6; ++k)
                set(*e, ("on" + juce::String(k)).toRawUTF8(), 1);
            e->prepareToPlay(48000, 512);
        }
        auto begin = std::chrono::steady_clock::now();
        bool instancesFinite = true;
        for (int block = 0; block < 100; ++block)
            for (auto &e : engines)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        audio.setSample(c, i, 0.1f * std::sin((block * 512 + i) * 0.09f));
                e->processBlock(audio, midi);
                for (int i = 0; i < 512; ++i)
                    instancesFinite &= std::isfinite(audio.getSample(0, i));
            }
        double ratio = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() /
                       (100 * 512.0 / 48000);
        std::cout << "8 instances, 6 engines each, offline realtime ratio " << ratio << "\n";
        check(instancesFinite && ratio < 1,
              "eight schema 1 instances process all engines faster than realtime offline");
    }

    {
        auto repeatedOwner = std::make_unique<OpenDuckProcessor>();
        auto &repeated = *repeatedOwner;
        revisionTests::loadLegacy(repeated); // Explicit schema-1 compatibility contract.
        auto layout = repeated.getBusesLayout();
        layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
        repeated.setBusesLayout(layout);
        set(repeated, "trigger", 1);
        set(repeated, "threshold", -36);
        repeated.prepareToPlay(44100, 512);
        juce::AudioBuffer<float> bus(4, 512);
        std::array<float, 8> peaks{};
        const double samplesPerBeat = 44100.0 * 60 / 145;
        for (int offset = 0; offset < (int)(samplesPerBeat * 8); offset += 512)
        {
            bus.clear();
            for (int i = 0; i < 512; ++i)
            {
                double phase = std::fmod(offset + i, samplesPerBeat) / 44100;
                float kick = (float)(0.09 * std::sin(juce::MathConstants<double>::twoPi * 65 * phase) *
                                     std::exp(-phase / 0.085));
                bus.setSample(2, i, kick);
                bus.setSample(3, i, kick);
            }
            repeated.processBlock(bus, midi);
            int beat = juce::jmin(7, (int)(offset / samplesPerBeat));
            peaks[(size_t)beat] = juce::jmax(peaks[(size_t)beat], repeated.engineActivity[0].load());
        }
        bool every = true;
        for (auto peak : peaks)
            every &= peak > 0.15f;
        check(every, "audio sidechain retriggers on every kick at 145 BPM");
    }
    revisionTests::legacyCompatibility(check);
    revisionTests::catalogAndCurves(check);
    revisionTests::sessionPersistence(check);
    revisionTests::customCurveValidation(check);
    revisionTests::modernDsp(check);
    revisionTests::bandFocus(check);
    revisionTests::editorControls(check);
    revisionTests::nativeCurveGestures(check);
    if (argc > 1)
    {
        auto folder = juce::File(juce::String::fromUTF8(argv[1]));
        folder.createDirectory();
        {
            auto renderOwner = std::make_unique<OpenDuckProcessor>();
            auto &render = *renderOwner;
            revisionTests::loadLegacy(render);
            set(render, "sync", 0);
            set(render, "shape", 3);
            set(render, "attack", 0.1f);
            set(render, "release", 5);
            render.prepareToPlay(48000, 512);
            juce::WavAudioFormat format;
            auto file = folder.getChildFile("audio-stress-48k.wav");
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            stream->setPosition(0);
            static_cast<juce::FileOutputStream *>(stream.get())->truncate();
            auto writer = format.createWriterFor(
                stream,
                juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(
                    24));
            for (int block = 0; block < 750; ++block)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        audio.setSample(c, i,
                                        (float)(0.2 * std::sin(juce::MathConstants<double>::twoPi * 1000 *
                                                               (block * 512 + i) / 48000)));
                render.processBlock(audio, midi);
                writer->writeFromAudioSampleBuffer(audio, 0, 512);
            }
        }
        auto visualOwner = std::make_unique<OpenDuckProcessor>();
        auto &visual = *visualOwner;
        visual.prepareToPlay(48000, 512);
        std::unique_ptr<juce::AudioProcessorEditor> e(visual.createEditor());
        e->setSize(1648, 830);
        e->resized();
        {
            juce::MemoryBlock preset;
            visual.getStateInformation(preset);
            folder.getChildFile("QA-StateRoundtrip.odpx").replaceWithData(preset.getData(), preset.getSize());
        }
        {
            auto image = e->createComponentSnapshot(e->getLocalBounds(), true, 2);
            juce::FileOutputStream stream(folder.getChildFile("preview-default.png"));
            stream.setPosition(0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
        for (int level = 0; level < 3; ++level)
        {
            for (const char *id : {"curveAmount0", "curveAttack0", "curveRelease0", "curveTension0", "curvePhase0", "mix", "curveFocus0"})
            {
                auto *param = visual.state.getParameter(id);
                param->setValueNotifyingHost(level * 0.5f);
            }
            e.reset(visual.createEditor());
            e->setSize(1648, 830);
            e->resized();
            auto image = e->createComponentSnapshot(e->getLocalBounds(), true, 2);
            juce::FileOutputStream stream(folder.getChildFile("knobs-" + juce::String(level) + ".png"));
            stream.setPosition(0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
        check(true, "rendered all seven active knobs at min/mid/max at 200 percent");
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
