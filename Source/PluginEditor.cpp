#include "PluginEditor.h"
#include "LocalAssist.h"
#include <BinaryData.h>
#include <algorithm>

namespace
{
constexpr float designWidth = 1767, designHeight = 890;
const char *modeNames[] = {"DUCKING", "SPECTRAL", "MULTIBAND", "MORPH", "GROOVE", "TRANSIENT"};
float wrap(float x) { return x - std::floor(x); }
float sectorValue(const OpenDuckProcessor &p, const char *base, int mode)
{
    if (auto *v = p.state.getRawParameterValue(OpenDuckProcessor::sectorParameterId(base, mode))) return v->load();
    return 0;
}
void smallButton(juce::TextButton &b) { b.getProperties().set("premiumType", "small"); }
}

OpenDuckEditor::Readout::Readout()
{
    addChildComponent(editor);
    editor.setJustification(juce::Justification::centred);
    editor.setSelectAllWhenFocused(true);
    editor.onReturnKey = [this] { finish(true); };
    editor.onEscapeKey = [this] { finish(false); };
    editor.onFocusLost = [this] { if (editor.isVisible()) finish(true); };
    setMouseCursor(juce::MouseCursor::IBeamCursor);
    setTitle("Double click to type an exact value");
}
void OpenDuckEditor::Readout::finish(bool commit)
{
    const auto text = editor.getText().trim().replaceCharacter(',', '.');
    editor.setVisible(false);
    if (commit && text.containsAnyOf("0123456789") && onCommit)
    {
        auto v = text.getDoubleValue();
        if (text.containsIgnoreCase("khz")) v *= 1000;
        if (std::isfinite(v)) onCommit(v);
    }
    repaint();
}
void OpenDuckEditor::Readout::mouseDoubleClick(const juce::MouseEvent &)
{
    if (!isEnabled() || !onCommit) return;
    editor.setText(juce::String(editValue, 3), false);
    editor.setFont(premium::font(juce::jmax(12.0f, 22 * scale)));
    editor.setVisible(true); editor.grabKeyboardFocus(); editor.selectAll();
}
void OpenDuckEditor::Readout::resized() { editor.setBounds(getLocalBounds().reduced(1)); }
void OpenDuckEditor::Readout::set(juce::String n, juce::String u, double value)
{
    editValue = value;
    if (number != n || unit != u) { number = std::move(n); unit = std::move(u); repaint(); }
}
void OpenDuckEditor::Readout::paint(juce::Graphics &g)
{
    auto r = getLocalBounds().toFloat().reduced(1); premium::lcd(g, r, 5 * scale);
    const auto nf = premium::font(juce::jmin(r.getHeight() * .72f, 28 * scale));
    const auto uf = premium::font(juce::jmax(9.0f, 13 * scale), false);
    juce::GlyphArrangement n, u; n.addLineOfText(nf, number, 0, 0); u.addLineOfText(uf, unit, 0, 0);
    const float nw = n.getBoundingBox(0, -1, true).getWidth(), uw = u.getBoundingBox(0, -1, true).getWidth();
    const float gap = unit.isEmpty() ? 0.0f : 5 * scale, x = r.getCentreX() - (nw + uw + gap) / 2;
    g.setFont(nf); g.setColour(isEnabled() ? juce::Colour(0xffecfaff) : juce::Colour(0xff617d90));
    g.drawText(number, juce::Rectangle<float>(x, r.getY(), nw + 3 * scale, r.getHeight()), juce::Justification::centredLeft);
    g.setFont(uf); g.setColour(juce::Colour(0xff9dcce1));
    g.drawText(unit, juce::Rectangle<float>(x + nw + gap, r.getY() + 3 * scale, uw + 3 * scale, r.getHeight() - 3 * scale), juce::Justification::centredLeft);
}

OpenDuckEditor::OpenDuckEditor(OpenDuckProcessor &p) : AudioProcessorEditor(p), canvas(*this), proc(p)
{
    int bytes = 0;
    if (auto *data = BinaryData::getNamedResource("PremiumFrame_png", bytes))
        frame = juce::SoftwareImageType().convert(juce::ImageCache::getFromMemory(data, bytes));
    setOpaque(false); setLookAndFeel(&look); setResizable(true, false);
    getConstrainer()->setFixedAspectRatio(designWidth / designHeight); setResizeLimits(1010, 508, 1767, 890);
    resizeGrip = std::make_unique<ResizeGrip>(this, getConstrainer());
    addAndMakeVisible(*resizeGrip); resizeGrip->setName("chromeResize"); resizeGrip->setAlwaysOnTop(true);
    look.shapeSample = [](float t, int shape) { return odx::factoryShape(t, shape); };
    addAndMakeVisible(canvas);
    const char *knobNames[] = {"amount", "attack", "release", "tension", "phase", "mix", "bandFocus", "assistStrength"};
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto &k = knobs[i]; k.setName(knobNames[i]);
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); k.setMouseDragSensitivity(i == 4 ? 1200 : 280);
        k.setWantsKeyboardFocus(true); k.setTooltip(i == 4 ? "Horizontal phase. FINE or Shift: precise adjustment." : "Drag or use arrow keys. Double click the numeric readout to type.");
        k.precision = i == 4;
        addAndMakeVisible(k); addAndMakeVisible(values[i]); values[i].setName(juce::String(knobNames[i]) + "Value");
        values[i].onCommit = [this, i](double value) { knobs[i].setValue(i == 7 ? value / 100 : value, juce::sendNotificationSync); };
        k.onValueChange = [this, i]
        {
            if (!updating && bandIndex() >= 0 && i < 5)
            {
                auto c = editingCurve(); const float value = (float)knobs[i].getValue();
                if (i == 0) c.amount = value; if (i == 1) c.attack = value; if (i == 2) c.release = value; if (i == 3) c.tension = value; if (i == 4) c.phase = value;
                setEditingCurve(c);
            }
            if (!updating && i == 7) { queueAssistStrength(false); updateAssist(); }
            updateReadouts(); canvas.repaint();
        };
    }
    for (int i = 0; i < 6; ++i)
        assistUi[(size_t)i].requestedStrength = juce::jlimit(0.0f, 1.0f, (float)proc.state.state.getProperty("editorAssistStrength" + juce::String(i), 1.0));
    knobs[7].setRange(0, 1, .01); knobs[7].setValue(1, juce::dontSendNotification); knobs[7].setEnabled(false); values[7].setEnabled(false);
    knobs[7].setTooltip("This exact Strength is rendered and checked before APPLY. Changing Strength never applies audio changes automatically.");
    knobs[7].onDragEnd = [this] { queueAssistStrength(true); updateAssist(); };
    for (int i = 0; i < 6; ++i)
    {
        auto &b = modes[(size_t)i]; b.setName("mode" + juce::String(i)); b.setButtonText(modeNames[i]);
        b.getProperties().set("premiumType", "mode"); b.getProperties().set("modeIndex", i);
        b.setTitle(juce::String(modeNames[i]) + " controls (selection only)");
        b.setTooltip("Select the controls to edit. Selection does not enable processing. ON/OFF switches each engine; active engines process audio in series.");
        b.onClick = [this, i] { setParameter("selected", (float)i); bindModeControls(); resized(); timerCallback(); };
        addAndMakeVisible(b);
        auto &on = enabled[(size_t)i]; on.setName("on" + juce::String(i)); on.getProperties().set("engineIndex", i); addAndMakeVisible(on);
        on.setTooltip("ON enables this engine in the serial audio chain; OFF bypasses its processing. Selecting a card only changes the controls shown. S can audition an engine independently.");
        on.onStateChange = [this, i]
        {
            auto &button = enabled[(size_t)i]; const auto label = button.getToggleState() ? "ON" : "OFF";
            button.setButtonText(label); button.setTitle(juce::String(modeNames[i]) + " processing " + label);
        };
        on.onStateChange();
        auto &s = solo[(size_t)i]; s.setName("solo" + juce::String(i)); s.setButtonText("S"); addAndMakeVisible(s);
        s.setTitle(juce::String(modeNames[i]) + " solo processing");
        s.setTooltip("S auditions this engine. While any S is active, only soloed engines process audio. Selecting a card changes the controls shown.");
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "on" + juce::String(i), on));
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "solo" + juce::String(i), s));
    }
    for (int i = 0; i < 16; ++i)
    {
        auto &b = shapes[(size_t)i]; b.setName("shape" + juce::String(i)); b.setTooltip(OpenDuckProcessor::factoryShapeNames()[(size_t)i]);
        b.getProperties().set("premiumType", "shape"); b.getProperties().set("shapeIndex", i);
        b.onClick = [this, i]
        {
            if (bandIndex() < 0 && proc.usesRevision3(mode())) { proc.selectFactoryShape(i, mode()); timerCallback(); return; }
            auto curve = editingCurve(); curve.shape = i; curve.custom = false; curve.count = 0; curve.linear = false;
            curve.edit = false; curve.freeDraw = false; curve.length = 1; curve.presetId = -1;
            setEditingCurve(curve, "Select shape"); timerCallback();
        }; addAndMakeVisible(b);
        auto &step = steps[(size_t)i]; step.setName("step" + juce::String(i)); step.setButtonText(juce::String(i + 1));
        advancedContent.addChildComponent(step);
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "step" + juce::String(i), step));
    }
    behaviorMenu.setName("behaviorMenu"); addAndMakeVisible(behaviorMenu);
    behaviorMenu.onChange = [this] { if (!updating && behaviorMenu.getSelectedId() > 0) { proc.applySectorPreset(behaviorMenu.getSelectedId() - 1, mode()); timerCallback(); } };
    previous.setName("behaviorPrevious"); next.setName("behaviorNext"); previous.onClick = [this] { navigateBehavior(-1); }; next.onClick = [this] { navigateBehavior(1); };
    editPoints.setName("editPoints"); freeDraw.setName("freeDraw"); band.setName("bandEnabled");
    editPoints.onClick = [this]
    {
        if (bandIndex() < 0) proc.setEditPointsEnabled(!proc.editPointsEnabled(), mode());
        else { auto c = editingCurve(); c.edit = !c.edit; if (!c.edit) c.freeDraw = false; setEditingCurve(c, "Toggle band point editing"); }
        timerCallback();
    };
    freeDraw.onClick = [this]
    {
        if (bandIndex() < 0) proc.setFreeDrawEnabled(!proc.freeDrawEnabled(), mode());
        else { auto c = editingCurve(); c.freeDraw = !c.freeDraw; if (c.freeDraw) c.edit = true; setEditingCurve(c, "Toggle band free draw"); }
        timerCallback();
    };
    band.onClick = [this] { proc.setSectorParameter(mode(), "bands", sectorValue(proc, "bands", mode()) > .5f ? 0.0f : 1.0f); timerCallback(); };
    mirrorH.setName("mirrorHorizontal"); mirrorV.setName("mirrorVertical"); mirrorH.onClick = [this] { transformShape("reverse"); }; mirrorV.onClick = [this] { transformShape("invert"); };
    undoButton.setName("undo"); redoButton.setName("redo"); removePoint.setName("removePoint");
    undoButton.onClick = [this] { proc.undo.undo(); timerCallback(); }; redoButton.onClick = [this] { proc.undo.redo(); timerCallback(); };
    removePoint.onClick = [this] { canvas.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)); };
    fine.setName("phaseFine"); fine.onClick = [this]
    {
        phaseFine = !phaseFine; proc.state.state.setProperty("editorPhaseFine", phaseFine, nullptr);
        knobs[4].setMouseDragSensitivity(phaseFine ? 6000 : 1200); fine.setToggleState(phaseFine, juce::dontSendNotification);
    };
    freeze.setName("scopeFreeze"); freeze.onClick = [this] { frozen = !proc.isScopeFrozen(); proc.setScopeFrozen(frozen); timerCallback(); };
    advancedToggle.setName("details"); advancedToggle.onClick = [this] { setAdvanced(!advancedExpanded); };
    upgrade.setName("upgradeRevision3"); smallButton(upgrade); addAndMakeVisible(upgrade);
    upgrade.onClick = [this] { proc.upgradeToRevision3(); bindModeControls(true); timerCallback(); };
    for (auto *b : {&previous, &next, &editPoints, &freeDraw, &band, &undoButton, &redoButton, &removePoint, &mirrorH, &mirrorV, &fine, &freeze, &advancedToggle}) { addAndMakeVisible(*b); smallButton(*b); }
    displaySource.setName("scopeSource"); displaySource.addItemList({"INPUT", "SIDECHAIN / KICK", "OUTPUT"}, 1); displaySource.setSelectedId(2);
    displaySource.onChange = [this] { if (!updating) { proc.state.state.setProperty("editorScopeSource", displaySource.getSelectedId(), nullptr); updateScope(); canvas.repaint(); } };
    zoom.setName("scopeZoom"); zoom.addItemList({"1x", "2x", "4x", "8x"}, 1); zoom.setSelectedId(1);
    zoom.onChange = [this] { scopeZoom = std::pow(2.0f, (float)(zoom.getSelectedId() - 1)); scopeOffset = juce::jlimit(0.0f, 1 - 1 / scopeZoom, scopeOffset); if (!updating) proc.state.state.setProperty("editorScopeZoom", zoom.getSelectedId(), nullptr); updateScope(); canvas.repaint(); };
    speed.setName("speed"); snap.setName("snap"); group.setName("group");
    for (auto *c : {&speed, &snap, &group, &displaySource, &zoom}) addAndMakeVisible(*c);
    smoothing.setName("smoothing"); smoothing.setSliderStyle(juce::Slider::LinearHorizontal); smoothing.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    smoothing.onValueChange = [this] { smoothingNumber.setText(juce::String(smoothing.getValue(), 1) + " ms", juce::dontSendNotification); canvas.repaint(); };
    addAndMakeVisible(smoothing); addAndMakeVisible(smoothingNumber);
    sync.setName("hostSync"); sync.setButtonText("HOST SYNC"); addAndMakeVisible(sync);
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, "sync", sync));
    bpmNumber.setName("freeBpm"); bpmNumber.setJustificationType(juce::Justification::centred);
    bpmNumber.setColour(juce::Label::textColourId, juce::Colour(0xffe3f7ff)); addAndMakeVisible(bpmNumber);
    bpmNumber.onTextChange = [this] { if (!updating && proc.value("sync") < .5f) setParameter("freeBpm", juce::jlimit(30.0f, 300.0f, bpmNumber.getText().getFloatValue())); };
    addAndMakeVisible(advancedPane); advancedPane.setName("advancedPane"); advancedPane.setVisible(false);
    const char *tabs[] = {"ENVELOPE", "TRIGGER", "BANDS", "TOOLS", "LIBRARY", "ENGINE"};
    for (int i = 0; i < 6; ++i) { auto &b = advancedTabs[(size_t)i]; b.setButtonText(tabs[i]); b.setName("advancedTab" + juce::String(i)); smallButton(b); advancedPane.addAndMakeVisible(b); b.onClick = [this, i] { activeTab = i; buildAdvanced(); timerCallback(); }; }
    advancedClose.setName("advancedClose"); smallButton(advancedClose); advancedPane.addAndMakeVisible(advancedClose); advancedClose.onClick = [this] { setAdvanced(false); };
    advancedViewport.setViewedComponent(&advancedContent, false); advancedViewport.setScrollBarsShown(true, false); advancedPane.addAndMakeVisible(advancedViewport);
    advancedContent.setName("advancedControls");
    paintMode.setName("paintMode"); paintMode.addItemList({"FREE", "RAMP", "STEP", "GATE"}, 1); paintMode.setSelectedId(1); advancedContent.addChildComponent(paintMode);
    segmentType.setName("segmentType"); segmentType.addItemList({"SMOOTH", "SMOOTH 2D", "SPLINE", "S CURVE", "SINE", "SAW / LINEAR"}, 1); segmentType.setSelectedId(1);
    segmentType.onChange = [this] { if (!updating) canvas.setSegmentType(segmentType.getSelectedId() - 1); }; advancedContent.addChildComponent(segmentType);
    bandCurve.setName("bandCurve"); bandCurve.addItemList({"LINKED / MAIN", "LOW CURVE", "MID CURVE", "HIGH CURVE"}, 1); bandCurve.setSelectedId(1);
    bandCurve.onChange = [this] { proc.state.state.setProperty("editorBandCurve" + juce::String(mode()), bandCurve.getSelectedId(), nullptr); bindModeControls(true); canvas.repaint(); timerCallback(); }; advancedContent.addChildComponent(bandCurve);
    for (auto *b : {&savePreset, &loadPreset, &traceWav, &traceSc, &saveShape, &loadShape, &favorite, &initButton}) { smallButton(*b); advancedContent.addChildComponent(*b); }
    traceWav.setName("traceWav"); traceSc.setName("traceSidechain"); traceWav.onClick = [this] { importTrace(false); }; traceSc.onClick = [this] { importTrace(true); };
    const auto presetFolder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenDuck/Pro X/Presets");
    savePreset.onClick = [this, presetFolder]
    {
        presetFolder.createDirectory(); chooser = std::make_unique<juce::FileChooser>("Save OpenDuck preset", presetFolder.getChildFile("My preset.odpx"), "*.odpx");
        auto safe = juce::Component::SafePointer<OpenDuckEditor>(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser &f)
        { if (safe != nullptr && f.getResult() != juce::File{}) { juce::MemoryBlock data; safe->proc.getStateInformation(data); safe->statusMessage = f.getResult().replaceWithData(data.getData(), data.getSize()) ? "Preset saved" : "Preset could not be saved"; } });
    };
    loadPreset.onClick = [this, presetFolder]
    {
        chooser = std::make_unique<juce::FileChooser>("Load OpenDuck preset", presetFolder, "*.odpx"); auto safe = juce::Component::SafePointer<OpenDuckEditor>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser &f)
        { juce::MemoryBlock data; if (safe != nullptr && f.getResult().loadFileAsData(data)) { safe->proc.setStateInformation(data.getData(), (int)data.getSize()); safe->bindModeControls(); safe->resized(); safe->timerCallback(); } });
    };
    saveShape.onClick = [this, presetFolder]
    {
        presetFolder.createDirectory(); chooser = std::make_unique<juce::FileChooser>("Save curve only", presetFolder.getChildFile("My curve.odcurve"), "*.odcurve"); auto safe = juce::Component::SafePointer<OpenDuckEditor>(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe](const juce::FileChooser &f)
        { if (safe != nullptr && f.getResult() != juce::File{}) safe->statusMessage = safe->proc.saveUserCurve(f.getResult(), safe->mode()).getErrorMessage(); });
    };
    loadShape.onClick = [this, presetFolder]
    {
        chooser = std::make_unique<juce::FileChooser>("Load curve only", presetFolder, "*.odcurve"); auto safe = juce::Component::SafePointer<OpenDuckEditor>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser &f)
        { if (safe != nullptr && f.getResult() != juce::File{}) { const auto result = safe->proc.loadUserCurve(f.getResult(), safe->mode()); safe->statusMessage = result.wasOk() ? "Curve loaded" : result.getErrorMessage(); safe->timerCallback(); } });
    };
    favorite.setName("favorite"); favorite.onClick = [this]
    {
        const auto key = "favorite" + juce::String(proc.currentBehaviorPresetId()); const bool on = !(bool)proc.state.state.getProperty(key, false);
        proc.state.state.setProperty(key, on, nullptr); favorite.setToggleState(on, juce::dontSendNotification); rebuildLibrary();
    };
    initButton.setName("init"); initButton.onClick = [this] { auto ids = OpenDuckProcessor::sectorPresetIds(mode()); if (!ids.empty()) proc.applySectorPreset(ids.front(), mode()); timerCallback(); };
    librarySearch.setName("presetSearch"); librarySearch.setEditable(true); librarySearch.setText("Search presets", juce::dontSendNotification); librarySearch.onTextChange = [this] { rebuildLibrary(); }; advancedContent.addChildComponent(librarySearch);
    libraryInfo.setName("libraryInfo"); advancedContent.addChildComponent(libraryInfo);
    aiWindow.setName("assistWindow"); aiWindow.addItemList({"5 s", "10 s"}, 1); aiWindow.setSelectedId(1); addAndMakeVisible(aiWindow);
    aiWindow.onChange = [this] { if (!updating) proc.state.state.setProperty("editorAssistWindow" + juce::String(mode()), aiWindow.getSelectedId(), nullptr); };
    aiStatus.setName("assistStatus"); aiStatus.setColour(juce::Label::textColourId, juce::Colour(0xff94b9cd)); addAndMakeVisible(aiStatus);
    for (auto *b : {&aiAnalyze, &aiCancel, &aiApply, &aiUndo}) { smallButton(*b); addAndMakeVisible(*b); }
    aiAnalyze.setName("assistAnalyze"); aiCancel.setName("assistCancel"); aiApply.setName("assistApply"); aiUndo.setName("assistUndo");
    aiAnalyze.onClick = [this]
    {
        if (proc.startLocalAssistCapture(mode(), aiWindow.getSelectedId() == 2 ? 10 : 5))
        { auto &slot = assistUi[(size_t)mode()]; slot.hasTarget = slot.pending = false; slot.generation = 0; }
        updateAssist();
    };
    aiCancel.onClick = [this]
    {
        auto &slot = assistUi[(size_t)mode()]; slot.pending = slot.hasTarget = false;
        proc.cancelLocalAssist(mode()); updateAssist();
    };
    aiApply.onClick = [this]
    {
        updateAssist();
        if (aiApply.isEnabled()) proc.applyLocalAssist(mode(), assistUi[(size_t)mode()].requestedStrength);
        updateAssist(); timerCallback();
    };
    aiUndo.onClick = [this] { proc.undoLocalAssist(mode()); updateAssist(); timerCallback(); };
    advancedExpanded = (bool)proc.state.state.getProperty("editorDetails", false);
    phaseFine = (bool)proc.state.state.getProperty("editorPhaseFine", false);
    knobs[4].setMouseDragSensitivity(phaseFine ? 6000 : 1200); fine.setToggleState(phaseFine, juce::dontSendNotification);
    displaySource.setSelectedId(juce::jlimit(1, 3, (int)proc.state.state.getProperty("editorScopeSource", 2)), juce::dontSendNotification);
    zoom.setSelectedId(juce::jlimit(1, 4, (int)proc.state.state.getProperty("editorScopeZoom", 1)), juce::dontSendNotification); scopeZoom = std::pow(2.0f, (float)(zoom.getSelectedId() - 1));
    bindModeControls(); setAdvanced(advancedExpanded);
    const int width = juce::jlimit(1010, 1767, (int)proc.state.state.getProperty("editorWidth", 1326)); setSize(width, juce::roundToInt(width * designHeight / designWidth));
    timerCallback(); startTimerHz(30);
}

OpenDuckEditor::~OpenDuckEditor() { stopTimer(); advancedViewport.setViewedComponent(nullptr, false); setLookAndFeel(nullptr); }
int OpenDuckEditor::mode() const { return juce::jlimit(0, 5, (int)proc.value("selected")); }
juce::String OpenDuckEditor::sector(const char *base) const { return OpenDuckProcessor::sectorParameterId(base, mode()); }
int OpenDuckEditor::bandIndex() const { return bandCurve.getSelectedId() <= 1 ? -1 : bandCurve.getSelectedId() - 2; }
OpenDuckProcessor::CurveSnapshot OpenDuckEditor::editingCurve() const { return bandIndex() < 0 ? proc.getCurveSnapshot(mode()) : proc.getBandCurveSnapshot(mode(), bandIndex()); }
void OpenDuckEditor::setEditingCurve(const OpenDuckProcessor::CurveSnapshot &c, const juce::String &transaction)
{
    if (bandIndex() < 0) proc.setCurveSnapshot(mode(), c, transaction);
    else { if (transaction.isNotEmpty()) proc.undo.beginNewTransaction(transaction); proc.setBandCurveSnapshot(mode(), bandIndex(), c); }
}
double OpenDuckEditor::cycleSeconds() const { return proc.cycleSeconds(mode()); }
void OpenDuckEditor::setParameter(const juce::String &id, float value)
{
    if (auto *p = proc.state.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture(); }
}
void OpenDuckEditor::bindModeControls(bool force)
{
    if (!force && mode() == selectedMode && !knobAttachments.empty()) return;
    const bool modeChanged = mode() != selectedMode;
    selectedMode = mode(); selectedPreset = -2; knobAttachments.clear(); choiceAttachments.clear(); smoothingAttachment.reset(); bandAttachment.reset();
    knobs[7].setValue(assistUi[(size_t)mode()].requestedStrength, juce::dontSendNotification);
    if (modeChanged) bandCurve.setSelectedId(juce::jlimit(1, 4, (int)proc.state.state.getProperty("editorBandCurve" + juce::String(mode()), 1)), juce::dontSendNotification);
    if (modeChanged) aiWindow.setSelectedId(juce::jlimit(1, 2, (int)proc.state.state.getProperty("editorAssistWindow" + juce::String(mode()), 1)), juce::dontSendNotification);
    const char *bases[] = {"amount", "attack", "release", "tension", "phase"};
    for (int i = 0; i < 7; ++i)
    {
        const auto id = i < 5 ? OpenDuckProcessor::curveParameterId(bases[i], mode()) : i == 5 ? juce::String("mix") : sector("crossLow");
        if (i < 5 && bandIndex() >= 0)
        {
            updating = true; const auto &range = proc.state.getParameter(id)->getNormalisableRange(); knobs[(size_t)i].setRange(range.start, range.end, range.interval); knobs[(size_t)i].setSkewFactor(range.skew, range.symmetricSkew); const auto c = editingCurve();
            const float fields[] = {c.amount, c.attack, c.release, c.tension, c.phase}; knobs[(size_t)i].setValue(fields[i], juce::dontSendNotification); updating = false; continue;
        }
        knobAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state, id, knobs[(size_t)i]));
    }
    const auto attachChoice = [this](juce::ComboBox &box, const char *base)
    {
        box.clear(juce::dontSendNotification); auto *p = proc.state.getParameter(sector(base));
        if (p == nullptr) return; box.addItemList(p->getAllValueStrings(), 1);
        choiceAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, sector(base), box));
    };
    attachChoice(speed, "speed"); attachChoice(snap, "snap"); attachChoice(group, "group");
    smoothingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state, sector("smooth"), smoothing);
    rebuildLibrary(); buildAdvanced(); updateReadouts();
}
void OpenDuckEditor::navigateBehavior(int direction)
{
    const auto ids = OpenDuckProcessor::sectorPresetIds(mode()); if (ids.empty()) return;
    const auto found = std::find(ids.begin(), ids.end(), proc.currentBehaviorPresetId(mode()));
    const int index = found == ids.end() ? (direction > 0 ? -1 : 0) : (int)std::distance(ids.begin(), found);
    proc.applySectorPreset(ids[(size_t)((index + direction + (int)ids.size()) % (int)ids.size())], mode()); timerCallback();
}
void OpenDuckEditor::rebuildLibrary()
{
    const int current = proc.currentBehaviorPresetId(mode()); behaviorMenu.clear(juce::dontSendNotification);
    auto search = librarySearch.getText().trim(); if (search == "Search presets") search.clear();
    for (int category = -1; category < 7; ++category)
    {
        juce::PopupMenu menu;
        for (const auto id : OpenDuckProcessor::sectorPresetIds(mode(), category < 0 ? -1 : category))
        {
            const auto &p = OpenDuckProcessor::sectorPresets()[(size_t)id];
            if (category < 0 && !(bool)proc.state.state.getProperty("favorite" + juce::String(id), false)) continue;
            if (search.isNotEmpty() && !p.name.containsIgnoreCase(search)) continue;
            auto label = p.name; const int slash = label.indexOfChar('/');
            if (slash >= 0 && label.substring(0, slash).trim().equalsIgnoreCase(modeNames[mode()])) label = label.substring(slash + 1).trim();
            menu.addItem(id + 1, juce::String(modeNames[mode()]) + " / " + label);
        }
        if (menu.getNumItems() > 0) behaviorMenu.getRootMenu()->addSubMenu(category < 0 ? "Favorites" : OpenDuckProcessor::factoryCategoryNames()[(size_t)category], menu);
    }
    behaviorMenu.setSelectedId(current + 1, juce::dontSendNotification); selectedPreset = -2;
    libraryInfo.setText("700 original behaviors across six sectors. Current library: " + juce::String((int)OpenDuckProcessor::sectorPresetIds(mode()).size()) + ". Preset browsing preserves routing, MIX and trigger settings.", juce::dontSendNotification);
}

void OpenDuckEditor::addAdvancedParameter(const juce::String &id, const juce::String &label)
{
    auto *p = proc.state.getParameter(id); if (p == nullptr) return;
    auto c = std::make_unique<ParameterControl>(); c->id = id;
    c->label.setText(label, juce::dontSendNotification); c->label.setColour(juce::Label::textColourId, juce::Colour(0xffa7ccdf));
    c->label.setInterceptsMouseClicks(false, false); advancedContent.addAndMakeVisible(c->label);
    const auto booleanControl = [&id]
    {
        for (const char *prefix : {"v3Linked", "v3EndHold", "v3SnapEnabled", "v3Locked", "v3Listen", "v3LowSolo", "v3MidSolo", "v3HighSolo", "v3RunStopped"}) if (id.startsWith(prefix)) return true;
        return false;
    };
    if (dynamic_cast<juce::AudioParameterBool *>(p) != nullptr || booleanControl())
    {
        c->toggle.setName(id); c->toggle.setButtonText("ON"); advancedContent.addAndMakeVisible(c->toggle);
        c->toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state, id, c->toggle);
    }
    else if (p->getAllValueStrings().size() > 0)
    {
        c->choice.setName(id); c->choice.addItemList(p->getAllValueStrings(), 1); advancedContent.addAndMakeVisible(c->choice);
        c->choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state, id, c->choice);
    }
    else
    {
        c->slider.setName(id); c->slider.setSliderStyle(juce::Slider::LinearHorizontal); c->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 23);
        c->slider.setTextValueSuffix(p->getLabel().isEmpty() ? "" : " " + p->getLabel()); advancedContent.addAndMakeVisible(c->slider);
        c->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state, id, c->slider);
    }
    advancedControls.push_back(std::move(c));
}
void OpenDuckEditor::addTool(const juce::String &name, const juce::String &label, std::function<void()> action)
{
    auto b = std::make_unique<juce::TextButton>(label); b->setName(name); smallButton(*b); b->onClick = std::move(action); advancedContent.addAndMakeVisible(*b); toolButtons.push_back(std::move(b));
}
void OpenDuckEditor::buildAdvanced()
{
    advancedControls.clear(); toolButtons.clear();
    for (auto *c : std::initializer_list<juce::Component *>{&paintMode, &segmentType, &bandCurve, &savePreset, &loadPreset, &saveShape, &loadShape, &traceWav, &traceSc, &favorite, &initButton, &librarySearch, &libraryInfo}) c->setVisible(false);
    for (auto &s : steps) s.setVisible(false);
    const auto add = [this](const char *base, const char *label) { addAdvancedParameter(sector(base), label); };
    if (activeTab == 0)
    {
        add("speedMode", "CLOCK MODE"); add("hz", "FREE SPEED / Hz"); add("smooth", "SMOOTHING / ms");
        add("riseSlope", "RISE SLOPE"); add("end", "END MARKER / CYCLE"); add("endHold", "HOLD END VALUE");
        add("snapEnabled", "SNAP ENABLED"); add("swing", "GRID SWING / %"); add("offset", "OFFSET / ms");
        add("runStopped", "RUN WHILE STOPPED");
        add("output", "OUTPUT GAIN / dB"); add("character", "ENGINE CHARACTER");
        addTool("phaseLeft16", "SHIFT -1/16", [this] { setParameter(OpenDuckProcessor::curveParameterId("phase", mode()), juce::jlimit(-50.0f, 50.0f, editingCurve().phase - 6.25f)); });
        addTool("phaseRight16", "SHIFT +1/16", [this] { setParameter(OpenDuckProcessor::curveParameterId("phase", mode()), juce::jlimit(-50.0f, 50.0f, editingCurve().phase + 6.25f)); });
        addTool("phaseLeft256", "SHIFT -1/256", [this] { setParameter(OpenDuckProcessor::curveParameterId("phase", mode()), juce::jlimit(-50.0f, 50.0f, editingCurve().phase - .390625f)); });
        addTool("phaseRight256", "SHIFT +1/256", [this] { setParameter(OpenDuckProcessor::curveParameterId("phase", mode()), juce::jlimit(-50.0f, 50.0f, editingCurve().phase + .390625f)); });
        addTool("resetTransforms", "RESET SHIFT / SLOPE", [this] { transformShape("reset"); });
    }
    if (activeTab == 1)
    {
        add("trigger", "TRIGGER SOURCE"); add("loop", "REPEAT / LOOP"); add("locked", "LOCK TRIGGER SETTINGS");
        add("loopStart", "SUSTAIN LOOP START"); add("loopEnd", "SUSTAIN LOOP END"); add("threshold", "TRIGGER LEVEL / dB");
        add("hysteresis", "HYSTERESIS / dB"); add("holdoff", "HOLD OFF / ms"); add("detectorAttack", "DETECTOR ATTACK / ms");
        add("detectorRelease", "DETECTOR RELEASE / ms"); add("detectorLow", "DETECTOR LOW / Hz"); add("detectorHigh", "DETECTOR HIGH / Hz");
        add("listen", "LISTEN TO DETECTOR"); add("lookahead", "LOOKAHEAD / ms"); add("midiChannel", "MIDI CHANNEL / 0 OMNI");
        add("noteLow", "MIDI LOW NOTE"); add("noteHigh", "MIDI HIGH NOTE"); add("velocityLow", "MINIMUM VELOCITY");
        add("velocityHigh", "MAXIMUM VELOCITY"); add("velocityDepth", "VELOCITY TO DEPTH");
    }
    if (activeTab == 2)
    {
        add("bands", "CROSSOVER MODE"); add("linked", "LINK BAND CURVES"); add("slope", "CROSSOVER SLOPE");
        add("phaseMode", "CROSSOVER PHASE"); add("crossLow", "LOW CROSSOVER / Hz"); add("crossHigh", "HIGH CROSSOVER / Hz");
        add("lowAmount", "LOW AMOUNT / %"); add("midAmount", "MID AMOUNT / %"); add("highAmount", "HIGH AMOUNT / %");
        add("lowSolo", "SOLO LOW BAND"); add("midSolo", "SOLO MID BAND"); add("highSolo", "SOLO HIGH BAND");
        bandCurve.setVisible(true);
    }
    if (activeTab == 3)
    {
        paintMode.setVisible(true); segmentType.setVisible(true);
        const char *ops[] = {"ramp", "step", "gate", "clear", "left", "right", "invert", "reverse", "quantise", "double", "halve", "random", "reset"};
        const char *labels[] = {"PAINT RAMP", "PAINT STEP", "PAINT GATE", "CLEAR", "ROTATE LEFT", "ROTATE RIGHT", "INVERT", "REVERSE", "QUANTISE", "DOUBLE / REPEAT", "HALVE", "RANDOM SHAPE", "RESET TRANSFORMS"};
        for (int i = 0; i < 13; ++i) { const juce::String op(ops[i]); addTool("tool" + op, labels[i], [this, op] { transformShape(op); }); }
        addTool("duplicateSelected", "DUPLICATE SELECTED", [this] { canvas.duplicateSelected(); });
        traceWav.setVisible(true); traceSc.setVisible(true);
    }
    if (activeTab == 4)
    {
        for (auto *c : std::initializer_list<juce::Component *>{&savePreset, &loadPreset, &saveShape, &loadShape, &favorite, &initButton, &librarySearch, &libraryInfo}) c->setVisible(true);
    }
    if (activeTab == 5)
    {
        if (mode() == 0) { add("character", "DUCKING CHARACTER"); add("output", "OUTPUT GAIN / dB"); }
        if (mode() == 1) { addAdvancedParameter("spectralLow", "SPECTRAL LOW / Hz"); addAdvancedParameter("spectralHigh", "SPECTRAL HIGH / Hz"); addAdvancedParameter("spectralDepth", "SPECTRAL DEPTH"); }
        if (mode() == 2) { for (const auto *id : {"bandLow", "bandMid", "bandHigh", "bandAttack0", "bandAttack1", "bandAttack2", "bandRelease0", "bandRelease1", "bandRelease2", "crossLow", "crossHigh"}) addAdvancedParameter(id, juce::String(id).toUpperCase()); }
        if (mode() == 3) addAdvancedParameter("morph", "MORPH BLEND");
        if (mode() == 4) { addAdvancedParameter("grooveDepth", "PATTERN DEPTH"); addAdvancedParameter("swing", "PATTERN SWING"); for (auto &s : steps) s.setVisible(true); }
        if (mode() == 5) { add("character", "TRANSIENT CHARACTER"); add("detectorAttack", "TRANSIENT ATTACK / ms"); add("detectorRelease", "TRANSIENT RELEASE / ms"); }
    }
    for (int i = 0; i < 6; ++i) advancedTabs[(size_t)i].setToggleState(activeTab == i, juce::dontSendNotification);
    layoutAdvanced(); repaint();
}
void OpenDuckEditor::layoutAdvanced()
{
    const int gap = juce::roundToInt(12 * uiScale), width = juce::jmax(300, advancedViewport.getWidth() - juce::roundToInt(22 * uiScale));
    const int row = juce::roundToInt(57 * uiScale), cw = (width - gap * 4) / 3;
    for (size_t i = 0; i < advancedControls.size(); ++i)
    {
        auto &c = *advancedControls[i]; const int x = gap + (int)(i % 3) * (cw + gap), y = gap + (int)(i / 3) * row;
        c.label.setBounds(x, y, cw, juce::roundToInt(18 * uiScale)); c.label.setFont(premium::font(juce::jmax(10.0f, 13 * uiScale)));
        auto r = juce::Rectangle<int>(x, y + juce::roundToInt(21 * uiScale), cw, juce::roundToInt(27 * uiScale));
        c.slider.setBounds(r); c.choice.setBounds(r); c.toggle.setBounds(r.withWidth(juce::jmin(cw, juce::roundToInt(110 * uiScale))));
        c.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, juce::roundToInt(82 * uiScale), juce::roundToInt(25 * uiScale));
    }
    int y = gap + (int)((advancedControls.size() + 2) / 3) * row;
    const int tw = (width - gap * 5) / 4, th = juce::roundToInt(32 * uiScale);
    for (size_t i = 0; i < toolButtons.size(); ++i) toolButtons[i]->setBounds(gap + (int)(i % 4) * (tw + gap), y + (int)(i / 4) * (th + gap), tw, th);
    y += (int)((toolButtons.size() + 3) / 4) * (th + gap);
    if (activeTab == 2) { bandCurve.setBounds(gap, y, cw, th); y += th + gap; }
    if (activeTab == 3)
    {
        paintMode.setBounds(gap, y, tw, th); segmentType.setBounds(gap * 2 + tw, y, tw, th);
        traceWav.setBounds(gap * 3 + tw * 2, y, tw, th); traceSc.setBounds(gap * 4 + tw * 3, y, tw, th); y += th + gap;
    }
    if (activeTab == 4)
    {
        librarySearch.setBounds(gap, y, width - gap * 2, th); y += th + gap;
        int i = 0; for (auto *b : {&savePreset, &loadPreset, &saveShape, &loadShape, &favorite, &initButton}) { b->setBounds(gap + i % 3 * (cw + gap), y + i / 3 * (th + gap), cw, th); ++i; }
        y += (th + gap) * 2; libraryInfo.setBounds(gap, y, width - gap * 2, juce::roundToInt(55 * uiScale)); libraryInfo.setFont(premium::font(juce::jmax(10.0f, 14 * uiScale), false)); y += juce::roundToInt(65 * uiScale);
    }
    if (activeTab == 5 && mode() == 4)
    {
        for (int i = 0; i < 16; ++i) steps[(size_t)i].setBounds(gap + (i % 8) * (width - gap * 2) / 8, y + (i / 8) * (th + gap), (width - gap * 2) / 8 - gap, th);
        y += (th + gap) * 2;
    }
    advancedContent.setSize(width, juce::jmax(y + gap, advancedViewport.getHeight()));
}
void OpenDuckEditor::setAdvanced(bool expanded)
{
    advancedExpanded = expanded; proc.state.state.setProperty("editorDetails", expanded, nullptr);
    advancedPane.setVisible(expanded); advancedToggle.setToggleState(expanded, juce::dontSendNotification);
    if (expanded) { advancedPane.toFront(false); buildAdvanced(); }
    resized(); repaint();
}
void OpenDuckEditor::transformShape(const juce::String &op)
{
    using O = OpenDuckProcessor::CurveOperation;
    const auto operation = op == "invert" ? O::mirrorVertical : op == "reverse" ? O::mirrorHorizontal : op == "clear" ? O::clear
        : op == "left" ? O::rotateLeft : op == "right" ? O::rotateRight : op == "quantise" ? O::quantise
        : op == "double" ? O::doubleCurve : op == "halve" ? O::halveCurve : op == "random" ? O::randomise
        : op == "ramp" ? O::ramp : op == "step" ? O::step : op == "gate" ? O::gate : O::resetTransforms;
    proc.transformCurve(operation, mode(), bandIndex()); timerCallback();
}
void OpenDuckEditor::importTrace(bool sidechain)
{
    if (sidechain) { auto r = proc.traceSidechain(mode(), bandIndex()); statusMessage = r.wasOk() ? "Sidechain cycle traced" : r.getErrorMessage(); timerCallback(); return; }
    chooser = std::make_unique<juce::FileChooser>("Trace one WAV cycle", juce::File{}, "*.wav"); auto safe = juce::Component::SafePointer<OpenDuckEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser &f)
    {
        if (safe == nullptr || f.getResult() == juce::File{}) return;
        juce::AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f.getResult()));
        if (reader == nullptr) { safe->statusMessage = "WAV could not be read"; return; }
        const int count = (int)juce::jmin(reader->lengthInSamples, (juce::int64)std::ceil(safe->cycleSeconds() * reader->sampleRate));
        juce::AudioBuffer<float> audio((int)juce::jmin(2u, reader->numChannels), juce::jmax(1, count));
        if (!reader->read(&audio, 0, count, 0, true, true)) { safe->statusMessage = "WAV could not be decoded"; return; }
        const auto r = safe->proc.traceAudio(audio, reader->sampleRate, safe->mode(), safe->bandIndex()); safe->statusMessage = r.wasOk() ? "WAV cycle traced" : r.getErrorMessage(); safe->timerCallback();
    });
}

juce::Rectangle<int> OpenDuckEditor::designRect(float x, float y, float w, float h) const
{
    return {juce::roundToInt(x * uiScale), juce::roundToInt(y * uiScale), juce::roundToInt(w * uiScale), juce::roundToInt(h * uiScale)};
}
void OpenDuckEditor::resized()
{
    uiScale = getWidth() / designWidth; look.scale = uiScale; advancedPane.scale = uiScale;
    proc.state.state.setProperty("editorWidth", getWidth(), nullptr);
    for (int i = 0; i < 6; ++i)
    {
        modes[(size_t)i].setBounds(designRect(472.0f + (float)i * 145, 61, 137, 151));
        enabled[(size_t)i].setBounds(designRect(484.0f + (float)i * 145, 70, 57, 21)); solo[(size_t)i].setBounds(designRect(576.0f + (float)i * 145, 70, 21, 20));
    }
    sync.setBounds(designRect(1377, 71, 158, 31)); bpmNumber.setBounds(designRect(1393, 112, 128, 65)); bpmNumber.setFont(premium::font(43 * uiScale));
    previous.setBounds(designRect(66, 266, 46, 37)); behaviorMenu.setBounds(designRect(122, 266, 1113, 37)); next.setBounds(designRect(1248, 266, 46, 37));
    displaySource.setBounds(designRect(719, 315, 210, 26)); freeze.setBounds(designRect(940, 315, 95, 26)); zoom.setBounds(designRect(1085, 315, 89, 26)); advancedToggle.setBounds(designRect(1238, 315, 161, 26));
    upgrade.setBounds(designRect(714, 315, 516, 26));
    canvas.setBounds(designRect(61, 345, 1350, 192));
    band.setBounds(designRect(67, 548, 77, 31)); speed.setBounds(designRect(209, 548, 138, 31)); snap.setBounds(designRect(405, 548, 84, 31)); group.setBounds(designRect(563, 548, 95, 31));
    smoothing.setBounds(designRect(781, 550, 145, 27)); smoothingNumber.setBounds(designRect(929, 548, 81, 31)); smoothingNumber.setFont(premium::font(13 * uiScale, false));
    freeDraw.setBounds(designRect(1023, 548, 111, 31)); undoButton.setBounds(designRect(1143, 548, 73, 31)); redoButton.setBounds(designRect(1225, 548, 73, 31)); removePoint.setBounds(designRect(1308, 548, 91, 31));
    for (int i = 0; i < 16; ++i) shapes[(size_t)i].setBounds(designRect(73.0f + (float)(i % 4) * 73, 653.0f + (float)(i / 4) * 26, 67, 23));
    mirrorH.setBounds(designRect(73, 761, 139, 25)); mirrorV.setBounds(designRect(220, 761, 139, 25)); editPoints.setBounds(designRect(73, 793, 286, 28));
    for (int i = 0; i < 5; ++i) { knobs[(size_t)i].setBounds(designRect(420.0f + (float)i * 187, 650, 137, 120)); values[(size_t)i].setBounds(designRect(413.0f + (float)i * 187, 779, 151, 38)); }
    fine.setBounds(designRect(1273, 625, 43, 22));
    knobs[5].setBounds(designRect(1570, 69, 155, 119)); values[5].setBounds(designRect(1578, 197, 145, 38));
    knobs[6].setBounds(designRect(1450, 302, 151, 142)); values[6].setBounds(designRect(1454, 455, 145, 35));
    aiWindow.setBounds(designRect(1620, 625, 84, 25)); aiAnalyze.setBounds(designRect(1377, 660, 89, 29)); aiCancel.setBounds(designRect(1474, 660, 76, 29)); aiApply.setBounds(designRect(1558, 660, 69, 29)); aiUndo.setBounds(designRect(1635, 660, 69, 29));
    aiStatus.setBounds(designRect(1377, 702, 205, 117)); aiStatus.setFont(premium::font(juce::jmax(10.0f, 14 * uiScale), false)); aiStatus.setJustificationType(juce::Justification::topLeft);
    knobs[7].setBounds(designRect(1598, 719, 101, 68)); values[7].setBounds(designRect(1601, 792, 95, 28));
    for (auto &value : values) value.scale = uiScale;
    advancedPane.setBounds(designRect(68, 310, 1336, 283));
    for (int i = 0; i < 6; ++i) advancedTabs[(size_t)i].setBounds(juce::roundToInt((17 + i * 201) * uiScale), juce::roundToInt(14 * uiScale), juce::roundToInt(193 * uiScale), juce::roundToInt(28 * uiScale));
    advancedClose.setBounds(juce::roundToInt(1265 * uiScale), juce::roundToInt(14 * uiScale), juce::roundToInt(48 * uiScale), juce::roundToInt(28 * uiScale));
    advancedViewport.setBounds(juce::roundToInt(8 * uiScale), juce::roundToInt(49 * uiScale), juce::roundToInt(1320 * uiScale), juce::roundToInt(226 * uiScale)); layoutAdvanced();
    if (resizeGrip) resizeGrip->setBounds(designRect(1725, 851, 22, 22)); canvas.repaint();
}
void OpenDuckEditor::paint(juce::Graphics &graphics)
{
    auto &g = graphics; g.addTransform(juce::AffineTransform::scale(uiScale));
    if (frame.isValid()) g.drawImage(frame, juce::Rectangle<float>(0, 0, designWidth, designHeight)); else g.fillAll(juce::Colour(0xff17232e));
    premium::lcd(g, {1391, 109, 132, 96}, 7);
    g.setFont(premium::font(20)); g.setColour(juce::Colour(0xffe3f0f7));
    g.drawText("MIX", 1570, 34, 155, 29, juce::Justification::centred); g.drawText("BAND FOCUS", 1450, 264, 268, 28, juce::Justification::centred);
    g.drawText("SHAPE", 73, 625, 140, 24, juce::Justification::centredLeft); g.drawText("LOCAL AI DUCKING", 1377, 626, 235, 25, juce::Justification::centredLeft);
    const char *labels[] = {"AMOUNT", "ATTACK", "RELEASE", "TENSION", "PHASE"};
    for (int i = 0; i < 5; ++i)
    {
        g.drawText(labels[i], 409 + i * 187, 622, i == 4 ? 117 : 159, 26, juce::Justification::centred);
        if (i > 0) { g.setColour(juce::Colour(0xff020910).withAlpha(.65f)); g.drawVerticalLine(395 + i * 187, 631, 816); g.setColour(juce::Colour(0xffe3f0f7)); }
    }
    g.setFont(premium::font(13, false)); g.setColour(juce::Colour(0xff9fd4e9)); g.drawText("BPM", 1391, 175, 132, 24, juce::Justification::centred);
    const auto curve = editingCurve(); g.drawText(curve.custom ? "CUSTOM" : juce::String(curve.shape + 1).paddedLeft('0', 2) + " / 16", 285, 626, 73, 23, juce::Justification::centredRight);
    const auto ids = OpenDuckProcessor::sectorPresetIds(mode()); auto found = std::find(ids.begin(), ids.end(), proc.currentBehaviorPresetId(mode()));
    g.drawText((found == ids.end() ? "---" : juce::String((int)std::distance(ids.begin(), found) + 1).paddedLeft('0', 3)) + " / " + juce::String((int)ids.size()), 1303, 270, 100, 27, juce::Justification::centred);
    g.drawText(juce::String(modeNames[mode()]) + " / " + (curve.custom ? "CUSTOM" : juce::String(OpenDuckProcessor::factoryShapeNames()[(size_t)juce::jlimit(0, 15, curve.shape)]).toUpperCase()), 74, 315, 490, 26, juce::Justification::centredLeft);
    g.setFont(premium::font(11, false)); g.setColour(juce::Colour(0xff87aabc)); g.drawText("WAVE AUTO", 574, 315, 89, 26, juce::Justification::centred);
    g.drawText("DISPLAY", 657, 315, 59, 26, juce::Justification::centred); g.drawText("ZOOM", 1040, 315, 42, 26, juce::Justification::centred);
    g.drawText("SPEED", 153, 548, 53, 31, juce::Justification::centred); g.drawText("SNAP", 353, 548, 49, 31, juce::Justification::centred); g.drawText("GROUP", 494, 548, 66, 31, juce::Justification::centred);
    g.drawText("SMOOTHING", 669, 548, 109, 31, juce::Justification::centred);
    if (!curve.edit) { g.setColour(juce::Colour(0xff7196ad)); g.drawText("1 KICK / 1 CYCLE", 1030, 548, 369, 31, juce::Justification::centredRight); }
    const auto bands = (int)sectorValue(proc, "bands", mode());
    g.setColour(juce::Colour(0xff9fc8dd)); g.setFont(premium::font(12, false));
    g.drawText(bands == 0 ? "BAND OFF / FULL RANGE" : bands == 1 ? "TWO BAND CROSSOVER" : "THREE BAND CROSSOVER", 1450, 505, 271, 23, juce::Justification::centred);
    g.drawText("LIVE / INPUT - REDUCTION - OUTPUT", 1450, 548, 271, 22, juce::Justification::centred);
    g.setFont(premium::font(11, false));
    g.drawText(proc.sidechainPresent.load() ? "SIDECHAIN CONNECTED" : "NO SIDECHAIN SIGNAL", 1450, 570, 271, 19, juce::Justification::centred);
    g.drawText("PROPOSAL STRENGTH", 1590, 701, 118, 18, juce::Justification::centred);
    const float gr = -juce::Decibels::gainToDecibels(juce::jmax(.000001f, 1 - proc.reduction.load()), -60.0f);
    const char *meterNames[] = {"IN", "DUCK", "OUT"};
    for (int m = 0; m < 3; ++m)
    {
        auto r = juce::Rectangle<float>(1619.0f + m * 34, 306, 28, 184); premium::lcd(g, r, 4);
        g.setColour(juce::Colour(0xffc6e9f7)); g.setFont(premium::font(10)); g.drawText(meterNames[m], r.withHeight(19), juce::Justification::centred);
        const float peak = m == 0 ? juce::jmax(proc.inputPeaks[0].load(), proc.inputPeaks[1].load()) : juce::jmax(proc.outputPeaks[0].load(), proc.outputPeaks[1].load());
        const float level = juce::jlimit(0.0f, 1.0f, m == 1 ? gr / 60 : (juce::Decibels::gainToDecibels(peak, -60.0f) + 60) / 60);
        for (int step = 0; step < 25; ++step) { g.setColour(level > step / 25.0f ? m == 1 ? juce::Colour(0xffdbb766) : m == 2 ? juce::Colour(0xffd8effa) : premium::cyan() : juce::Colour(0xff16313f)); g.fillRect(r.getX() + 6, r.getBottom() - 21 - step * 5.5f, 16.0f, 3.7f); }
        g.setColour(juce::Colour(0xffb8d7e8)); g.setFont(premium::font(9, false)); g.drawText(m == 1 ? juce::String(gr, 0) : juce::String(juce::Decibels::gainToDecibels(peak, -60.0f), 0), r.withTop(r.getBottom() - 18), juce::Justification::centred);
    }
    if (statusMessage.isNotEmpty()) { g.setColour(juce::Colour(0xffb9d6e6)); g.setFont(premium::font(10, false)); g.drawFittedText(statusMessage, 73, 591, 1324, 15, juce::Justification::centredLeft, 1); }
}
void OpenDuckEditor::updateReadouts()
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const double v = knobs[i].getValue();
        if (i == 1) values[i].set(juce::String(v, 1), "ms", v);
        else if (i == 2) values[i].set(juce::String(v, 0), "ms", v);
        else if (i == 6) values[i].set(v >= 1000 ? juce::String(v / 1000, 1) : juce::String(v, 0), v >= 1000 ? "kHz" : "Hz", v);
        else if (i == 7) values[i].set(juce::String(v * 100, 0), "%", v * 100);
        else values[i].set((i == 3 || i == 4) && v > 0 ? "+" + juce::String(v, i == 4 ? 2 : 0) : juce::String(v, i == 4 ? 2 : 0), "%", v);
    }
}
void OpenDuckEditor::updateScope()
{
    const int currentMode = mode();
    const auto f = proc.getScopeFrame(currentMode);
    scope.generation = f.generation; scope.period = f.periodSeconds; scope.phase = f.phase; scope.completed = f.complete;
    scope.capturedBins = juce::jlimit(0, 512, f.bins);
    const bool validCompleteFrame = f.complete && f.bins > 0 && f.bins <= 512;
    scope.expectedPeriod = proc.cycleSeconds(currentMode);
    scope.periodMatches = odx::view::scopePeriodsMatch(scope.period, scope.expectedPeriod);
    scope.axis = odx::view::scopeAxis(scope.expectedPeriod, scopeOffset, scopeZoom, scope.phase);
    scope.state = scopeFreshness[(size_t)currentMode].observe(f.generation, f.phase, f.periodSeconds,
        validCompleteFrame, scope.periodMatches, proc.isScopeFrozen(), juce::Time::getMillisecondCounterHiRes());
    scope.referencePeak = .000001f;
    for (size_t i = 0; i < (size_t)scope.capturedBins; ++i)
        scope.referencePeak = std::max({scope.referencePeak, std::abs(f.mainMin[i]), std::abs(f.mainMax[i]), std::abs(f.sideMin[i]), std::abs(f.sideMax[i])});
    canvas.getProperties().set("scopeReferencePeak", scope.referencePeak);
    const int source = displaySource.getSelectedId();
    scope.minimum = source == 2 ? f.sideMin : source == 3 ? f.outMin : f.mainMin;
    scope.maximum = source == 2 ? f.sideMax : source == 3 ? f.outMax : f.mainMax;
    auto keyHit = odx::view::isolateSingleHit(f.sideMin, f.sideMax, f.periodSeconds, scope.capturedBins);
    const bool useMainKey = !keyHit.hasAudio && source != 2;
    if (useMainKey) keyHit = odx::view::isolateSingleHit(f.mainMin, f.mainMax, f.periodSeconds, scope.capturedBins);
    scope.hit = keyHit;
    scope.sourceHasAudio = false; for (size_t i = 0; i < (size_t)scope.capturedBins; ++i) if (std::abs(scope.minimum[i]) + std::abs(scope.maximum[i]) > 1e-7f) { scope.sourceHasAudio = true; break; }
    scope.hasAudio = scope.sourceHasAudio && scope.hit.binCount > 0 && validCompleteFrame && scope.periodMatches;
    scope.hitDurationMs = scope.hasAudio ? odx::view::singleHitDurationMs(
        useMainKey ? f.mainMin : f.sideMin, useMainKey ? f.mainMax : f.sideMax, scope.hit, scope.period, scope.capturedBins) : -1;
    if (!scope.hasAudio && scope.state != odx::view::ScopeState::periodMismatch) scope.state = odx::view::ScopeState::waiting;
    canvas.getProperties().set("scopeSingleHitStartBin", scope.hit.startBin);
    canvas.getProperties().set("scopeSingleHitBinCount", scope.hit.binCount);
    canvas.getProperties().set("scopeDetectedOnsets", scope.hit.detectedOnsets);
    canvas.getProperties().set("scopeVisibleOnsets", scope.hasAudio ? 1 : 0);
    auto &metadata = canvas.getProperties();
    metadata.set("scopeMode", currentMode); metadata.set("scopeGeneration", (juce::int64)scope.generation);
    metadata.set("scopePhase", scope.phase); metadata.set("scopeFrameComplete", validCompleteFrame);
    metadata.set("scopeFrameBins", scope.capturedBins);
    metadata.set("scopeCapturedPeriodMs", scope.generation > 0 ? scope.period * 1000 : 0); metadata.set("scopeExpectedPeriodMs", scope.expectedPeriod * 1000);
    metadata.set("scopePeriodMatches", scope.periodMatches); metadata.set("scopeState", odx::view::scopeStateName(scope.state));
    metadata.set("scopePeriodMismatch", scope.state == odx::view::ScopeState::periodMismatch);
    metadata.set("scopeViewStartMs", scope.axis.startMs); metadata.set("scopeViewEndMs", scope.axis.endMs);
    metadata.set("scopeCursorMs", scope.axis.cursorMs); metadata.set("scopeCursorInView", scope.axis.cursorInView);
    metadata.set("scopeHitDurationMs", scope.hitDurationMs); metadata.set("scopeHitThresholdDb", -40.0);
    for (int i = 0; i < 5; ++i) metadata.set("scopeTickMs" + juce::String(i), scope.axis.ticksMs[(size_t)i]);
}
void OpenDuckEditor::queueAssistStrength(bool immediate)
{
    auto &slot = assistUi[(size_t)mode()];
    slot.requestedStrength = juce::jlimit(0.0f, 1.0f, (float)knobs[7].getValue());
    slot.changedAtMs = juce::Time::getMillisecondCounterHiRes() - (immediate ? 180.0 : 0.0);
    slot.pending = slot.hasTarget;
    proc.state.state.setProperty("editorAssistStrength" + juce::String(mode()), slot.requestedStrength, nullptr);
    aiApply.setEnabled(false);
    if (immediate) serviceAssistStrengthRequests();
}
void OpenDuckEditor::serviceAssistStrengthRequests()
{
    using S = odx::ai::State;
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int m = 0; m < 6; ++m)
    {
        auto &slot = assistUi[(size_t)m];
        if (!slot.pending || !slot.hasTarget || now - slot.changedAtMs < 180) continue;
        const auto status = proc.localAssistStatus(m);
        if (status.generation != slot.generation || (status.state != S::ready && status.state != S::analyzing))
        { slot.hasTarget = slot.pending = false; continue; }
        if (status.state == S::analyzing) continue;
        odx::ai::Proposal proposal;
        if (!proc.localAssistProposal(m, proposal) || !proposal.targetImproved)
        { slot.hasTarget = slot.pending = false; continue; }
        if (std::abs(proposal.evaluatedStrength - slot.requestedStrength) <= 1e-5f)
        { slot.pending = false; continue; }
        // A different sector may own the worker. Keep the request queued and
        // retry on the timer rather than treating a busy worker as a failure.
        if (proc.requestLocalAssistStrength(m, slot.requestedStrength)) slot.pending = false;
    }
}
void OpenDuckEditor::updateAssist()
{
    const auto s = proc.localAssistStatus(mode()); using S = odx::ai::State;
    const bool busy = s.state == S::capturing || s.state == S::analyzing;
    auto &slot = assistUi[(size_t)mode()]; odx::ai::Proposal proposal;
    const bool available = s.state == S::ready && proc.localAssistProposal(mode(), proposal);
    if (slot.generation != s.generation)
    { slot.hasTarget = slot.pending = false; slot.generation = s.generation; }
    if (available)
    {
        slot.hasTarget = proposal.targetImproved;
        if (slot.hasTarget && std::abs(proposal.evaluatedStrength - slot.requestedStrength) > 1e-5f && !slot.pending)
        { slot.pending = true; slot.changedAtMs = juce::Time::getMillisecondCounterHiRes() - 180; }
        if (!slot.hasTarget) slot.pending = false;
    }
    else if (s.state != S::analyzing) slot.hasTarget = slot.pending = false;
    const bool matches = available && std::abs(proposal.evaluatedStrength - slot.requestedStrength) <= 1e-5f;
    const bool ready = matches && proposal.improved && !slot.pending && slot.requestedStrength > 0;
    const bool modern = proc.usesRevision3(mode());
    aiAnalyze.setEnabled(modern && !busy && s.modelsAvailable); aiCancel.setEnabled(busy || slot.pending); aiApply.setEnabled(modern && ready); aiUndo.setEnabled(proc.canUndoLocalAssist(mode()));
    knobs[7].setEnabled(modern && slot.hasTarget); values[7].setEnabled(modern && slot.hasTarget); aiWindow.setEnabled(modern && !busy);
    juce::String text;
    if (s.state == S::capturing) text = "CAPTURING " + juce::String(s.capturedSeconds, 1) + " / " + juce::String(s.requestedSeconds, 0) + " s\nPlay audio in the host.";
    else if (s.state == S::analyzing) text = (slot.hasTarget ? "VALIDATING STRENGTH\n" : "ANALYZING / LOCAL\n") + juce::String(juce::jlimit(0, 100, juce::roundToInt(s.analysisProgress * 100))) + "% worker progress";
    else if (s.state == S::ready && slot.pending) text = "STRENGTH QUEUED / " + juce::String(slot.requestedStrength * 100, 0) + "%\nApply waits for the local worker.";
    else if (s.state == S::ready && slot.hasTarget) text = (ready ? "STRENGTH VERIFIED / " : "STRENGTH REJECTED / ") + juce::String(slot.requestedStrength * 100, 0) + "%\n" + juce::String(s.message);
    else if (s.state == S::ready) text = "BASELINE RETAINED\n" + juce::String(s.validHits) + " measured hits\n" + juce::String(s.message);
    else if (s.state == S::idle) text = s.modelsAvailable ? "LOCAL / " + juce::String(modeNames[mode()]) + "\nANALYZE captures 5 / 10 s.\nChanges apply only on APPLY." : "LOCAL MODELS UNAVAILABLE\n" + juce::String(s.message);
    else text = juce::String(odx::ai::stateName(s.state)).toUpperCase() + "\n" + juce::String(s.message);
    if (text != lastAiStatus) { lastAiStatus = text; aiStatus.setText(text, juce::dontSendNotification); }
}
void OpenDuckEditor::timerCallback()
{
    if (auto *peer = getPeer()) if (peer->getCurrentRenderingEngine() != 0) peer->setCurrentRenderingEngine(0);
    if (mode() != selectedMode) { bindModeControls(); resized(); }
    updating = true; const auto c = editingCurve(); const int preset = proc.currentBehaviorPresetId(mode());
    if (bandIndex() >= 0) { const float fields[] = {c.amount, c.attack, c.release, c.tension, c.phase}; for (int i = 0; i < 5; ++i) knobs[(size_t)i].setValue(fields[i], juce::dontSendNotification); }
    const bool presetMatchesSector = preset >= 0 && preset < 700 && OpenDuckProcessor::sectorPresets()[(size_t)preset].mode == mode();
    bool modified = c.custom || c.legacy || !presetMatchesSector;
    if (presetMatchesSector)
    {
        const auto &f = OpenDuckProcessor::sectorPresets()[(size_t)preset];
        const auto different = [](float a, float b) { return std::abs(a - b) > .011f; };
        modified = modified || c.shape != f.shape || different(c.phase, f.phase) || different(c.length, f.length) || different(c.tension, f.tension) || different(c.amount, f.amount) || different(c.attack, f.attack) || different(c.release, f.release);
    }
    if (selectedPreset != preset || lastPresetModified != modified)
    {
        selectedPreset = preset; lastPresetModified = modified;
        if (!modified) behaviorMenu.setSelectedId(preset + 1, juce::dontSendNotification);
        else { behaviorMenu.setSelectedId(0, juce::dontSendNotification); behaviorMenu.setText(juce::String(modeNames[mode()]) + " / " + (c.custom ? "CUSTOM CURVE" : presetMatchesSector ? OpenDuckProcessor::sectorPresets()[(size_t)preset].name + " * MODIFIED" : "MODIFIED"), juce::dontSendNotification); }
    }
    for (int i = 0; i < 6; ++i) modes[(size_t)i].setToggleState(mode() == i, juce::dontSendNotification);
    for (int i = 0; i < 16; ++i) shapes[(size_t)i].setToggleState(!c.custom && c.shape == i, juce::dontSendNotification);
    editPoints.setButtonText(c.edit ? "EDIT POINTS / ON" : "EDIT POINTS / OFF"); editPoints.setToggleState(c.edit, juce::dontSendNotification); freeDraw.setToggleState(c.freeDraw, juce::dontSendNotification);
    for (auto *b : {&freeDraw, &undoButton, &redoButton, &removePoint}) b->setVisible(c.edit);
    undoButton.setEnabled(proc.undo.canUndo()); redoButton.setEnabled(proc.undo.canRedo()); band.setToggleState(sectorValue(proc, "bands", mode()) > .5f, juce::dontSendNotification);
    frozen = proc.isScopeFrozen(); freeze.setToggleState(frozen, juce::dontSendNotification); freeze.setButtonText(frozen ? "FROZEN" : "FREEZE");
    const bool synced = proc.value("sync") >= .5f; bpmNumber.setEditable(false, !synced, false);
    if (!bpmNumber.isBeingEdited()) bpmNumber.setText(juce::String(synced ? proc.bpm.load() : proc.value("freeBpm"), 1), juce::dontSendNotification);
    const bool modern = proc.usesRevision3(mode());
    upgrade.setVisible(!modern); if (!modern) upgrade.toFront(false);
    for (auto *c0 : std::initializer_list<juce::Component *>{&behaviorMenu, &previous, &next, &speed, &snap, &group, &smoothing, &band, &knobs[6], &values[6]}) c0->setEnabled(modern);
    for (auto *c0 : std::initializer_list<juce::Component *>{&displaySource, &freeze, &zoom}) c0->setVisible(modern);
    for (auto &c0 : advancedControls) if (c0->id.startsWith("v3")) { c0->slider.setEnabled(modern); c0->choice.setEnabled(modern); c0->toggle.setEnabled(modern); }
    for (auto &b0 : toolButtons) b0->setEnabled(modern);
    for (auto *b0 : {&mirrorH, &mirrorV, &traceWav, &traceSc, &initButton}) b0->setEnabled(modern);
    const int bandsSetting = (int)sectorValue(proc, "bands", mode());
    const bool implicitFocus = bandsSetting == 0 && proc.getCurveSnapshot(mode()).band;
    const int effectiveBands = bandsSetting > 0 ? bandsSetting : implicitFocus ? 1 : mode() == 2 ? 2 : 0;
    const bool independentBands = modern && effectiveBands > 0 && sectorValue(proc, "linked", mode()) < .5f;
    const int previousBandId = bandCurve.getSelectedId();
    if ((bandCurve.indexOfItemId(3) >= 0) != (effectiveBands == 2))
    {
        bandCurve.clear(juce::dontSendNotification); bandCurve.addItem("LINKED / MAIN", 1); bandCurve.addItem("LOW CURVE", 2);
        if (effectiveBands == 2) bandCurve.addItem("MID CURVE", 3);
        bandCurve.addItem("HIGH CURVE", 4); bandCurve.setSelectedId(previousBandId == 3 && effectiveBands != 2 ? 1 : previousBandId, juce::dontSendNotification);
    }
    bandCurve.setEnabled(independentBands); bandCurve.setItemEnabled(2, independentBands); bandCurve.setItemEnabled(3, independentBands && effectiveBands == 2); bandCurve.setItemEnabled(4, independentBands && !implicitFocus);
    for (auto &control : advancedControls)
        if (control->id == sector("midAmount") || control->id == sector("midSolo") || control->id == sector("crossHigh"))
        {
            const bool available = modern && effectiveBands == 2;
            control->slider.setEnabled(available); control->choice.setEnabled(available); control->toggle.setEnabled(available); control->label.setAlpha(available ? 1.0f : .4f);
        }
    if (previousBandId != bandCurve.getSelectedId() || (bandIndex() >= 0 && !bandCurve.isItemEnabled(bandCurve.getSelectedId())))
    {
        bandCurve.setSelectedId(1, juce::dontSendNotification); proc.state.state.setProperty("editorBandCurve" + juce::String(mode()), 1, nullptr); updating = false; bindModeControls(true); updating = true;
    }
    favorite.setEnabled(presetMatchesSector && !c.custom);
    updating = false; updateReadouts(); updateScope(); serviceAssistStrengthRequests(); updateAssist(); canvas.repaint(); repaint();
}
void OpenDuckEditor::parentHierarchyChanged() { if (auto *p = getPeer()) p->setCurrentRenderingEngine(0); }
void OpenDuckEditor::ResizeGrip::paint(juce::Graphics &g)
{
    const auto r = getLocalBounds().toFloat().reduced(2);
    for (int i = 0; i < 3; ++i) { const float d = 4.0f + i * 4.0f; g.setColour(juce::Colour(0xff07131d)); g.drawLine(r.getRight() - d, r.getBottom(), r.getRight(), r.getBottom() - d, 2); g.setColour(juce::Colour(0xffcce5ef)); g.drawLine(r.getRight() - d + 1, r.getBottom(), r.getRight(), r.getBottom() - d + 1, .7f); }
}

OpenDuckEditor::CurveCanvas::CurveCanvas(OpenDuckEditor &editor) : owner(editor)
{
    setName("curveCanvas"); setWantsKeyboardFocus(true); setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}
juce::Rectangle<float> OpenDuckEditor::CurveCanvas::plot() const
{
    const float s = owner.uiScale;
    return getLocalBounds().toFloat().withTrimmedLeft(48 * s).withTrimmedRight(14 * s).withTrimmedTop(24 * s).withTrimmedBottom(juce::jmax(31.0f, 42 * s));
}
juce::Point<float> OpenDuckEditor::CurveCanvas::toCurve(juce::Point<float> p, const OpenDuckProcessor::CurveSnapshot &c, bool useSnap) const
{
    const auto r = plot(); float t = owner.scopeOffset + juce::jlimit(0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()) / owner.scopeZoom;
    if (useSnap && sectorValue(owner.proc, "snapEnabled", owner.mode()) > .5f)
    {
        const float divisions = (float)(1 << juce::jlimit(0, 6, (int)sectorValue(owner.proc, "snap", owner.mode()))); t = std::round(t * divisions) / divisions;
    }
    const float gain = juce::jlimit(0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight());
    if (!c.modern3) return {t == 1 && c.phase == 0 ? 1 : wrap(t - c.phase / 100), juce::jlimit(0.0f, 1.0f, 1 - (1 - gain) / juce::jmax(.0001f, c.amount / 100))};
    return {owner.proc.previewCycleToRaw(owner.mode(), t, owner.bandIndex()), owner.proc.previewGainToCurveEnvelope(owner.mode(), t, gain, owner.bandIndex())};
}
juce::Point<float> OpenDuckEditor::CurveCanvas::toScreen(odx::CurvePoint p, const OpenDuckProcessor::CurveSnapshot &c) const
{
    const auto r = plot(); const float t = c.modern3 ? owner.proc.rawToPreviewCycle(owner.mode(), p.x, owner.bandIndex()) : p.x == 1 && c.phase == 0 ? 1 : wrap(p.x + c.phase / 100);
    const float gain = c.modern3 ? owner.proc.previewCurveGain(owner.mode(), t, owner.bandIndex()) : OpenDuckProcessor::effectiveCurveSample(c, t, owner.cycleSeconds(), owner.mode(), sectorValue(owner.proc, "character", owner.mode()));
    return {r.getX() + (t - owner.scopeOffset) * owner.scopeZoom * r.getWidth(), r.getBottom() - gain * r.getHeight()};
}
void OpenDuckEditor::CurveCanvas::prepareCustom(OpenDuckProcessor::CurveSnapshot &c, bool dense)
{
    if (c.custom && !dense && c.count >= 2) return;
    auto source = c; source.phase = 0; source.amount = 100;
    if (source.modern3) { source.length = 1; source.riseSlope = 1; }
    constexpr float anchors[] = {0, .08f, .14f, .24f, .38f, .57f, .78f, 1}; c.count = dense ? 193 : 8;
    for (int i = 0; i < c.count; ++i) { const float x = dense ? i / 192.0f : anchors[i]; c.points[(size_t)i] = {x, OpenDuckProcessor::sampleCurve(source, x), dense ? 5 : 0}; }
    c.custom = true; c.shape = 16; c.linear = dense; if (!c.modern3) c.length = 1; c.tension = 0; c.presetId = -1; c.legacy = false;
}
void OpenDuckEditor::CurveCanvas::commit(const OpenDuckProcessor::CurveSnapshot &c) { owner.setEditingCurve(c); repaint(); owner.updateReadouts(); }
void OpenDuckEditor::CurveCanvas::paint(juce::Graphics &g)
{
    const float s = owner.uiScale; const auto bounds = getLocalBounds().toFloat(), r = plot(); premium::lcd(g, bounds.reduced(.5f), 9 * s);
    const auto c = owner.editingCurve(); const auto colour = premium::modeColour(owner.mode());
    const float domain = 1 / owner.scopeZoom, offset = owner.scopeOffset;
    const auto xAt = [=](float t) { return r.getX() + (t - offset) * owner.scopeZoom * r.getWidth(); };
    g.saveState(); g.reduceClipRegion(r.toNearestInt());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff071d29), r.getTopLeft(), juce::Colour(0xff02090f), r.getBottomRight(), false)); g.fillRect(r);
    const int divisions = 1 << juce::jlimit(0, 6, (int)sectorValue(owner.proc, "snap", owner.mode()));
    for (int i = 0; i <= divisions; ++i) { const float x = xAt((float)i / divisions); g.setColour(juce::Colour(0xff285367).withAlpha(.66f)); g.drawLine(x, r.getY(), x, r.getBottom(), juce::jmax(.5f, .65f * s)); }
    for (int i = 0; i <= 4; ++i) { const float y = r.getY() + r.getHeight() * i / 4; g.setColour(juce::Colour(0xff235066).withAlpha(.65f)); g.drawLine(r.getX(), y, r.getRight(), y, juce::jmax(.5f, .65f * s)); }
    juce::Path curve, fill; const float peak = owner.scope.referencePeak;
    if (owner.scope.hasAudio)
    {
        g.setColour(juce::Colour(0xffdceff9).withAlpha(.70f));
        for (int i = 0; i < owner.scope.capturedBins; ++i)
        {
            if (!owner.scope.hit.contains(i)) continue;
            const float t = (i + .5f) / 512, x = xAt(t);
            const float y1 = r.getCentreY() - owner.scope.maximum[(size_t)i] / peak * r.getHeight() * .43f, y2 = r.getCentreY() - owner.scope.minimum[(size_t)i] / peak * r.getHeight() * .43f;
            g.drawLine(x, y1, x, y2, juce::jmax(.6f, .8f * s));
        }
    }
    for (int i = 0; i <= 800; ++i)
    {
        const float t = offset + (float)i / 800 * domain, x = xAt(t), gain = owner.proc.previewCurveGain(owner.mode(), t, owner.bandIndex()), y = r.getBottom() - gain * r.getHeight();
        if (i == 0) { curve.startNewSubPath(x, y); fill.startNewSubPath(x, y); } else { curve.lineTo(x, y); fill.lineTo(x, y); }
    }
    fill.lineTo(r.getBottomRight()); fill.lineTo(r.getBottomLeft()); fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(.12f), r.getTopLeft(), colour.withAlpha(.01f), r.getBottomLeft(), false)); g.fillPath(fill);
    const float mix = owner.proc.value("mix");
    if (mix < 99.99f)
    {
        const float y = r.getBottom() - (1 - mix / 100) * r.getHeight(); const float dash[] = {5 * s, 4 * s};
        g.setColour(juce::Colour(0xffbdaa79)); g.drawDashedLine({r.getX(), y, r.getRight(), y}, dash, 2, .8f * s); g.setFont(premium::font(10 * s, false));
        g.drawText("MIX DRY FLOOR " + juce::String(100 - mix, 0) + "%", r.withY(y - 15 * s).withHeight(14 * s).reduced(6 * s, 0), juce::Justification::centredRight);
    }
    if (owner.scope.hasAudio)
    {
        const auto cursorColour = owner.scope.state == odx::view::ScopeState::live ? juce::Colour(0xffe5f6ff) : juce::Colour(0xffdfbc77);
        const float x = xAt(owner.scope.phase);
        g.setColour(cursorColour.withAlpha(.15f));
        if (owner.scope.axis.cursorInView)
        {
            g.drawLine(x, r.getY(), x, r.getBottom(), juce::jmax(3.f, 4 * s));
            g.setColour(cursorColour); g.drawLine(x, r.getY(), x, r.getBottom(), juce::jmax(1.3f, 1.6f * s));
            juce::Path marker; marker.addTriangle(x - 4, r.getY(), x + 4, r.getY(), x, r.getY() + 6); g.fillPath(marker);
        }
        else
        {
            // An off-screen cursor is not clamped into a fabricated position.
            const bool left = owner.scope.axis.cursorPosition < 0;
            const float edge = left ? r.getX() + 2 : r.getRight() - 2;
            const float direction = left ? 1.f : -1.f;
            g.setColour(cursorColour); g.drawLine(edge + direction * 6, r.getY() + 4, edge, r.getY() + 9, 1.5f);
            g.drawLine(edge, r.getY() + 9, edge + direction * 6, r.getY() + 14, 1.5f);
        }
    }
    g.setColour(colour.withAlpha(.10f)); g.strokePath(curve, juce::PathStrokeType(7 * s)); g.setColour(colour); g.strokePath(curve, juce::PathStrokeType(2 * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (dragging == Drag::selection) { g.setColour(colour.withAlpha(.12f)); g.fillRect(juce::Rectangle<float>(start, lastDraw)); g.setColour(colour); g.drawRect(juce::Rectangle<float>(start, lastDraw), .8f); }
    g.restoreState();
    const auto effective = owner.proc.getEffectiveCurveSnapshot(owner.mode(), owner.bandIndex());
    const float inverseSlope = 1 / juce::jmax(.25f, effective.riseSlope);
    const float a = owner.proc.rawToPreviewCycle(owner.mode(), std::pow(.14f, inverseSlope), owner.bandIndex()), rel = owner.proc.rawToPreviewCycle(owner.mode(), std::pow(.57f, inverseSlope), owner.bandIndex());
    const float times[] = {a, rel}; const char *labels[] = {"ATTACK", "RELEASE"};
    for (int i = 0; c.modern3 && i < 2; ++i)
    {
        const float x = xAt(times[i]); if (x < r.getX() || x > r.getRight()) continue;
        const float dash[] = {3 * s, 4 * s}; g.setColour(i == 0 ? juce::Colour(0xff53b9df) : juce::Colour(0xff6be9cc)); g.drawDashedLine({x, r.getY(), x, r.getBottom()}, dash, 2, .8f * s);
        g.fillEllipse(x - 4 * s, r.getY() - 4 * s, 8 * s, 8 * s); g.setFont(premium::font(10 * s, false));
        const float lx = juce::jlimit(r.getX(), r.getRight() - 112 * s, x + 8 * s); g.drawText(juce::String(labels[i]) + " " + juce::String(i == 0 ? c.attack : c.release, i == 0 ? 1 : 0) + " ms", juce::Rectangle<float>{lx, bounds.getY() + 2 * s, 112 * s, 17 * s}, juce::Justification::centredLeft);
    }
    if (c.edit && !c.freeDraw)
    {
        auto handles = c; prepareCustom(handles, false); const int stride = juce::jmax(1, handles.count / 40);
        for (int i = 0; i < handles.count; ++i)
        {
            if (i % stride != 0 && i != handles.count - 1 && i != selectedPoint) continue;
            const auto p = toScreen(handles.points[(size_t)i], handles); if (!r.expanded(1).contains(p)) continue;
            const float radius = (i == selectedPoint || std::find(selection.begin(), selection.end(), i) != selection.end() ? 5.0f : 3.5f) * s;
            g.setColour(juce::Colour(0xff03101a)); g.fillEllipse(p.x - radius, p.y - radius, radius * 2, radius * 2); g.setColour(colour); g.drawEllipse(p.x - radius, p.y - radius, radius * 2, radius * 2, 1.4f * s);
        }
    }
    g.setFont(premium::font(juce::jmax(8.5f, 11 * s), false)); g.setColour(juce::Colour(0xffa0c9df));
    g.drawText("100%", juce::Rectangle<float>{2 * s, r.getY() - 6 * s, 41 * s, 14 * s}, juce::Justification::centredRight); g.drawText("0%", juce::Rectangle<float>{6 * s, r.getBottom() - 8 * s, 37 * s, 14 * s}, juce::Justification::centredRight);
    const float tickHeight = juce::jmax(11.f, 14 * s), noteHeight = juce::jmax(12.f, 15 * s);
    for (int i = 0; i < 5; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / 4;
        g.drawLine(x, r.getBottom(), x, r.getBottom() + 3, .8f);
        const float width = juce::jmin(100.f, r.getWidth() / 5);
        const float labelX = i == 0 ? x : i == 4 ? x - width : x - width / 2;
        g.drawText(juce::String(owner.scope.axis.ticksMs[(size_t)i], 1) + " ms",
            juce::Rectangle<float>{labelX, r.getBottom() + 2, width, tickHeight},
            i == 0 ? juce::Justification::centredLeft : i == 4 ? juce::Justification::centredRight : juce::Justification::centred);
    }
    const auto &axis = owner.scope.axis;
    const auto viewText = " / VIEW " + juce::String(axis.startMs, 1) + "-" + juce::String(axis.endMs, 1)
        + " ms / CYCLE " + juce::String(axis.periodMs, 1) + " ms / " + juce::String(owner.scopeZoom, 0) + "x";
    juce::String note;
    if (owner.scope.state == odx::view::ScopeState::periodMismatch)
        note = "WAITING / HELD FRAME " + juce::String(owner.scope.period * 1000, 1) + " ms / NEW PERIOD CAPTURE REQUIRED" + viewText;
    else if (owner.scope.hasAudio)
        note = juce::String(odx::view::scopeStateName(owner.scope.state)) + " / SINGLE KICK + TARGET" + viewText
            + " / CURSOR " + juce::String(axis.cursorMs, 1) + " ms" + (axis.cursorInView ? "" : " (OUTSIDE VIEW)")
            + (owner.scope.capturedBins < 512 ? " / CAPTURE 0-" + juce::String(owner.scope.capturedBins / 512.0 * owner.scope.period * 1000, 1) + " ms" : "")
            + (owner.scope.hitDurationMs >= 0 ? " / HIT ~" + juce::String(owner.scope.hitDurationMs, 1) + " ms @ -40dB" : " / HIT -- @ -40dB");
    else
        note = juce::String("WAITING / ") + (!owner.scope.completed ? "COMPLETE CYCLE" : owner.scope.sourceHasAudio ? "NO ISOLATED KICK" : "NO AUDIO") + viewText;
    getProperties().set("scopeNote", note);
    getProperties().set("scopeNoteFits", juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), note) <= r.getWidth());
    g.setColour(owner.scope.state == odx::view::ScopeState::live ? juce::Colour(0xffb9dfed) : juce::Colour(0xffd2bd8d));
    g.drawText(note, juce::Rectangle<float>{r.getX(), bounds.getBottom() - noteHeight - 1, r.getWidth(), noteHeight}, juce::Justification::centred);
}
void OpenDuckEditor::CurveCanvas::mouseDown(const juce::MouseEvent &e)
{
    if (!e.mods.isLeftButtonDown() || !plot().expanded(5 * owner.uiScale).contains(e.position)) return;
    grabKeyboardFocus(); draggingMode = owner.mode(); initial = owner.editingCurve(); working = initial; start = e.position;
    const auto r = plot(); const auto effective = owner.proc.getEffectiveCurveSnapshot(owner.mode(), owner.bandIndex());
    const float inverseSlope = 1 / juce::jmax(.25f, effective.riseSlope);
    const float ax = r.getX() + (owner.proc.rawToPreviewCycle(owner.mode(), std::pow(.14f, inverseSlope), owner.bandIndex()) - owner.scopeOffset) * owner.scopeZoom * r.getWidth();
    const float rx = r.getX() + (owner.proc.rawToPreviewCycle(owner.mode(), std::pow(.57f, inverseSlope), owner.bandIndex()) - owner.scopeOffset) * owner.scopeZoom * r.getWidth();
    owner.proc.undo.beginNewTransaction(initial.edit ? "Edit ducking curve" : "Move curve phase");
    if (e.mods.isCtrlDown() && r.contains(e.position))
    {
        dragging = Drag::bend;
        if (!initial.custom && owner.bandIndex() < 0)
            gestureParameter = owner.proc.state.getParameter(OpenDuckProcessor::curveParameterId("tension", draggingMode));
        if (gestureParameter) gestureParameter->beginChangeGesture();
        return;
    }
    if (initial.modern3 && e.position.y < r.getY() + 12 * owner.uiScale && (std::abs(e.position.x - ax) < 11 * owner.uiScale || std::abs(e.position.x - rx) < 11 * owner.uiScale))
    {
        dragging = std::abs(e.position.x - ax) < std::abs(e.position.x - rx) ? Drag::attack : Drag::release;
        if (owner.bandIndex() < 0) gestureParameter = owner.proc.state.getParameter(OpenDuckProcessor::curveParameterId(dragging == Drag::attack ? "attack" : "release", draggingMode));
        if (gestureParameter) gestureParameter->beginChangeGesture(); return;
    }
    if (!initial.edit)
    {
        dragging = Drag::phase; if (owner.bandIndex() < 0) gestureParameter = owner.proc.state.getParameter(OpenDuckProcessor::curveParameterId("phase", draggingMode));
        if (gestureParameter) gestureParameter->beginChangeGesture(); return;
    }
    if (initial.freeDraw)
    {
        prepareCustom(working, true); initial = working; dragging = Drag::draw; lastDraw = toCurve(e.position, working, !e.mods.isAltDown()); drawSegment(lastDraw, lastDraw); commit(working); return;
    }
    prepareCustom(working, false); float distance = 15 * owner.uiScale; int nearest = -1;
    for (int i = 0; i < working.count; ++i) { const float d = toScreen(working.points[(size_t)i], working).getDistanceFrom(e.position); if (d < distance) { nearest = i; distance = d; } }
    if (nearest >= 0)
    {
        selectedPoint = nearest;
        if (e.mods.isShiftDown()) { const auto found = std::find(selection.begin(), selection.end(), nearest); if (found == selection.end()) selection.push_back(nearest); else selection.erase(found); }
        else if (std::find(selection.begin(), selection.end(), nearest) == selection.end()) selection = {nearest};
        initial = working; dragging = Drag::point;
    }
    else { selectedPoint = -1; if (!e.mods.isShiftDown()) selection.clear(); if (e.mods.isShiftDown()) { dragging = Drag::selection; lastDraw = start; } }
    repaint();
}
bool OpenDuckEditor::CurveCanvas::applyCurvature(float centre, float amount)
{
    working = initial;
    if (!initial.custom)
    {
        working.tension = juce::jlimit(-100.f, 100.f, initial.tension + amount * (initial.modern3 ? 100.f : -100.f));
        return working.tension != initial.tension;
    }
    // The legacy decoder has no per-segment curvature. Its explicit upgrade
    // control remains the boundary; this gesture never rewrites its anchors.
    if (!initial.modern3 || initial.count < 2) return false;
    int segment = 0;
    while (segment + 1 < initial.count - 1 && centre > initial.points[(size_t)segment + 1].x) ++segment;
    const auto &a = initial.points[(size_t)segment], &b = initial.points[(size_t)segment + 1];
    if (std::abs(b.y - a.y) < .000001f) return false; // A flat span needs explicit anchors to form a bump.
    const float direction = (b.y > a.y ? 1.f : -1.f) * (a.reversed ? -1.f : 1.f);
    working.points[(size_t)segment].curvature = juce::jlimit(-1.f, 1.f, a.curvature + amount * 2 * direction);
    return working.points[(size_t)segment].curvature != a.curvature;
}
void OpenDuckEditor::CurveCanvas::drawSegment(juce::Point<float> a, juce::Point<float> b)
{
    if (std::abs(a.x - b.x) > .5f) return;
    int nearest = 0; const int paint = owner.paintMode.getSelectedId();
    for (int i = 0; i < working.count; ++i)
    {
        auto &p = working.points[(size_t)i]; if (std::abs(p.x - b.x) < std::abs(working.points[(size_t)nearest].x - b.x)) nearest = i;
        if (p.x >= juce::jmin(a.x, b.x) && p.x <= juce::jmax(a.x, b.x))
        {
            const float linear = a.y + (b.y - a.y) * (p.x - a.x) / (std::abs(b.x - a.x) < .00001f ? .00001f : b.x - a.x);
            p.y = juce::jlimit(0.0f, 1.0f, paint == 3 ? b.y : paint == 4 ? (b.y >= .5f ? 1.0f : 0.0f) : linear); p.segment = 5;
        }
    }
    working.points[(size_t)nearest].y = paint == 4 ? (b.y >= .5f ? 1.0f : 0.0f) : b.y;
}
void OpenDuckEditor::CurveCanvas::mouseDrag(const juce::MouseEvent &e)
{
    if (dragging == Drag::none) return;
    if (dragging == Drag::selection) { lastDraw = e.position; repaint(); return; }
    if (dragging == Drag::bend)
    {
        const float amount = (start.y - e.position.y) / plot().getHeight() * (e.mods.isShiftDown() ? .2f : 1.f);
        const auto previousCurve = working;
        bool changed = applyCurvature(toCurve(start, initial, false).x, amount) || previousCurve.tension != working.tension;
        for (int i = 0; i < working.count; ++i) changed |= previousCurve.points[(size_t)i].curvature != working.points[(size_t)i].curvature;
        if (changed) commit(working);
        return;
    }
    if (dragging == Drag::phase || dragging == Drag::attack || dragging == Drag::release)
    {
        const float delta = (e.position.x - start.x) / plot().getWidth() / owner.scopeZoom;
        const bool precise = owner.phaseFine || e.mods.isShiftDown();
        float value = 0;
        if (dragging == Drag::phase) { value = (wrap((initial.phase + delta * (precise ? 2.5f : 25.0f)) / 100 + .5f) - .5f) * 100; working.phase = value; }
        else if (dragging == Drag::attack) { value = juce::jlimit(0.0f, 200.0f, initial.attack + delta * (float)owner.cycleSeconds() * 1000 * (e.mods.isShiftDown() ? .1f : 1.0f)); working.attack = value; }
        else { value = juce::jlimit(5.0f, 1000.0f, initial.release + delta * (float)owner.cycleSeconds() * 1000 * (e.mods.isShiftDown() ? .1f : 1.0f)); working.release = value; }
        if (gestureParameter) gestureParameter->setValueNotifyingHost(gestureParameter->convertTo0to1(value)); else commit(working);
        owner.updateReadouts(); repaint(); return;
    }
    const auto p = toCurve(e.position, working, !e.mods.isAltDown());
    if (dragging == Drag::point && selectedPoint >= 0)
    {
        const auto origin = toCurve(start, initial, false); const float dx = p.x - origin.x, dy = p.y - origin.y; working = initial;
        for (const int index : selection)
        {
            if (index < 0 || index >= working.count) continue; auto &point = working.points[(size_t)index]; point.y = juce::jlimit(0.0f, 1.0f, initial.points[(size_t)index].y + dy);
            if (index > 0 && index < working.count - 1) point.x = juce::jlimit(initial.points[(size_t)index - 1].x + .0001f, initial.points[(size_t)index + 1].x - .0001f, initial.points[(size_t)index].x + dx);
        }
    }
    else if (dragging == Drag::draw)
    {
        if (owner.paintMode.getSelectedId() == 2) { working = initial; drawSegment(toCurve(start, initial, !e.mods.isAltDown()), p); }
        else drawSegment(lastDraw, p);
        lastDraw = p;
    }
    commit(working);
}
void OpenDuckEditor::CurveCanvas::mouseUp(const juce::MouseEvent &)
{
    if (dragging == Drag::selection)
    {
        auto handles = owner.editingCurve(); prepareCustom(handles, false); const juce::Rectangle<float> box(start, lastDraw);
        for (int i = 0; i < handles.count; ++i) if (box.contains(toScreen(handles.points[(size_t)i], handles)) && std::find(selection.begin(), selection.end(), i) == selection.end()) selection.push_back(i);
    }
    if (gestureParameter) { gestureParameter->endChangeGesture(); gestureParameter = nullptr; }
    dragging = Drag::none; owner.timerCallback();
}
void OpenDuckEditor::CurveCanvas::mouseDoubleClick(const juce::MouseEvent &e)
{
    auto c = owner.editingCurve(); if (!c.edit || c.freeDraw || e.mods.isCtrlDown() || !plot().contains(e.position)) return;
    prepareCustom(c, false); if (c.count >= 256) return; const auto p = toCurve(e.position, c, !e.mods.isAltDown()); int found = -1;
    for (int i = 0; i < c.count; ++i) if (std::abs(c.points[(size_t)i].x - p.x) < .003f) found = i;
    if (found >= 0) c.points[(size_t)found].y = p.y;
    else { c.points[(size_t)c.count++] = {p.x, p.y}; std::sort(c.points.begin(), c.points.begin() + c.count, [](auto a, auto b) { return a.x < b.x; }); }
    owner.setEditingCurve(c, "Add curve point"); owner.timerCallback();
}
void OpenDuckEditor::CurveCanvas::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel)
{
    if (owner.scopeZoom > 1 && e.mods.isAltDown()) { owner.scopeOffset = juce::jlimit(0.0f, 1 - 1 / owner.scopeZoom, owner.scopeOffset - wheel.deltaY * .15f / owner.scopeZoom); owner.updateScope(); repaint(); return; }
    if (!e.mods.isCtrlDown() || wheel.deltaY == 0 || !plot().contains(e.position)) return;
    draggingMode = owner.mode(); initial = owner.editingCurve();
    const float amount = (wheel.deltaY > 0 ? .04f : -.04f) * (e.mods.isShiftDown() ? .2f : 1.f);
    if (!applyCurvature(toCurve(e.position, initial, false).x, amount)) return;
    auto *parameter = !initial.custom && owner.bandIndex() < 0
        ? owner.proc.state.getParameter(OpenDuckProcessor::curveParameterId("tension", draggingMode)) : nullptr;
    if (parameter) parameter->beginChangeGesture();
    owner.setEditingCurve(working, "Change curve curvature");
    if (parameter) parameter->endChangeGesture();
    owner.timerCallback();
}
void OpenDuckEditor::CurveCanvas::setSegmentType(int type)
{
    auto c = owner.editingCurve(); if (!c.edit) return; prepareCustom(c, false); c.linear = false;
    for (int i = 0; i < c.count; ++i) if (selection.empty() || std::find(selection.begin(), selection.end(), i) != selection.end()) c.points[(size_t)i].segment = juce::jlimit(0, 5, type);
    owner.setEditingCurve(c, "Change curve segment"); owner.timerCallback();
}
void OpenDuckEditor::CurveCanvas::duplicateSelected()
{
    auto c = owner.editingCurve(); if (!c.edit || !c.custom || selection.empty()) return;
    for (const int i : selection) if (i > 0 && i < c.count - 1 && c.count < 256) { auto p = c.points[(size_t)i]; p.x = juce::jlimit(.0001f, .9999f, p.x + .025f); c.points[(size_t)c.count++] = p; }
    std::sort(c.points.begin(), c.points.begin() + c.count, [](auto a, auto b) { return a.x < b.x; }); selection.clear(); owner.setEditingCurve(c, "Duplicate curve points"); owner.timerCallback();
}
bool OpenDuckEditor::CurveCanvas::keyPressed(const juce::KeyPress &key)
{
    if (key.getModifiers().isCtrlDown() && (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A')) { auto c = owner.editingCurve(); if (!c.edit) return false; prepareCustom(c, false); selection.clear(); for (int i = 0; i < c.count; ++i) selection.push_back(i); repaint(); return true; }
    if (key.getModifiers().isCtrlDown() && (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D')) { duplicateSelected(); return true; }
    if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey) return false;
    auto c = owner.editingCurve(); if (!c.edit || !c.custom) return false;
    if (selection.empty() && selectedPoint > 0) selection.push_back(selectedPoint);
    std::sort(selection.rbegin(), selection.rend()); bool changed = false;
    for (const int i : selection) if (i > 0 && i < c.count - 1) { for (int j = i; j < c.count - 1; ++j) c.points[(size_t)j] = c.points[(size_t)j + 1]; --c.count; changed = true; }
    selection.clear(); selectedPoint = -1; if (changed) { owner.setEditingCurve(c, "Remove curve points"); owner.timerCallback(); } return changed;
}
