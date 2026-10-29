#include "PluginProcessor.h"
#include "LocalAssistRender.h"
#include <cstring>
#include <limits>

namespace
{
// Scope is computed once during construction. Parameter callbacks only perform
// map lookups and atomic invalidation; they never allocate or acquire a mutex.
bool affectsAssist(const juce::String &id, int mode)
{
    if (id == "template4" || id == "position4" || id == "duration4") return mode == 0;
    if (id.startsWith("v3") || id.startsWith("curve"))
        return id.getLastCharacter() == (juce::juce_wchar) ('0' + mode);
    if (id == "selected" || id.startsWith("on") || id.startsWith("solo") || id == "bypass")
        return false;
    if (id.startsWith("step") || id == "grooveDepth") return mode == 4;
    if (id.startsWith("spectral")) return mode == 1;
    if (id == "morph") return mode == 3;
    if (id.startsWith("band") || id.startsWith("cross")) return mode == 2;
    return id == "mix" || id == "sync" || id == "freeBpm";
}
struct Signature
{
    std::uint64_t value = 1469598103934665603ull;
    void bytes(const void *data, size_t count)
    {
        auto *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < count; ++i) { value ^= p[i]; value *= 1099511628211ull; }
    }
    template<typename T> void add(T item) { bytes(&item, sizeof(item)); }
    void curve(const odx::CurveSnapshot &c)
    {
        add(c.count); add(c.shape); add(c.phase); add(c.length); add(c.tension);
        add(c.amount); add(c.attack); add(c.release); add(c.focus); add(c.riseSlope);
        add(c.custom); add(c.linear); add(c.band); add(c.legacy); add(c.modern3);
        for (int i = 0; i < c.count && i < 256; ++i)
        {
            const auto &p = c.points[(size_t)i];
            add(p.x); add(p.y); add(p.segment); add(p.curvature); add(p.reversed);
        }
    }
};
std::uint64_t signature(const odx::ai::Context &context, int mode, bool includeTempo = true)
{
    Signature hash; hash.curve(context.baseline.curve);
    for (const auto &curve : context.bandCurves) hash.curve(curve);
    for (int i = 0; i < context.frozenCount; ++i)
    {
        const auto &control = context.frozen[(size_t)i];
        if (affectsAssist(juce::String(control.id), mode))
        {
            hash.bytes(control.id.data(), control.id.size()); hash.add(control.value);
        }
    }
    hash.add(context.sampleRate);
    if (includeTempo) hash.add(context.bpm);
    return hash.value;
}
}

struct OpenDuckProcessor::LocalAssistState final : juce::AudioProcessorValueTreeState::Listener
{
    explicit LocalAssistState(OpenDuckProcessor &owner) : processor(owner)
    {
        controller.setEvaluator(odx::ai::renderCandidate);
        for (auto *parameter : processor.getParameters())
            if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(parameter))
            {
                const auto id = ranged->getParameterID(); unsigned mask = 0;
                for (int mode = 0; mode < 6; ++mode)
                    if (affectsAssist(id, mode)) mask |= 1u << (unsigned)mode;
                if (mask != 0) { masks.emplace(id, mask); processor.state.addParameterListener(id, this); }
            }
    }
    ~LocalAssistState() override
    {
        for (const auto &entry : masks) processor.state.removeParameterListener(entry.first, this);
        controller.cancel(); // Controller joins its worker before releasing captured audio.
    }
    void parameterChanged(const juce::String &id, float) override
    {
        const auto found = masks.find(id);
        if (found == masks.end()) return;
        for (int mode = 0; mode < 6; ++mode)
            if ((found->second & (1u << (unsigned)mode)) != 0)
                controller.invalidate(mode, revision[(size_t)mode].fetch_add(1) + 1);
    }
    odx::ai::Context context(int mode) const
    {
        auto result = odx::ai::makeContext(processor, mode, revision[(size_t)mode].load());
        result.sampleRate = processor.rate;
        return result;
    }
    void check(int mode)
    {
        const auto index = (size_t)mode;
        if (!hasContext[index]) return;
        const auto current = context(mode);
        const auto status = controller.status(mode);
        // The host may deliver its first real BPM only after ANALYZE was armed.
        // Compare against first-sample metadata, never the pre-audio UI default.
        const bool tempoChanged = status.capturedBpm > 0 &&
            std::abs(current.bpm - status.capturedBpm) > .001;
        if (signature(current, mode, false) != capturedSignature[index] || tempoChanged)
        {
            controller.invalidate(mode, revision[index].fetch_add(1) + 1);
            hasContext[index] = false;
        }
    }
    OpenDuckProcessor &processor;
    odx::ai::Controller controller;
    std::map<juce::String, unsigned> masks;
    std::array<std::atomic<std::uint64_t>, 6> revision{};
    std::array<odx::ai::Candidate, 6> beforeApply{};
    std::array<std::uint64_t, 6> capturedSignature{}, appliedSignature{};
    std::array<bool, 6> hasContext{}, hasUndo{};
    std::array<std::string, 6> rejection{};
};

void OpenDuckProcessor::initialiseLocalAssist()
{
    localAssist = std::make_shared<LocalAssistState>(*this);
}
void OpenDuckProcessor::prepareLocalAssist()
{
    if (!localAssist) return;
    localAssist->controller.prepare(rate);
    for (int mode = 0; mode < 6; ++mode)
        localAssist->controller.invalidate(mode, localAssist->revision[(size_t)mode].fetch_add(1) + 1);
}
void OpenDuckProcessor::resetLocalAssist()
{
    if (!localAssist) return;
    localAssist->controller.cancel();
    for (int mode = 0; mode < 6; ++mode)
    {
        const auto i = (size_t)mode;
        localAssist->controller.invalidate(mode, localAssist->revision[i].fetch_add(1) + 1);
        localAssist->hasContext[i] = localAssist->hasUndo[i] = false;
        localAssist->rejection[i].clear();
    }
}
void OpenDuckProcessor::pushLocalAssist(const juce::AudioBuffer<float> &buffer)
{
    if (!localAssist || buffer.getNumSamples() <= 0) return;
    const auto *mainBus = getBus(true, 0);
    const auto *sideBus = getBus(true, 1);
    auto channel = [&buffer](const juce::AudioProcessor::Bus *bus, int index) -> const float *
    {
        if (!bus || !bus->isEnabled() || index >= bus->getNumberOfChannels()) return nullptr;
        const int position = bus->getChannelIndexInProcessBlockBuffer(index);
        return position >= 0 && position < buffer.getNumChannels() ? buffer.getReadPointer(position) : nullptr;
    };
    double ppq = std::numeric_limits<double>::quiet_NaN(), tempo = value("freeBpm");
    const bool synced = value("sync") > .5f;
    if (auto *playhead = getPlayHead())
        if (const auto position = playhead->getPosition())
        {
            if (const auto p = position->getPpqPosition()) ppq = *p;
            if (const auto b = position->getBpm()) if (synced) tempo = *b;
        }
    tempo = std::isfinite(tempo) ? juce::jlimit(20., 400., tempo) : 120.;
    // Publish the same effective clock as the DSP before capture metadata is
    // visible to the GUI timer, preventing a first-buffer 120->host BPM race.
    bpm.store((float)tempo, std::memory_order_release);
    localAssist->controller.push(channel(mainBus, 0), channel(mainBus, 1),
                                channel(sideBus, 0), channel(sideBus, 1), buffer.getNumSamples(), ppq, tempo);
}
odx::ai::Status OpenDuckProcessor::localAssistStatus(int mode) const
{
    mode = resolveMode(mode);
    if (!localAssist) return {};
    localAssist->check(mode);
    auto status = localAssist->controller.status(mode);
    const auto &reason = localAssist->rejection[(size_t)mode];
    if (!reason.empty()) { status.state = odx::ai::State::error; status.message = reason; }
    return status;
}
bool OpenDuckProcessor::startLocalAssistCapture(int mode, int seconds)
{
    mode = resolveMode(mode); if (!localAssist) return false;
    auto &assist = *localAssist; const auto i = (size_t)mode;
    assist.rejection[i].clear();
    if (!usesRevision3(mode)) assist.rejection[i] = "Upgrade the legacy audio engine before local analysis";
    else if (!assist.controller.available()) assist.rejection[i] = "Trained models unavailable; analysis is disabled";
    else if (value(sectorParameterId("group", mode)) > .5f)
        assist.rejection[i] = "Set this sector Group to Off before AI analysis to keep this instance independent";
    if (!assist.rejection[i].empty()) return false;
    const auto context = assist.context(mode);
    if (!assist.controller.start(mode, seconds, context))
    { assist.rejection[i] = "An analysis is active, or the capture length is not 5 or 10 seconds"; return false; }
    assist.capturedSignature[i] = signature(context, mode, false); assist.hasContext[i] = true;
    return true;
}
void OpenDuckProcessor::cancelLocalAssist(int mode)
{
    mode = resolveMode(mode);
    if (!localAssist) return;
    localAssist->controller.cancel(mode);
    // A click may arrive just after the worker published Ready, or while a
    // different sector owns the worker. Invalidate this slot as well so a queued
    // or just-completed proposal cannot become applicable after the user's Cancel.
    localAssist->controller.invalidate(mode, localAssist->revision[(size_t)mode].fetch_add(1) + 1);
    localAssist->hasContext[(size_t)mode] = false;
}
bool OpenDuckProcessor::requestLocalAssistStrength(int mode, float strength)
{
    mode = resolveMode(mode);
    if (!localAssist || !std::isfinite(strength) || strength < 0 || strength > 1 ||
        !usesRevision3(mode) || value(sectorParameterId("group", mode)) > .5f) return false;
    localAssist->check(mode);
    return localAssist->controller.requestStrength(mode, strength);
}
bool OpenDuckProcessor::localAssistProposal(int mode, odx::ai::Proposal &proposal) const
{
    mode = resolveMode(mode); if (!localAssist) return false;
    localAssist->check(mode); return localAssist->controller.proposal(mode, proposal);
}
bool OpenDuckProcessor::applyLocalAssist(int mode, float strength)
{
    mode = resolveMode(mode);
    if (!localAssist || !std::isfinite(strength) || strength <= 0 || !usesRevision3(mode) ||
        value(sectorParameterId("group", mode)) > .5f) return false;
    auto &assist = *localAssist; const auto i = (size_t)mode;
    odx::ai::Proposal proposal;
    if (!localAssistProposal(mode, proposal) || !proposal.improved ||
        std::abs(proposal.evaluatedStrength - juce::jlimit(0.f, 1.f, strength)) > 1.e-5f) return false;
    const auto current = assist.context(mode);
    // A nonlinear parameter interpolation is not guaranteed to preserve the full
    // target's quality. Only apply the exact candidate rendered and assessed by
    // the worker at this strength; the GUI requests a fresh evaluation on change.
    const auto &candidate = proposal.evaluatedCandidate;
    // Proposal writes are an explicit subset of this sector's captured controls.
    for (int k = 0; k < candidate.controlCount; ++k)
    {
        const auto &control = candidate.controls[(size_t)k]; bool allowed = false;
        for (int j = 0; j < current.baseline.controlCount; ++j)
            if (control.id == current.baseline.controls[(size_t)j].id) { allowed = true; break; }
        if (!allowed || !std::isfinite(control.value)) return false;
    }
    assist.beforeApply[i] = current.baseline;
    setCurveSnapshot(mode, candidate.curve, "Apply local assistant");
    for (int k = 0; k < candidate.controlCount; ++k)
    {
        const auto &control = candidate.controls[(size_t)k];
        if (auto *parameter = state.getParameter(juce::String(control.id)))
        {
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(control.value)); parameter->endChangeGesture();
        }
    }
    assist.appliedSignature[i] = signature(assist.context(mode), mode);
    assist.hasUndo[i] = true; assist.hasContext[i] = false;
    return true;
}
bool OpenDuckProcessor::canUndoLocalAssist(int mode) const
{
    mode = resolveMode(mode); if (!localAssist) return false;
    const auto i = (size_t)mode;
    return localAssist->hasUndo[i] && ((mode == 0 && value("template4") > .5f) || localAssist->appliedSignature[i] == signature(localAssist->context(mode), mode));
}
bool OpenDuckProcessor::undoLocalAssist(int mode)
{
    mode = resolveMode(mode); if (!canUndoLocalAssist(mode)) return false;
    auto &assist = *localAssist; const auto i = (size_t)mode;
    const auto candidate = assist.beforeApply[i];
    setCurveSnapshot(mode, candidate.curve, "Undo local assistant");
    for (int k = 0; k < candidate.controlCount; ++k)
    {
        const auto &control = candidate.controls[(size_t)k];
        if (auto *parameter = state.getParameter(juce::String(control.id)))
        {
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(control.value)); parameter->endChangeGesture();
        }
    }
    assist.hasUndo[i] = false;
    return true;
}
