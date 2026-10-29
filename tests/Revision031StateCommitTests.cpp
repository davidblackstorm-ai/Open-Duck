#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

// In-process fake host only: no DAW, editor, audio device or production state.
// A latency callback is synchronous on the caller thread. Saving at that point
// must observe the same complete transaction as saving after setState returns.
namespace
{
int passed = 0, failed = 0;
void check(bool condition, const char* label)
{
    std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
    condition ? ++passed : ++failed;
}
juce::ValueTree stateTree(OpenDuckProcessor& processor)
{
    juce::MemoryBlock data; processor.getStateInformation(data);
    auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), (int)data.getSize());
    if (!xml) throw std::runtime_error("Processor did not serialize XML");
    return juce::ValueTree::fromXml(*xml);
}
void load(OpenDuckProcessor& processor, const juce::ValueTree& tree)
{
    auto xml = tree.createXml(); juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    processor.setStateInformation(data.getData(), (int)data.getSize());
}
void parameter(juce::ValueTree tree, const juce::String& id, float value)
{
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);
        if (child.getProperty("id").toString() == id) { child.setProperty("value", value, nullptr); return; }
    }
    throw std::runtime_error("Missing parameter in fixture");
}
void points(juce::ValueTree tree, float middle)
{
    tree.removeAllChildren(nullptr); tree.setProperty("count", 3, nullptr);
    tree.setProperty("custom", true, nullptr); tree.setProperty("shape", 16, nullptr);
    for (int i = 0; i < 3; ++i)
    {
        juce::ValueTree point("Point"); point.setProperty("x", i * .5f, nullptr);
        point.setProperty("y", i == 1 ? middle : (float)i * .5f, nullptr);
        point.setProperty("segment", 0, nullptr); point.setProperty("curvature", 0, nullptr);
        point.setProperty("reversed", false, nullptr); tree.addChild(point, -1, nullptr);
    }
}
float groupMiddle(const juce::ValueTree& tree)
{
    auto group = tree.getChildWithName("GroupShapes3");
    for (int i = 0; i < group.getNumChildren(); ++i)
        if ((int)group.getChild(i).getProperty("mode", -1) == 0)
            return (float)group.getChild(i).getChild(1).getProperty("y", -100);
    return -100;
}
struct SavingHost final : juce::AudioProcessorListener
{
    explicit SavingHost(OpenDuckProcessor& processor) : p(processor) { p.addListener(this); }
    ~SavingHost() override { p.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (!details.latencyChanged) return;
        ++notifications; saved = stateTree(p); undoVisible = p.undo.canUndo();
        std::cout << "TRACE latency-callback PDC=" << p.getLatencySamples()
                  << " groupMiddle=" << groupMiddle(saved) << " undo=" << undoVisible << '\n';
    }
    OpenDuckProcessor& p; juce::ValueTree saved;
    int notifications = 0; bool undoVisible = false;
};
}
int main()
{
    std::cout << std::unitbuf; juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        auto owner = std::make_unique<OpenDuckProcessor>(); auto& p = *owner;
        auto modern = stateTree(p);
        // Explicit test fixture, not an archived compatibility claim: schema2
        // selects the supported 1152-sample path and guarantees a PDC change.
        auto legacy = modern.createCopy(); legacy.setProperty("schema", 2, nullptr);
        load(p, legacy); check(!p.usesRevision3(0) && p.getLatencySamples() == 1152,
                              "Fixture enters schema2 before the state-commit transition");
        auto local = p.getCurveSnapshot(0); local.modern3 = false; local.phase = 7;
        p.setCurveSnapshot(0, local, "Test pre-existing undo entry");
        check(p.undo.canUndo(), "Fixture contains pre-existing undo state");
        parameter(modern, OpenDuckProcessor::sectorParameterId("group", 0), 4);
        parameter(modern, OpenDuckProcessor::curveParameterId("shape", 0), 16);
        auto curve = modern.getChildWithName("Curves").getChild(0); points(curve, .19f);
        auto groups = modern.getChildWithName("GroupShapes3");
        groups.removeAllChildren(nullptr);
        // Clone a validated shape record to preserve every required scalar.
        auto shape = modern.getChildWithName("Bands3").getChild(0);
        juce::ValueTree group("GroupShape");
        for (int i = 0; i < shape.getNumProperties(); ++i)
            group.setProperty(shape.getPropertyName(i), shape.getProperty(shape.getPropertyName(i)), nullptr);
        group.removeProperty("band", nullptr); group.setProperty("mode", 0, nullptr);
        points(group, .83f); groups.addChild(group, -1, nullptr);
        SavingHost host(p); load(p, modern);
        const auto final = stateTree(p);
        check(p.usesRevision3(0) && host.notifications == 1,
              "Loading revision3 sends exactly one synchronous latency notification");
        check(std::abs(groupMiddle(final) - .83f) < 1e-6f,
              "The complete loaded group shape is available after state restoration");
        check(host.saved.isValid() && std::abs(groupMiddle(host.saved) - .83f) < 1e-6f,
              "Reentrant host save sees the complete restored group shape");
        check(!host.undoVisible && !p.undo.canUndo(),
              "Reentrant host callback sees restored undo state already cleared");
        check(host.saved.isEquivalentTo(final),
              "Reentrant state snapshot equals the state after setStateInformation returns");
    }
    catch (const std::exception& error) { std::cout << "FAIL state-commit exception: " << error.what() << '\n'; ++failed; }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
