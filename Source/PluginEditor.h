#pragma once
#include "PluginProcessor.h"
#include "PremiumLook.h"
#include "ScopeWaveformView.h"

class OpenDuckEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit OpenDuckEditor(OpenDuckProcessor &);
    ~OpenDuckEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void parentHierarchyChanged() override;
    juce::Rectangle<float> getCurvePlotBounds() const { return canvas.getPlotBounds(); }

  private:
    class Readout final : public juce::Component
    {
      public:
        Readout();
        juce::String number, unit;
        float scale = 1;
        double editValue = 0;
        std::function<void(double)> onCommit;
        void set(juce::String, juce::String, double);
        void paint(juce::Graphics &) override;
        void resized() override;
        void mouseDoubleClick(const juce::MouseEvent &) override;
        bool isEditing() const { return editor.isVisible(); }
      private:
        juce::TextEditor editor;
        void finish(bool);
    };
    class CurveCanvas final : public juce::Component
    {
      public:
        explicit CurveCanvas(OpenDuckEditor &);
        void paint(juce::Graphics &) override;
        void mouseDown(const juce::MouseEvent &) override;
        void mouseDrag(const juce::MouseEvent &) override;
        void mouseUp(const juce::MouseEvent &) override;
        void mouseDoubleClick(const juce::MouseEvent &) override;
        void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
        bool keyStateChanged(bool) override { return false; }
        bool keyPressed(const juce::KeyPress &) override;
        void setSegmentType(int);
        void duplicateSelected();
        juce::Rectangle<float> getPlotBounds() const { return plot(); }
      private:
        OpenDuckEditor &owner;
        enum class Drag { none, phase, point, bend, draw, attack, release, selection } dragging = Drag::none;
        OpenDuckProcessor::CurveSnapshot initial{}, working{};
        juce::Point<float> start{}, lastDraw{};
        std::vector<int> selection;
        int selectedPoint = -1, draggingMode = 0;
        juce::RangedAudioParameter *gestureParameter = nullptr;
        juce::Rectangle<float> plot() const;
        juce::Point<float> toCurve(juce::Point<float>, const OpenDuckProcessor::CurveSnapshot &, bool snap = true) const;
        juce::Point<float> toScreen(odx::CurvePoint, const OpenDuckProcessor::CurveSnapshot &) const;
        void prepareCustom(OpenDuckProcessor::CurveSnapshot &, bool dense);
        void commit(const OpenDuckProcessor::CurveSnapshot &);
        void drawSegment(juce::Point<float>, juce::Point<float>);
        bool applyCurvature(float, float);
    } canvas;
    class ResizeGrip final : public juce::ResizableCornerComponent
    {
      public:
        ResizeGrip(juce::Component *c, juce::ComponentBoundsConstrainer *b) : ResizableCornerComponent(c, b) {}
        void paint(juce::Graphics &) override;
    };
    class InsetPanel final : public juce::Component
    {
      public:
        float scale = 1;
        void paint(juce::Graphics &g) override { premium::panel(g, getLocalBounds().toFloat(), 10 * scale); }
    } advancedPane;
    class PrecisionSlider final : public juce::Slider
    {
      public:
        bool precision = false;
        void mouseDown(const juce::MouseEvent &e) override { juce::Slider::mouseDown(e); last = e.position; }
        void mouseDrag(const juce::MouseEvent &e) override
        {
            if (!precision) { juce::Slider::mouseDrag(e); return; }
            const auto delta = e.position - last; last = e.position;
            const double extent = getMouseDragSensitivity() * (e.mods.isShiftDown() ? 5.0 : 1.0);
            setValue(getValue() + (delta.x - delta.y) * (getMaximum() - getMinimum()) / extent, juce::sendNotificationSync);
        }
      private:
        juce::Point<float> last;
    };
    struct ParameterControl
    {
        juce::String id;
        juce::Label label;
        juce::Slider slider;
        juce::ComboBox choice;
        juce::ToggleButton toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    };
    struct ScopeView
    {
        std::array<float, 512> minimum{}, maximum{};
        double period = .5;
        float phase = 0;
        float referencePeak = .000001f;
        uint64_t generation = 0;
        int capturedBins = 0;
        odx::view::SingleHitSpan hit;
        odx::view::ScopeAxis axis;
        odx::view::ScopeState state = odx::view::ScopeState::waiting;
        double expectedPeriod = .5, hitDurationMs = -1;
        bool completed = false, hasAudio = false, sourceHasAudio = false, periodMatches = false;
    } scope;
    std::array<odx::view::ScopeFreshness, 6> scopeFreshness{};
    OpenDuckProcessor &proc;
    PremiumLook look;
    juce::Image frame;
    juce::TooltipWindow tooltips{this, 650};
    std::array<PrecisionSlider, 8> knobs;
    std::array<Readout, 8> values;
    std::array<juce::TextButton, 6> modes;
    std::array<juce::ToggleButton, 6> enabled, solo;
    std::array<juce::TextButton, 16> shapes;
    std::array<juce::ToggleButton, 16> steps;
    std::array<juce::TextButton, 6> advancedTabs;
    std::vector<std::unique_ptr<ParameterControl>> advancedControls;
    std::vector<std::unique_ptr<juce::TextButton>> toolButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> choiceAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandAttachment;
    juce::ComboBox behaviorMenu, speed, snap, group, displaySource, zoom, aiWindow, paintMode, segmentType, bandCurve;
    juce::Label bpmNumber, smoothingNumber, aiStatus, librarySearch, libraryInfo;
    juce::ToggleButton sync;
    juce::Slider smoothing;
    juce::Viewport advancedViewport;
    juce::Component advancedContent;
    juce::TextButton previous{"<"}, next{">"}, editPoints{"EDIT POINTS / OFF"}, freeDraw{"FREE DRAW"},
        band{"BAND"}, undoButton{"UNDO"}, redoButton{"REDO"}, removePoint{"- POINT"},
        advancedToggle{"ADVANCED"}, advancedClose{"X"}, savePreset{"SAVE PRESET"}, loadPreset{"LOAD PRESET"},
        mirrorH{"MIRROR H"}, mirrorV{"MIRROR V"}, fine{"FINE"}, freeze{"FREEZE"},
        aiAnalyze{"ANALYZE"}, aiCancel{"CANCEL"}, aiApply{"APPLY"}, aiUndo{"UNDO"},
        traceWav{"TRACE WAV"}, traceSc{"TRACE SC"}, saveShape{"SAVE SHAPE"}, loadShape{"LOAD SHAPE"},
        favorite{"FAVORITE"}, initButton{"INIT"}, upgrade{"UPGRADE AUDIO ENGINE TO 0.3 (SERIAL ROUTING)"};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ResizeGrip> resizeGrip;
    int selectedMode = -1, selectedPreset = -2, activeTab = 0;
    bool advancedExpanded = false, updating = false, lastPresetModified = false, frozen = false, phaseFine = false;
    float uiScale = 1, scopeZoom = 1, scopeOffset = 0;
    juce::String lastAiStatus, statusMessage;
    struct AssistUiSlot
    {
        float requestedStrength = 1;
        double changedAtMs = 0;
        uint64 generation = 0;
        bool hasTarget = false, pending = false;
    };
    std::array<AssistUiSlot, 6> assistUi{};
    int bandIndex() const;
    OpenDuckProcessor::CurveSnapshot editingCurve() const;
    void setEditingCurve(const OpenDuckProcessor::CurveSnapshot &, const juce::String & = {});
    int mode() const;
    juce::String sector(const char *) const;
    void setParameter(const juce::String &, float);
    void bindModeControls(bool force = false);
    void rebuildLibrary();
    void navigateBehavior(int);
    void buildAdvanced();
    void addAdvancedParameter(const juce::String &, const juce::String &);
    void addTool(const juce::String &, const juce::String &, std::function<void()>);
    void layoutAdvanced();
    void setAdvanced(bool);
    void transformShape(const juce::String &);
    void importTrace(bool);
    void updateScope();
    void updateAssist();
    void queueAssistStrength(bool immediate);
    void serviceAssistStrengthRequests();
    void timerCallback() override;
    void updateReadouts();
    double cycleSeconds() const;
    juce::Rectangle<int> designRect(float, float, float, float) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenDuckEditor)
};
