#pragma once
#include "PluginProcessor.h"
#include "TemplateGeometry.h"

// The template editor is used only for new revision-4 instances. Legacy sessions
// retain OpenDuckEditor and their published controls/parameter interpretation.
class SingleDuckEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SingleDuckEditor(OpenDuckProcessor&);
    ~SingleDuckEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    class Look final : public juce::LookAndFeel_V4 {
    public:
        float scale = 1;
        juce::Image faceTexture;
        Look();
        void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
        void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
    } look;
    class Graph final : public juce::Component, public juce::SettableTooltipClient {
    public:
        explicit Graph(SingleDuckEditor& e):owner(e) {}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        SingleDuckEditor& owner;
        int node = -1;
        float panStart = 0, panMouse = 0;
        juce::Rectangle<float> plot() const;
        void edit(juce::Point<float>, bool add);
        void showViewMenu();
    } graph;
    OpenDuckProcessor& proc;
    juce::Image reference;
    juce::TooltipWindow tooltip{this,600};
    std::array<juce::Slider,3> knobs;
    std::array<juce::Label,3> readouts;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3> attachments;
    std::array<juce::TextButton,16> shapes;
    std::array<juce::TextButton,4> rates;
    std::array<juce::TextButton,3> triggers;
    juce::TextButton previous{"<"}, next{">"}, preset, manual{"MANUAL"}, ai{"AI"},
        five{"5s"}, ten{"10s"}, analyze{"ANALYZE"}, power{"POWER"},
        mirrorH{"MIRROR H"}, mirrorV{"MIRROR V"}, editPoints{"EDIT POINTS / OFF"};
    class MiniPanel final : public juce::Component {
    public:
        float scale=1;
        void paint(juce::Graphics& g) override {
            const auto r=getLocalBounds().toFloat().reduced(.5f*scale);
            g.setColour(juce::Colour(0xff08141a));g.fillRoundedRectangle(r,8*scale);
            g.setColour(juce::Colour(0xff73909a));g.drawRoundedRectangle(r,8*scale,scale);
        }
    } mini;
    juce::Label miniTitle, aiMessage;
    juce::TextButton close{"X"}, apply{"APPLY"}, undo{"UNDO"}, cancel{"CANCEL"};
    std::array<juce::Slider,4> settings;
    std::array<juce::Label,4> settingsLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> settingAttachments;
    juce::Label routing;
    std::unique_ptr<juce::ResizableCornerComponent> resizeGrip;
    juce::ComponentBoundsConstrainer constrainer;
    float scale=1;
    bool aiMode=false, settingsOpen=false;
    int seconds=5;
    int waveSource=0;
    bool waveSingle=false;
    float waveZoom=1, waveOffset=0;
    void saveWaveView();
    juce::Rectangle<int> box(float,float,float,float) const;
    void set(const juce::String&,float);
    void timerCallback() override;
    void selectPreset(int);
    void showSettings();
    void showAi();
    void popupPresets();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SingleDuckEditor)
};

