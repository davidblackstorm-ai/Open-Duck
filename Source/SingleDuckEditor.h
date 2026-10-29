#pragma once
#include "PluginProcessor.h"
#include "TemplateGeometry.h"
#include "ScopeWaveformView.h"
#include "DenseRotarySlider.h"

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
    bool keyPressed(const juce::KeyPress&) override;
private:
    class Look final : public juce::LookAndFeel_V4 {
    public:
        float scale = 1;
        OpenDuckProcessor* processor = nullptr;
        juce::Image faceTexture;
        Look();
        void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider::SliderStyle,juce::Slider&) override;
        juce::Label* createSliderTextBox(juce::Slider&) override;
        juce::Font getPopupMenuFont() override;
        void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
        void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
    } look;
    class Graph final : public juce::Component, public juce::SettableTooltipClient {
    public:
        explicit Graph(SingleDuckEditor&);
        void resized() override;
        void refreshUndo();
        void resetViewClassifier(){viewSelector.reset();viewDecision={};}
        void paint(juce::Graphics&) override;
        void mouseEnter(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        SingleDuckEditor& owner;
        juce::TextButton undoCurve;
        bool moving = false, panning = false, sloping = false;
        bool pointerInside = false;
        using ViewWindow = odx::view::CurveViewWindow;
        mutable odx::view::CurveViewSelector viewSelector;
        mutable odx::view::CurveViewDecision viewDecision;
        ViewWindow dragWindow;
        float panStart = 0, panMouse = 0, moveStart = 0, slopeStart = 0;
        juce::Rectangle<float> plot() const;
        bool triggeredTimeline() const;
        OpenDuckProcessor::ScopeFrame scopeFrame() const;
        std::vector<juce::Rectangle<float>> slopeRegions() const;
        std::vector<juce::Rectangle<float>> slopeRegions(const ViewWindow&, const OpenDuckProcessor::ScopeFrame* = nullptr) const;
        ViewWindow viewWindow(const OpenDuckProcessor::ScopeFrame&) const;
        bool guideVisible() const {return pointerInside||moving||panning||sloping;}
        void updatePointer(bool);
        void showViewMenu();
    } graph;
    OpenDuckProcessor& proc;
    juce::Image reference;
    juce::TooltipWindow tooltip{this,600};
    std::array<odx::ui4::DenseRotarySlider,3> knobs;
    std::array<juce::Label,3> readouts;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3> attachments;
    std::array<juce::TextButton,16> shapes;
    std::array<juce::TextButton,4> rates;
    std::array<juce::TextButton,3> triggers;
    juce::TextButton previous{"<"}, next{">"}, preset, power{"POWER"},
        mirrorH{"MIRROR H"}, mirrorV{"MIRROR V"};
    std::array<juce::TextButton,4> moveArrows;
    std::array<juce::TextButton,3> curveViews;
    juce::TextButton bandEnable{"BAND"}, bandListen;
    juce::Slider bandCutoff;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bandCutoffAttachment;
    class MiniPanel final : public juce::Component {
    public:
        float scale=1;
        void paint(juce::Graphics& g) override {
            const auto r=getLocalBounds().toFloat().reduced(.5f*scale);
            g.setColour(juce::Colour(0xff08141a));g.fillRoundedRectangle(r,8*scale);
            g.setColour(juce::Colour(0xff73909a));g.drawRoundedRectangle(r,8*scale,scale);
        }
    } mini;
    juce::Label miniTitle;
    juce::TextButton close{"X"};
    std::array<juce::Slider,4> settings;
    std::array<juce::Label,4> settingsLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> settingAttachments;
    juce::Label routing;
    juce::TextButton antiClick{"ANTI-CLICK"};
    juce::TextButton triggerLoop{"ONE SHOT"};
    juce::Label latencyInfo;
    bool settingsMeasured=false;
    std::unique_ptr<juce::ResizableCornerComponent> resizeGrip;
    juce::ComponentBoundsConstrainer constrainer;
    float scale=1;
    bool settingsOpen=false;
    int waveSource=0;
    bool waveSingle=false;
    int curveView=0;
    float waveZoom=1, waveOffset=0;
    int thumbnailRefreshDivider=0;
    juce::String activityLabel4() const;
    void saveWaveView();
    void restoreWaveView();
    juce::Rectangle<int> box(float,float,float,float) const;
    void set(const juce::String&,float);
    void performEdit(const juce::String&,const std::function<void()>&);
    void timerCallback() override;
    void selectPreset(int);
    void showSettings();
    void popupPresets();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SingleDuckEditor)
};

