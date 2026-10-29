#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace odx::view
{
// A display mask on the original cycle bins. It never stretches/reorders audio,
// edits the gain curve, or changes the shared main/sidechain amplitude reference.
struct SingleHitSpan
{
    int startBin = 0, binCount = 0, detectedOnsets = 0, onsetBin = -1;
    bool hasAudio = false;
    bool contains(int bin) const
    {
        return binCount > 0 && (bin - startBin + 512) % 512 < binCount;
    }
};

inline SingleHitSpan isolateSingleHit(const std::array<float, 512>& minimum,
                                     const std::array<float, 512>& maximum, double periodSeconds, int capturedBins = 512)
{
    SingleHitSpan result;
    if (!std::isfinite(periodSeconds) || periodSeconds <= 0 || capturedBins <= 0 || capturedBins > 512) return result;
    std::array<float, 512> magnitude{}, envelope{};
    float peak = 0;
    for (size_t i = 0; i < (size_t)capturedBins; ++i)
    {
        const float value = std::max(std::abs(minimum[i]), std::abs(maximum[i]));
        magnitude[i] = std::isfinite(value) ? value : 0;
        peak = std::max(peak, magnitude[i]);
    }
    result.hasAudio = peak > 1.e-6f;
    if (!result.hasAudio) return result;
    // A causal 22ms peak hold removes individual low-frequency oscillations
    // from onset detection. Circular history handles a hit at cycle zero.
    const int hold = std::clamp((int)std::ceil(.022 / periodSeconds * 512), 1, 256);
    for (int i = 0; i < capturedBins; ++i) for (int j = 0; j < (capturedBins == 512 ? hold : std::min(hold, i + 1)); ++j)
        envelope[(size_t)i] = std::max(envelope[(size_t)i], magnitude[(size_t)((i - j + 512) % 512)]);
    const float threshold = peak * .18f;
    const int separation = std::clamp((int)std::ceil(.070 / periodSeconds * 512), 1, 511);
    bool observedEdgeDecay = false;
    if (capturedBins < 512)
    {
        // The samples before a partial capture are unknown. Its left border
        // cannot supply a quiet prehistory. An early attack is accepted only
        // if the observed first70ms peak actually decays by6dB afterwards.
        float edgePeak = 0; int peakBin = 0;
        for (int i = 0; i < std::min(capturedBins, separation); ++i)
            if (magnitude[(size_t)i] > edgePeak) { edgePeak = magnitude[(size_t)i]; peakBin = i; }
        for (int i = peakBin + hold; i < capturedBins; ++i)
            if (edgePeak > 1.e-6f && envelope[(size_t)i] <= edgePeak * .5f) { observedEdgeDecay = true; break; }
    }
    std::array<int, 512> onsets{};
    int count = 0;
    for (int i = 0; i < capturedBins; ++i)
    {
        const bool crossing = i == 0 && capturedBins < 512 ? observedEdgeDecay
            : envelope[(size_t)((i + 511) % 512)] < threshold;
        const bool observedHistory = capturedBins == 512 || i >= hold || observedEdgeDecay;
        if (envelope[(size_t)i] >= threshold && crossing && observedHistory &&
            (count == 0 || i - onsets[(size_t)count - 1] >= separation)) onsets[(size_t)count++] = i;
    }
    if (capturedBins == 512 && count > 1 && onsets[0] + 512 - onsets[(size_t)count - 1] < separation) --count;
    result.detectedOnsets = count;
    if (count == 0) return result; // Sustained material is not relabelled a kick.
    result.onsetBin = onsets[0];
    const int preRoll = std::clamp((int)std::ceil(.010 / periodSeconds * 512), 1, 64);
    result.startBin = capturedBins == 512 ? (onsets[0] - preRoll + 512) % 512 : std::max(0, onsets[0] - preRoll);
    result.binCount = capturedBins == 512 ? (count == 1 ? 512 : onsets[1] - onsets[0])
        : count == 1 ? capturedBins - result.startBin : std::min(capturedBins - result.startBin, onsets[1] - onsets[0]);
    return result;
}

// Display metadata only. The clock is supplied by the caller so transport stop,
// a held frame and Freeze can be tested without sleeping or inventing motion.
enum class ScopeState { waiting, live, held, frozen, periodMismatch };
inline const char* scopeStateName(ScopeState state)
{
    switch (state)
    {
        case ScopeState::live: return "LIVE";
        case ScopeState::held: return "HELD";
        case ScopeState::frozen: return "FROZEN";
        case ScopeState::periodMismatch: return "WAITING";
        default: return "WAITING";
    }
}
inline bool scopePeriodsMatch(double captured, double expected)
{
    return std::isfinite(captured) && std::isfinite(expected) && captured > 0 && expected > 0
        && std::abs(captured - expected) <= std::max(.00025, expected * .001);
}
struct ScopeFreshness
{
    static constexpr double activityTimeoutMs = 250;
    bool observed = false;
    uint64_t generation = 0;
    float phase = 0;
    double lastFrameMs = 0, lastActivityMs = -std::numeric_limits<double>::infinity();

    ScopeState observe(uint64_t nextGeneration, float nextPhase, double periodSeconds,
                       bool completeFrame, bool periodMatches, bool frozen, double nowMs)
    {
        if (!std::isfinite(nowMs)) return ScopeState::waiting;
        const bool finitePhase = std::isfinite(nextPhase);
        if (!observed || nowMs < lastFrameMs)
        {
            observed = true; generation = nextGeneration; phase = nextPhase;
            lastFrameMs = nowMs; lastActivityMs = -std::numeric_limits<double>::infinity();
        }
        else
        {
            if (nextGeneration != generation)
            {
                generation = nextGeneration; lastFrameMs = nowMs;
                lastActivityMs = nowMs;
            }
            if (!frozen && finitePhase && std::isfinite(phase) && std::abs(nextPhase - phase) > .00001f)
                lastActivityMs = nowMs;
            phase = nextPhase;
        }
        if (!completeFrame || nextGeneration == 0) return ScopeState::waiting;
        if (!periodMatches) return ScopeState::periodMismatch;
        if (frozen) return ScopeState::frozen;
        // Phase can continue while a producer stops publishing. Do not call an
        // arbitrarily old picture live just because its cursor is still moving.
        const double frameTimeout = std::max(1000., periodSeconds * 2500.);
        return nowMs - lastActivityMs <= activityTimeoutMs && nowMs - lastFrameMs <= frameTimeout
            ? ScopeState::live : ScopeState::held;
    }
};

struct ScopeAxis
{
    double periodMs = 0, startMs = 0, endMs = 0, cursorMs = 0;
    std::array<double, 5> ticksMs{};
    float cursorPosition = 0;
    bool cursorInView = false;
};
inline ScopeAxis scopeAxis(double periodSeconds, float offset, float zoom, float phase)
{
    ScopeAxis result;
    if (!std::isfinite(periodSeconds) || periodSeconds <= 0) return result;
    zoom = std::isfinite(zoom) ? std::clamp(zoom, 1.f, 8.f) : 1.f;
    offset = std::isfinite(offset) ? std::clamp(offset, 0.f, 1.f - 1.f / zoom) : 0.f;
    phase = std::isfinite(phase) ? std::clamp(phase, 0.f, 1.f) : 0.f;
    result.periodMs = periodSeconds * 1000;
    result.startMs = offset * result.periodMs;
    result.endMs = (offset + 1. / zoom) * result.periodMs;
    result.cursorMs = phase * result.periodMs;
    result.cursorPosition = (phase - offset) * zoom;
    result.cursorInView = result.cursorPosition >= 0 && result.cursorPosition <= 1;
    for (size_t i = 0; i < result.ticksMs.size(); ++i)
        result.ticksMs[i] = result.startMs + (result.endMs - result.startMs) * double(i) / 4;
    return result;
}

// Template display policy only. Neither the audio clock nor its waveform bins
// are modified. Manual choices force their view while AUTO retains its own
// classifier history, so switching back to AUTO uses the most recent evidence.
enum class CurveViewMode { automatic = 0, single = 1, loop = 2 };
struct CurveViewDecision
{
    bool loop = false, hasAudio = false;
    int detectedOnsets = 0, onsetBin = -1;
    uint64_t generation = 0;
};
struct CurveViewSelector
{
    bool automaticLoop = false, sourceObserved = false;
    int singleVotes = 0, sourceKey = 0;
    CurveViewDecision latest{};

    void reset()
    {
        automaticLoop = false; sourceObserved = false; singleVotes = 0;
        sourceKey = 0; latest = {};
    }

    CurveViewDecision observe(CurveViewMode mode, uint64_t generation,
                              bool complete, int firstBin, int bins,
                              const std::array<float, 512>& minimum,
                              const std::array<float, 512>& maximum,
                              double periodSeconds, int nextSourceKey = 0)
    {
        if (!sourceObserved || sourceKey != nextSourceKey)
        {
            reset(); sourceObserved = true; sourceKey = nextSourceKey;
        }
        const bool freshComplete = complete && firstBin == 0 && bins == 512
            && generation != 0 && generation != latest.generation
            && std::isfinite(periodSeconds) && periodSeconds > 0;
        if (freshComplete)
        {
            const auto hit = isolateSingleHit(minimum, maximum, periodSeconds, bins);
            latest.hasAudio = hit.hasAudio; latest.detectedOnsets = hit.detectedOnsets;
            latest.onsetBin = hit.onsetBin; latest.generation = generation;
            if (hit.hasAudio && hit.detectedOnsets >= 2)
            {
                automaticLoop = true; singleVotes = 0;
            }
            else if (hit.hasAudio)
            {
                // A lone event or sustained material needs two independent
                // completed windows to replace an established multi-hit view.
                if (singleVotes < 2) ++singleVotes;
                if (singleVotes >= 2) automaticLoop = false;
            }
            else singleVotes = 0; // Silence supplies no onset classification.
        }
        auto result = latest;
        result.loop = mode == CurveViewMode::loop
            || (mode == CurveViewMode::automatic && automaticLoop);
        return result;
    }
};

// A physical crop on the original capture time axis. captureZoom is purposely
// not restricted to the legacy 1..8 range: 16 beats viewed as a 1/8-note cycle
// need 32x, while a short recording may occupy only part of one DSP cycle.
struct CurveViewWindow
{
    bool valid = false, loop = false;
    double captureSeconds = 0, cycleSeconds = 0;
    double startSeconds = 0, durationSeconds = 0;
    double captureOffset = 0, captureZoom = 1, cycleAtStart = 0;
    std::array<double, 5> ticksMs{};

    double captureFractionAtDisplay(double display) const
    {
        return captureOffset + display / captureZoom;
    }
    double displayForCaptureFraction(double captured) const
    {
        return (captured - captureOffset) * captureZoom;
    }
    double cycleAtDisplay(double display) const
    {
        return valid ? cycleAtStart + display * durationSeconds / cycleSeconds : 0;
    }
};

inline CurveViewWindow curveViewWindow(bool loop, double captureSeconds,
                                      double cycleSeconds, double curvePhaseStart,
                                      int onsetBin = -1, int firstBin = 0, int bins = 512,
                                      double loopOffset = 0, double loopZoom = 1)
{
    CurveViewWindow result;
    result.loop = loop;
    if (!std::isfinite(captureSeconds) || captureSeconds <= 0
        || !std::isfinite(cycleSeconds) || cycleSeconds <= 0
        || !std::isfinite(curvePhaseStart)) return result;
    result.valid = true; result.captureSeconds = captureSeconds;
    result.cycleSeconds = cycleSeconds;
    result.durationSeconds = loop ? captureSeconds : cycleSeconds;
    if (loop)
    {
        loopZoom = std::isfinite(loopZoom) ? std::clamp(loopZoom, 1., 8.) : 1.;
        loopOffset = std::isfinite(loopOffset) ? std::clamp(loopOffset, 0., 1. - 1. / loopZoom) : 0.;
        result.startSeconds = loopOffset * captureSeconds;
        result.durationSeconds = captureSeconds / loopZoom;
    }
    else
    {
        firstBin = std::clamp(firstBin, 0, 511); bins = std::clamp(bins, 0, 512);
        const int anchorBin = onsetBin >= firstBin && onsetBin < bins ? onsetBin
            : bins > firstBin ? firstBin : 0;
        // A min/max bin describes an interval, not its left boundary. Anchor
        // at its centre so an onset quantised onto a cycle boundary cannot
        // select the preceding cycle and leave the real kick off-screen.
        const double anchorSeconds = (anchorBin + .5) * captureSeconds / 512.;
        const double anchorCycle = curvePhaseStart + anchorSeconds / cycleSeconds;
        // Keep a canonical phase-zero DSP cycle even when its beginning/tail
        // lies outside the real capture. The graph leaves those margins blank;
        // it never stretches or invents bins to fill the chosen cycle.
        result.startSeconds = (std::floor(anchorCycle) - curvePhaseStart) * cycleSeconds;
    }
    result.captureOffset = result.startSeconds / captureSeconds;
    result.captureZoom = captureSeconds / result.durationSeconds;
    result.cycleAtStart = loop ? curvePhaseStart + result.startSeconds / cycleSeconds : 0;
    for (size_t i = 0; i < result.ticksMs.size(); ++i)
        result.ticksMs[i] = 1000 * (result.startSeconds + result.durationSeconds * double(i) / 4);
    return result;
}

// Estimated onset-to-tail duration at -40dB AMPLITUDE relative to this hit's
// peak (20*log10), not the mask width or an arbitrary gate on the DSP output.
// A complete quiet 22ms after the last crossing is required; truncated tails,
// silence and steady tones have no duration estimate.
inline double singleHitDurationMs(const std::array<float, 512>& minimum,
                                  const std::array<float, 512>& maximum,
                                  const SingleHitSpan& hit, double periodSeconds, int capturedBins = 512)
{
    if (hit.onsetBin < 0 || hit.binCount <= 0 || capturedBins <= 0 || capturedBins > 512
        || !std::isfinite(periodSeconds) || periodSeconds <= 0) return -1;
    const int preRoll = (hit.onsetBin - hit.startBin + 512) % 512;
    const int available = hit.binCount - preRoll;
    if (available <= 1) return -1;
    // Uncaptured bins are unknown, not digital silence. In particular, an End
    // Marker before cycle1 cannot prove the tail settled after the final bin.
    for (int i = 0; i < available; ++i)
        if ((hit.onsetBin + i) % 512 >= capturedBins) return -1;
    const auto magnitude = [&](int relative)
    {
        const auto i = size_t((hit.onsetBin + relative) % 512);
        const float value = std::max(std::abs(minimum[i]), std::abs(maximum[i]));
        return std::isfinite(value) ? value : 0.f;
    };
    float peak = 0;
    for (int i = 0; i < available; ++i) peak = std::max(peak, magnitude(i));
    if (peak <= 1.e-6f) return -1;
    const float threshold = peak * .01f; // 10^(-40/20), not 10^(-40/10).
    int lastAbove = -1;
    for (int i = 0; i < available; ++i) if (magnitude(i) >= threshold) lastAbove = i;
    const int quietBins = std::max(1, int(std::ceil(.022 / periodSeconds * 512)));
    if (lastAbove < 0 || available - 1 - lastAbove < quietBins) return -1;
    return (lastAbove + 1) * periodSeconds * 1000 / 512;
}
}
