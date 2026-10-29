#pragma once
#include <JuceHeader.h>
#include "Engines.h"
#include "BehaviorLibrary.h"
#include "LocalAssist.h"
#include "MeasuredTemplateAudio4.h"
#include <map>
#include <string_view>
class OpenDuckProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
  public:
    OpenDuckProcessor();
    ~OpenDuckProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override
    {
        return true;
    }
    const juce::String getName() const override
    {
        return "Open Duck";
    }
    bool acceptsMidi() const override
    {
        return true;
    }
    bool producesMidi() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override
    {
        return revision3Mask.load(std::memory_order_relaxed)!=0
            ? (getLatencySamples()+SpectralEngine::size+6.0*BandEngine::taps)/rate
            : (2.0 * SpectralEngine::size + BandEngine::taps) / rate;
    }
    int getNumPrograms() override
    {
        return 1;
    }
    int getCurrentProgram() override
    {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override
    {
        return "Init";
    }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float value(const juce::String &id) const
    {
        return state.getRawParameterValue(id)->load();
    }
    float value(const char *id) const
    {
        auto it = raw.find(std::string_view(id));
        return it == raw.end() ? 0 : it->second->load(std::memory_order_relaxed);
    }
    juce::AudioProcessorParameter *getBypassParameter() const override
    {
        return state.getParameter("bypass");
    }
    static float shape(float phase, int type, float tension);
    static float customShape(float phase, const std::array<float, 8> &points, float tension);
    using CurvePoint = odx::CurvePoint;
    using CurveSnapshot = odx::CurveSnapshot;
    using BehaviorPreset = odx::BehaviorPreset;
    static const std::array<BehaviorPreset, 700> &factoryPresets()
    {
        return odx::factoryPresets();
    }
    static juce::String factoryPresetName(int id);
    void applyTemplatePreset(int id);
    static juce::String templatePresetName(int id);
    void shiftTemplateCurve(int direction, bool fine);
    void resetTemplateCurveEdits();
    CurveSnapshot getTemplateShapePreview(int shape) const;
    void beginTemplateEdit4(const juce::String& label);
    void commitTemplateEdit4();
    bool canUndoTemplateEdit4() const;
    bool undoTemplateEdit4();
    juce::String getTemplateUndoLabel4() const;
    static const std::array<const char *, 7> &factoryCategoryNames()
    {
        return odx::categoryNames();
    }
    static const std::array<const char *, 16> &factoryShapeNames()
    {
        return odx::shapeNames();
    }
    static juce::String curveParameterId(const char *parameter, int mode);
    void applyBehaviorPreset(int id, int mode = -1);
    int currentBehaviorPresetId(int mode = -1) const;
    CurveSnapshot getCurveSnapshot(int mode = -1) const;
    static float sampleCurve(const CurveSnapshot &, float cycle);
    void setCurveSnapshot(int mode, const CurveSnapshot &, const juce::String &transaction = {});
    void setCustomCurve(int mode, const CurveSnapshot &, const juce::String &transaction = {});
    bool editPointsEnabled(int mode = -1) const;
    void setEditPointsEnabled(bool enabled, int mode = -1);
    bool freeDrawEnabled(int mode = -1) const;
    void setFreeDrawEnabled(bool enabled, int mode = -1);
    bool usesModernCurves(int mode = -1) const;
    // Revision 3 APIs. Published 0.1/0.2 IDs and choice ranges above remain unchanged.
    static juce::String sectorParameterId(const char *base, int mode);
    void setSectorParameter(int mode, const char *base, float value);
    bool usesRevision3(int mode = -1) const;
    void upgradeToRevision3();
    static const std::array<BehaviorPreset, 700> &sectorPresets();
    static std::vector<int> sectorPresetIds(int mode, int category = -1);
    void applySectorPreset(int id, int mode = -1);
    void selectFactoryShape(int shape, int mode = -1);
    static float depthGain(float envelope, float amount);
    static float effectiveCurveSample(const CurveSnapshot &, float cycle, double cycleSeconds,
                                      int mode = 0, float character = .5f);
    static float curveTimeToRaw(const CurveSnapshot &, float cycle, double cycleSeconds);
    static float rawCurveTimeToCycle(const CurveSnapshot &, float rawX, double cycleSeconds);
    static float gainToEnvelope(float gain, float amount);
    float previewCurveGain(int mode, float cycle, int band = -1) const;
    float previewGainToCurveEnvelope(int mode, float cycle, float gain, int band = -1) const;
    float previewCycleToRaw(int mode, float cycle, int band = -1) const;
    float rawToPreviewCycle(int mode, float raw, int band = -1) const;
    CurveSnapshot getEffectiveCurveSnapshot(int mode, int band = -1) const;
    double cycleSeconds(int mode = -1) const;
    enum class CurveOperation { mirrorVertical, mirrorHorizontal, clear, rotateLeft, rotateRight,
        quantise, doubleCurve, halveCurve, randomise, ramp, step, gate, resetTransforms };
    void transformCurve(CurveOperation, int mode = -1, int band = -1);
    CurveSnapshot getBandCurveSnapshot(int mode, int band) const;
    void setBandCurveSnapshot(int mode, int band, const CurveSnapshot &);
    juce::Result saveUserCurve(const juce::File &, int mode = -1) const;
    juce::Result loadUserCurve(const juce::File &, int mode = -1);
    juce::Result traceAudio(const juce::AudioBuffer<float> &, double sampleRate, int mode = -1,
                           int band = -1);
    juce::Result traceSidechain(int mode = -1, int band = -1);
    struct ScopeFrame {
        std::array<float, 512> mainMin{}, mainMax{}, sideMin{}, sideMax{}, outMin{}, outMax{}, gain{};
        uint64_t generation = 0, triggerSample = 0;
        double periodSeconds = .5;
        float phase = 0;
        bool complete = false, sidechain = false;
        int bins = 0;
        // Input overview metadata. Legacy engine scopes retain the defaults.
        // [firstBin,bins) contains real samples; an initial partial bar may
        // start midway through its physical time axis.
        int firstBin = 0;
        double startPpq = 0, captureBeats = 4, curvePhaseStart = 0;
        bool hostAligned = false;
        // Fast model1 telemetry: the last real sample in each display bin.
        // This is the signed ducking coefficient, including bypass and the
        // Anti-click tap blend, rather than an output/input peak ratio.
        std::array<float,512> envelopeGain{}, envelopePhase{}, envelopeTimeSeconds{};
        std::array<uint8_t,512> envelopeValid{}, envelopeActive{};
        double envelopeCycleSeconds = 0;
        bool hasEnvelopeTimeline = false, envelopeTriggerCapture = false;
    };
    ScopeFrame getScopeFrame(int mode = -1) const;
    void setInputScopeBeats(double beats);
    double getInputScopeBeats() const { return inputScopeBeats.load(std::memory_order_relaxed); }
    ScopeFrame getInputScopeFrame(bool preferLive = false) const;
    void setScopeFrozen(bool freeze) {
        // Editor-thread operation. The audio callback only ever tries this lock.
        const juce::SpinLock::ScopedLockType lock(scopeLock);
        if (freeze && !scopeFrozen.load())
            for (size_t mode = 0; mode < 6; ++mode)
                publishedScopes[mode].phase = playheads3[mode].load();
        scopeFrozen.store(freeze);
    }
    bool isScopeFrozen() const { return scopeFrozen.load(); }
    int revision3LatencySamples() const;
    odx::ai::Status localAssistStatus(int mode) const;
    bool startLocalAssistCapture(int mode, int seconds);
    bool requestLocalAssistStrength(int mode, float strength);
    void cancelLocalAssist(int mode);
    bool applyLocalAssist(int mode, float strength = 1);
    bool undoLocalAssist(int mode);
    bool canUndoLocalAssist(int mode) const;
    bool localAssistProposal(int mode, odx::ai::Proposal &) const;
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> bpm{120}, inMeter{0}, outMeter{0}, reduction{0};
    std::array<std::atomic<float>, 2> inputPeaks{}, outputPeaks{};
    std::atomic<bool> sidechainPresent{false};
    // Actual fast-path input/detector diagnostics, independent of Power.
    std::atomic<float> templateSidechainPeak4{0}, templateDetectorPeak4{0};
    std::atomic<uint64_t> templateTriggerCount4{0};
    std::atomic<bool> templateSidechainEnabled4{false};
    std::array<std::atomic<float>, 512> waveform{};
    std::atomic<unsigned> waveWrite{0};
    std::array<std::atomic<float>, 6> engineActivity{};

  private:
    friend class OpenDuckCurveAction;
    friend class OpenDuckBandCurveAction;
    mutable juce::SpinLock curveLock;
    std::array<CurveSnapshot, 6> curves{}, audioCurves{};
    // Listener only touches this mask. No GUI selection is read by the DSP.
    std::atomic<unsigned> modernMask{63};
    std::atomic<bool> loadingState{false};
    struct TemplateEditCheckpoint4 { juce::String label; juce::MemoryBlock state; };
    std::vector<TemplateEditCheckpoint4> templateEditHistory4;
    juce::MemoryBlock templatePendingEdit4;
    juce::String templatePendingLabel4;
    int templateEditDepth4 = 0;
    bool restoringTemplateUndo4 = false;
    enum CurveParameter
    {
        curveShape,
        curvePhase,
        curveLength,
        curveTension,
        curveAmount,
        curveAttack,
        curveRelease,
        curveGrid,
        curveBand,
        curveFocus,
        curveParameterCount
    };
    std::array<std::array<std::atomic<float> *, curveParameterCount>, 6> curveRefs{};
    std::array<double, 6> curvePhases{}, curveCycles{};
    std::array<juce::SmoothedValue<float>, 6> focusSmoothers;
    int resolveMode(int) const;
    void parameterChanged(const juce::String &, float) override;
    static CurveSnapshot sanitiseCurve(CurveSnapshot);
    void applyCurveSnapshot(int, const CurveSnapshot &);
    void applyBandCurveSnapshot(int, int, const CurveSnapshot &);
    void readCurveParameters(int, CurveSnapshot &) const;
    void processLegacyBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &);
    void processModernBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &);
    void processRevision3Block(juce::AudioBuffer<float> &, juce::MidiBuffer &);
    void processMeasuredTemplateBlock4(juce::AudioBuffer<float>&,juce::MidiBuffer&,unsigned);
    void prepareMeasuredTemplate4(double);
    unsigned desiredRouting4() const;
    int routingLatency4(unsigned) const;
    void publishRouting4();
    void timerCallback() override;
    std::atomic<unsigned> activeRouting4{0};
    std::atomic<bool> routingDirty4{false};
    struct MeasuredHistory4 {
        int64_t sample=-1;
        double phase=0;
        float gain=1,bandBlend=0,listenBlend=0,bypass=0,detector=0;
        std::array<float,2> low{};
        bool inCycle=false,detectorListen=false;
    };
    std::vector<MeasuredHistory4> measuredHistory4;
    odx::audio4::LowpassTpt measuredLowpass4;
    odx::audio4::AnticipatedGain measuredAntiClick4;
    odx::audio4::CausalGainSmoother measuredSyncGain4;
    unsigned measuredLastRouting4=~0u;
    uint64_t measuredLastEnd4=0;
    float measuredBandBlend4=0,measuredListenBlend4=0;
    std::array<float,30> measuredLastControls4{};
    bool measuredControlsObserved4=false;
    float measuredAntiMix4=0,measuredAntiStartMix4=0,measuredAntiTargetMix4=0;
    int measuredAntiTransitionLeft4=0;
    void prepareRevision3(double);
    struct LocalAssistState;
    std::shared_ptr<LocalAssistState> localAssist;
    void initialiseLocalAssist();
    void prepareLocalAssist();
    void pushLocalAssist(const juce::AudioBuffer<float> &);
    void resetLocalAssist();
    std::atomic<unsigned> revision3Mask{63};
    std::array<std::vector<std::atomic<float> *>, 6> refs3;
    std::array<std::array<CurveSnapshot, 3>, 6> bandCurves{}, audioBandCurves{};
    struct ShapeMemory3 {float phase=0,length=1,tension=0,riseSlope=1,moveCurve4=0,slope4=0;bool mirrorH4=false,mirrorV4=false;float end4=63.f/64.f;};
    std::array<std::array<ShapeMemory3,16>,6> shapeMemories3{};
    std::array<BandEngine, 6> sectorBands;
    float templateMoveSmooth4=0,templateSlopeSmooth4=0;
    std::array<float, 3> templateGainTransitionDelta4{};
    std::array<float, 20> templateLastControls4{};
    int templateGainTransitionLeft4=0,templateGainTransitionTotal4=1;
    bool templateControlsObserved4=false,templateGainTransitionPending4=false,templateLastInCycle4=false;
    struct SectorClock {
        double phase = 0, cycle = 0;
        uint64_t age = 0, triggerSample = 0, noteOffSample = 0;
        float detector = 0, previous = 0, velocity = 1, releaseGain = 1;
        std::array<float,2> filterLo{}, filterHi{};
        std::array<float, 3> smoothGain{{1,1,1}};
        bool gate = false, active = false, held = false, releasing = false, noteOffPending = false;
        int holdoff = 0, heldNote = -1, lastTrigger = -1;
    };
    std::array<SectorClock, 6> clocks3;
    std::array<std::array<std::vector<float>, 2>, 3> delay3;
    std::array<std::vector<float>,6> gateDelay3;
    std::array<ScopeFrame, 6> writingScopes{}, publishedScopes{};
    // Independent overview of the actual channel input. Its capture clock is
    // deliberately unrelated to the selected envelope trigger and Power.
    std::atomic<double> inputScopeBeats{4};
    ScopeFrame inputWritingScope{}, inputCompletedScope{}, inputPublishedScope{}, inputLivePublishedScope{};
    mutable juce::SpinLock inputScopeLock;
    double inputScopeFreePpq = 0, inputScopeNextPpq = 0;
    double inputScopeTempo = 0, inputScopeWindowBeats = 0;
    uint64_t inputScopeGeneration = 0;
    int inputScopeLastBin = -1;
    bool inputScopeClockActive = false, inputScopeHasCompleted = false;
    struct ScopeEnvelopeSample4 {
        float gain = 1;
        double phase = 0, cycleSeconds = 0;
        bool active = false, valid = false;
    };
    void captureInputScope(float main, float side, float output, double ppq,
                           double tempo, double beats, bool hostAligned,
                           bool sidechain, double curvePhase, double curveSeconds,
                           const ScopeEnvelopeSample4* envelope = nullptr);
    void publishInputScope();
    // Display events follow the same declared output delay as Main/SC/Output.
    // They never replace or modify the audio-engine clocks above.
    struct ScopeTriggerEvent {
        uint64_t sample = 0;
        double periodSeconds = .5;
        bool noteOn = true, held = false;
    };
    struct ScopeCaptureClock {
        std::array<ScopeTriggerEvent,256> events{};
        size_t first = 0, count = 0;
        uint64_t triggerSample = 0, noteOffSample = 0, droppedEvents = 0;
        double periodSeconds = 0;
        int trigger = -1;
        bool active = false, held = false, noteOffPending = false, frozen = false;
    };
    std::array<ScopeCaptureClock,6> scopeClocks3{};
    void enqueueScopeTrigger(int mode, bool noteOn, bool held, double periodSeconds);
    mutable juce::SpinLock scopeLock;
    std::atomic<bool> scopeFrozen{false};
    uint64_t sampleClock3 = 0, scopeGeneration = 0;
    int delayPosition3 = 0, delayCapacity3 = 0;
    std::array<float, 6> phaseSmooth3{};
    std::array<std::atomic<float>,6> playheads3{};
    std::array<std::array<std::array<std::array<float, 8>, 2>, 2>, 6> crossoverMemory3{};
    std::map<std::string, std::atomic<float> *, std::less<>> raw;
    std::array<std::atomic<float> *, 8> pointRefs{};
    std::array<std::atomic<float> *, 6> onRefs{}, soloRefs{};
    std::array<std::atomic<float> *, 16> stepRefs{};
    double rate = 48000, phase = 0, freeCycle = 0;
    float follower = 0, smoothedGain = 1;
    bool gate = false;
    int hold = 0;
    SpectralEngine spectral;
    BandEngine bands;
    std::array<std::array<float, 1152>, 2> dryDelay{};
    std::array<std::array<float, 1152>, 2> wetDelay{};
    int delayPos = 0, waveCounter = 0, sideSilence = 0;
    float wavePeak = 0, spectralSmooth = 0, mixSmooth = 1, bypassSmooth = 1;
    std::array<float, 3> bandSmooth{{1, 1, 1}};
    std::array<float, 6> engineSmooth{};
    juce::SmoothedValue<float> morphSmooth;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenDuckProcessor)
};
