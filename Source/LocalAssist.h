#pragma once
#include "BehaviorLibrary.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <string>

namespace odx::ai
{
enum class State : unsigned char { idle, capturing, analyzing, ready, insufficient, cancelled, stale, error };

// All methods except push() are called outside the audio callback. One capture/analysis
// per controller; the six slots retain independent status and immutable proposals.
struct Control
{
    std::string id;
    float value = 0;
};
struct Candidate
{
    CurveSnapshot curve;
    std::array<Control, 32> controls{};
    int controlCount = 0;
};
struct Context
{
    double sampleRate = 48000, bpm = 120;
    double startPpq = 0;
    bool hostPositionAvailable = false, synced = true;
    float mix = 80;
    int trigger = 0;
    std::uint64_t revision = 0;
    Candidate baseline;
    std::array<CurveSnapshot, 3> bandCurves{};
    // Read-only APVTS values needed to reproduce the actual DSP. They never become
    // proposal writes: only Candidate.controls and Candidate.curve may be applied.
    std::array<Control, 1024> frozen{};
    int frozenCount = 0;
    // Refreshed by the native renderer from the frozen baseline at captured BPM.
    // Repeat's intentional periodic ducks include beats without a sidechain hit.
    std::array<float,256> intentGain{};
    double intentCycleSeconds = 0;
    bool hasIntentProfile = false;
};
struct CaptureView
{
    const float *mainL = nullptr, *mainR = nullptr, *sideL = nullptr, *sideR = nullptr;
    int samples = 0;
    double sampleRate = 48000, bpm = 120;
};
struct Metrics
{
    double objective = 0, overlap = 0, preservation = 0, artifacts = 0, hitExtent = 0;
    double inputRms = 0, outputRms = 0;
    bool finite = false;
    int analysedChannels = 1; // 1 = exact historical mono fastpath, 2 = stereo power aggregation.
    // Independent acceptance measurements. Signed dB: 0=unchanged, negative=loss.
    // Windows are derived from captured sources/frozen timing, never candidate output.
    bool preservationMeasured = false;
    double protectedFraction = 0, protectedRmsDb = 0;
    std::array<double, 4> protectedBandDb{};
    unsigned protectedBandMask = 0;
    int recoveryRegions = 0, protectedAttacks = 0;
    double worstRecoveryDb = 0, worstAttackDb = 0;
};
struct CandidateAssessment
{
    bool admissible = false;
    std::string reason;
};
struct Status
{
    State state = State::idle;
    float captureProgress = 0, analysisProgress = 0;
    double capturedSeconds = 0, requestedSeconds = 0, capturedBpm = 0;
    std::uint64_t generation = 0, contextRevision = 0;
    int validHits = 0;
    bool mainPresent = false, sidechainPresent = false, modelsAvailable = false;
    std::string message;
};
struct Proposal
{
    int mode = -1;
    std::uint64_t generation = 0, contextRevision = 0;
    Candidate before, after;
    Candidate evaluatedCandidate;
    float evaluatedStrength = 1;
    Metrics baseline, result;
    bool improved = false;
    bool targetImproved = false;
    std::string modelVersion;
};

// Evaluator executes on the worker only. It must render the real DSP into separate
// processor state, honour cancellation, and leave the live processor untouched.
using Evaluator = std::function<bool(int, const Candidate &, const CaptureView &, const Context &,
                                     Metrics &, const std::atomic<bool> &)>;

class Controller final
{
  public:
    Controller();
    ~Controller();
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    void prepare(double sampleRate); // Configures rate; start preallocates before audio capture.
    void setEvaluator(Evaluator);
    bool start(int mode, int seconds, const Context &); // seconds must be 5 or 10.
    bool requestStrength(int mode, float strength); // One real render using retained source capture; worker only.
    void push(const float *mainL, const float *mainR, const float *sideL,
              const float *sideR, int samples,
              double startPpq = std::numeric_limits<double>::quiet_NaN(),
              double bpm = 0) noexcept; // No lock/allocation/wait. Metadata is first-sample time.
    void cancel(int mode = -1) noexcept;
    void invalidate(int mode, std::uint64_t currentRevision) noexcept;
    Status status(int mode) const;
    bool proposal(int mode, Proposal &) const;
    bool available() const noexcept;
    static Candidate blend(const Candidate &before, const Candidate &after, float strength);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Shared by the offline teacher and the shipped worker. No Python audio renderer.
constexpr int featureCount = 96, controlOutputs = 27, outputCount = controlOutputs + 16;
using Features = std::array<float, featureCount>;
using Outputs = std::array<float, outputCount>;
struct Observation
{
    Features features{};
    int validHits = 0;
    double mainRms = 0, sideRms = 0;
    double mainPeak = 0, sidePeak = 0, mainSpectralEnergy = 0, sideSpectralEnergy = 0;
    int analysedChannels = 1;
    bool mainPresent = false, sidechainPresent = false;
};
Observation analyse(const CaptureView &, int mode, const Context &);
// Frozen pre-stereo reference used only by reproducibility regression tools.
Observation analyseAverageReference(const CaptureView &, int mode, const Context &);
Candidate decode(const Outputs &, int mode, const Candidate &baseline);
Outputs encode(const Candidate &, int mode);
Outputs refinementCandidate(const Outputs &prediction,int attempt); // Shared by live worker and measured parity tool.
Metrics measure(const CaptureView &, const float *outputL, const float *outputR, int outputSamples);
Metrics measure(const CaptureView &, const float *outputL, const float *outputR, int outputSamples,
                const Context &, int mode);
Metrics measureAverageReference(const CaptureView &, const float *outputL, const float *outputR, int outputSamples);
CandidateAssessment assessCandidate(const Metrics &baseline, const Metrics &candidate);
const char *stateName(State) noexcept;
} // namespace odx::ai
