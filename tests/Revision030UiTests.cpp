#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <set>

namespace
{
int passed = 0, failed = 0;
void check(bool ok, const char *message)
{
    std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
    ok ? ++passed : ++failed;
}
juce::Component *find(juce::Component &root, const juce::String &name)
{
    if (root.getName() == name) return &root;
    for (auto *c : root.getChildren()) if (auto *found = find(*c, name)) return found;
    return nullptr;
}
bool click(juce::Component &root, const juce::String &name)
{
    auto *b = dynamic_cast<juce::Button *>(find(root, name));
    if (b == nullptr || !b->isEnabled() || !b->onClick) return false;
    b->onClick(); return true;
}
void set(OpenDuckProcessor &p, const juce::String &id, float value)
{
    if (auto *parameter = p.state.getParameter(id)) parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
juce::MouseEvent mouse(juce::Component &c, juce::Point<float> point, int modifiers,
                       juce::Point<float> start, bool dragged = false, int clicks = 1)
{
    const auto time = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys(modifiers),
            1.f, 0.f, 0.f, 0.f, 0.f, &c, &c, time, start, time, clicks, dragged};
}
bool type(juce::Component &root, const juce::String &name, const juce::String &value)
{
    auto *c = find(root, name); if (c == nullptr) return false;
    auto point = c->getLocalBounds().toFloat().getCentre(); c->mouseDoubleClick(mouse(*c, point, 0, point, false, 2));
    for (auto *child : c->getChildren()) if (auto *editor = dynamic_cast<juce::TextEditor *>(child))
    {
        if (!editor->isVisible() || !editor->onReturnKey) return false;
        editor->setText(value, false); editor->onReturnKey(); return true;
    }
    return false;
}
bool same(const OpenDuckProcessor::CurveSnapshot &a, const OpenDuckProcessor::CurveSnapshot &b)
{
    if (a.count != b.count || a.shape != b.shape || a.presetId != b.presetId || a.custom != b.custom || a.edit != b.edit || a.freeDraw != b.freeDraw || a.modern3 != b.modern3) return false;
    for (auto pair : {std::pair<float,float>{a.amount,b.amount},{a.phase,b.phase},{a.attack,b.attack},{a.release,b.release},{a.tension,b.tension},{a.length,b.length}}) if (std::abs(pair.first - pair.second) > 1e-5f) return false;
    for (int i = 0; i < a.count; ++i) if (a.points[(size_t)i].x != b.points[(size_t)i].x || a.points[(size_t)i].y != b.points[(size_t)i].y || a.points[(size_t)i].segment != b.points[(size_t)i].segment) return false;
    return true;
}
void screenshot(juce::AudioProcessorEditor &editor, const juce::File &folder, const juce::String &name)
{
    auto image = editor.createComponentSnapshot(editor.getLocalBounds()); auto target = folder.getChildFile(name);
    juce::TemporaryFile temporary(target); auto stream = temporary.getFile().createOutputStream();
    juce::PNGImageFormat png; bool written = false;
    if (stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk())
    {
        written = png.writeImageToStream(image, *stream); stream->flush();
        written = written && stream->getStatus().wasOk();
    }
    stream.reset();
    check(written && temporary.overwriteTargetFileWithTemporary(), "native component screenshot written");
}
void basicAndLibrary(OpenDuckProcessor &p, juce::AudioProcessorEditor &editor)
{
    auto *duck = dynamic_cast<juce::Button *>(find(editor,"mode0"));
    check(duck != nullptr && duck->getButtonText() == "DUCKING" && p.value("mix") == 100 && p.getCurveSnapshot().amount == 100 && !p.editPointsEnabled(), "INIT is Ducking, MIX100, Amount100 and points OFF");
    auto *scopeSource=dynamic_cast<juce::ComboBox *>(find(editor,"scopeSource"));check(scopeSource!=nullptr && scopeSource->getSelectedId()==2,"new instances show the real sidechain kick by default");
    bool controls = true;
    for (const auto *name : {"amount", "attack", "release", "tension", "phase", "mix", "bandFocus", "phaseFine", "scopeSource", "scopeFreeze", "scopeZoom", "speed", "snap", "group", "smoothing", "mirrorHorizontal", "mirrorVertical", "assistAnalyze", "assistCancel", "assistApply", "assistUndo"}) controls &= find(editor,name) != nullptr;
    for (int i = 0; i < 16; ++i) controls &= find(editor,"shape"+juce::String(i)) != nullptr;
    check(controls,"all seven primary controls,16shapes,scope and local analysis controls exist");
    auto *strength = find(editor,"assistStrength"); check(strength != nullptr && !strength->isEnabled(), "proposal strength is disabled before a real proposal exists");
    auto *active = dynamic_cast<juce::ToggleButton *>(find(editor,"on0"));
    auto *inactive = dynamic_cast<juce::ToggleButton *>(find(editor,"on1"));
    auto *spectral = dynamic_cast<juce::Button *>(find(editor,"mode1"));
    bool explicitState = active != nullptr && inactive != nullptr && spectral != nullptr;
    if (explicitState)
    {
        explicitState = active->getToggleState() && active->getButtonText()=="ON" && !inactive->getToggleState()
            && inactive->getButtonText()=="OFF" && inactive->getTitle().endsWith("OFF")
            && spectral->getTooltip().containsIgnoreCase("in series");
        inactive->setToggleState(true,juce::sendNotificationSync);
        explicitState &= p.value("on1")==1 && inactive->getButtonText()=="ON" && p.value("selected")==0;
        click(editor,"mode1");
        explicitState &= p.value("on0")==1 && p.value("on1")==1 && active->getButtonText()=="ON" && inactive->getButtonText()=="ON";
        inactive->setToggleState(false,juce::sendNotificationSync); click(editor,"mode0");
        explicitState &= p.value("on1")==0 && inactive->getButtonText()=="OFF" && inactive->getTitle().endsWith("OFF");
    }
    check(explicitState,"ON/OFF labels and accessibility track active audio engines independently of the selected card");
    std::set<int> all;
    bool sectorNavigation = true;
    for (int m = 0; m < 6; ++m)
    {
        click(editor,"mode"+juce::String(m)); const auto ids = OpenDuckProcessor::sectorPresetIds(m); all.insert(ids.begin(),ids.end());
        const auto beforeMix = p.value("mix"), beforeTrigger = p.value(OpenDuckProcessor::sectorParameterId("trigger",m));
        p.applySectorPreset(ids.front(),m); click(editor,"behaviorPrevious");
        sectorNavigation &= p.currentBehaviorPresetId(m) == ids.back(); click(editor,"behaviorNext"); sectorNavigation &= p.currentBehaviorPresetId(m) == ids.front();
        sectorNavigation &= p.value("mix") == beforeMix && p.value(OpenDuckProcessor::sectorParameterId("trigger",m)) == beforeTrigger;
        auto *menu = dynamic_cast<juce::ComboBox *>(find(editor,"behaviorMenu")); sectorNavigation &= menu != nullptr && menu->getRootMenu()->getNumItems() >= 7;
    }
    check(all.size() == 700 && sectorNavigation,"700catalog entries are filtered by sector, categorized, and arrows wrap without changing MIX/trigger");
    std::array<OpenDuckProcessor::CurveSnapshot,6> before;
    for (int m = 0; m < 6; ++m) before[(size_t)m] = p.getCurveSnapshot(m);
    click(editor,"mode0"); bool independent = true;
    for (int m = 0; m < 6; ++m) independent &= same(before[(size_t)m],p.getCurveSnapshot(m)) && p.value("on"+juce::String(m)) == (m == 0 ? 1.0f : 0.0f);
    check(independent,"selecting a sector preserves all other curves and ON states");
}
void typedValues(OpenDuckProcessor &p, juce::AudioProcessorEditor &editor)
{
    bool typed = type(editor,"amountValue","73") && type(editor,"attackValue","21.5") && type(editor,"releaseValue","250") && type(editor,"tensionValue","-23") && type(editor,"phaseValue","3.25") && type(editor,"mixValue","91") && type(editor,"bandFocusValue","633");
    const auto c = p.getCurveSnapshot(0);
    typed &= std::abs(c.amount-73)<.01f && std::abs(c.attack-21.5f)<.11f && std::abs(c.release-250)<.1f && std::abs(c.tension+23)<.1f && std::abs(c.phase-3.25f)<.11f && p.value("mix")==91 && std::abs(p.value(OpenDuckProcessor::sectorParameterId("crossLow",0))-633)<1;
    check(typed,"all seven primary readouts accept exact manually typed values");
    p.applySectorPreset(OpenDuckProcessor::sectorPresetIds(0).front(),0); set(p,"mix",100);
}
void gestures(OpenDuckProcessor &p, juce::AudioProcessorEditor &editor)
{
    auto *canvas = find(editor,"curveCanvas"); if (canvas == nullptr) { check(false,"curve canvas exists"); return; }
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    const auto at = [&](float x,float y){const auto r=static_cast<OpenDuckEditor &>(editor).getCurvePlotBounds();return juce::Point<float>(r.getX()+r.getWidth()*x,r.getY()+r.getHeight()*y);};
    const auto a=at(.6f,.55f),b=at(.76f,.55f); const auto initial=p.getCurveSnapshot(0);
    canvas->mouseDoubleClick(mouse(*canvas,a,left,a,false,2));
    check(same(initial,p.getCurveSnapshot(0)),"double click cannot create points in INIT");
    auto drag=[&](juce::Point<float> start,juce::Point<float> end,int mods){canvas->mouseDown(mouse(*canvas,start,mods,start));canvas->mouseDrag(mouse(*canvas,end,mods,start,true));canvas->mouseUp(mouse(*canvas,end,0,start,true));};
    drag(a,b,left); const float normal=p.getCurveSnapshot(0).phase;
    check(normal>0 && normal<8 && !p.getCurveSnapshot(0).custom,"graph drag moves the complete curve with gentle phase sensitivity");
    set(p,OpenDuckProcessor::curveParameterId("phase",0),0); click(editor,"phaseFine"); drag(a,b,left); const float fine=p.getCurveSnapshot(0).phase;
    check(fine>0 && fine<normal/3,"FINE reduces phase drag sensitivity without a discontinuity"); click(editor,"phaseFine");
    set(p,OpenDuckProcessor::curveParameterId("phase",0),0); click(editor,"editPoints");
    const auto late=at(.86f,.57f); canvas->mouseDoubleClick(mouse(*canvas,late,left|juce::ModifierKeys::altModifier,late,false,2)); const auto added=p.getCurveSnapshot(0);
    check(added.custom && added.count>8 && std::any_of(added.points.begin(),added.points.begin()+added.count,[](auto point){return point.x>.7f && point.x<1;}),"editing creates a late-cycle point beyond the recovery window without resetting Attack/Release");
    check(added.attack==initial.attack && added.release==initial.release,"late point creation preserves the Attack and Release parameter values");
    const auto before=added; click(editor,"editPoints"); check(!p.editPointsEnabled(0) && p.getCurveSnapshot(0).count==before.count && p.getCurveSnapshot(0).custom,"turning points OFF hides handles and preserves the custom shape");
    click(editor,"editPoints");
    auto *menu=dynamic_cast<juce::ComboBox *>(find(editor,"snap"));
    const double seconds=p.cycleSeconds(0); if(menu) menu->setSelectedId(6,juce::sendNotificationSync);
    check(p.cycleSeconds(0)==seconds,"Snap changes point alignment while the audio cycle duration stays independent");
    auto flat=p.getCurveSnapshot(0);flat.count=2;flat.custom=true;flat.linear=false;flat.phase=0;flat.points[0]={0,.25f};flat.points[1]={1,.75f};p.setCustomCurve(0,flat);
    const auto bendBaseline=p.getCurveSnapshot(0);const float originalBend=OpenDuckProcessor::sampleCurve(bendBaseline,.4f);
    juce::MouseWheelDetails wheel{0,.1f,false,false,false};canvas->mouseWheelMove(mouse(*canvas,a,juce::ModifierKeys::ctrlModifier,a),wheel);
    const auto raised=p.getCurveSnapshot(0);wheel.deltaY=-.1f;
    canvas->mouseWheelMove(mouse(*canvas,a,juce::ModifierKeys::ctrlModifier,a),wheel);canvas->mouseWheelMove(mouse(*canvas,a,juce::ModifierKeys::ctrlModifier,a),wheel);
    const auto lowered=p.getCurveSnapshot(0);bool fixedAnchors=raised.count==2&&lowered.count==2&&raised.custom&&lowered.custom&&raised.edit==bendBaseline.edit&&lowered.edit==bendBaseline.edit&&raised.linear==bendBaseline.linear&&lowered.linear==bendBaseline.linear;
    for(int i=0;i<2;++i)fixedAnchors &= raised.points[(size_t)i].x==bendBaseline.points[(size_t)i].x&&raised.points[(size_t)i].y==bendBaseline.points[(size_t)i].y&&lowered.points[(size_t)i].x==bendBaseline.points[(size_t)i].x&&lowered.points[(size_t)i].y==bendBaseline.points[(size_t)i].y;
    check(fixedAnchors&&OpenDuckProcessor::sampleCurve(raised,.4f)>originalBend&&OpenDuckProcessor::sampleCurve(lowered,.4f)<originalBend,"Ctrl wheel bends an existing segment up and down without adding or moving its two anchors");
    click(editor,"freeDraw"); drag(at(.1f,.8f),at(.3f,.25f),left|juce::ModifierKeys::altModifier);
    check(p.getCurveSnapshot(0).linear && p.getCurveSnapshot(0).count==193,"FREE DRAW writes the drawn linear path in the native editor");
}
void advancedAndPersistence(OpenDuckProcessor &p,std::unique_ptr<juce::AudioProcessorEditor> &editor,const juce::File &folder)
{
    click(*editor,"details"); bool pages=true;
    const char *ids[][5]={{"smooth","riseSlope","end","offset","output"},{"trigger","threshold","noteLow","holdoff","lookahead"},{"bands","linked","crossLow","lowAmount","highSolo"}};
    for(int tab=0;tab<3;++tab){click(*editor,"advancedTab"+juce::String(tab));for(auto base:ids[tab])pages &= find(*editor,OpenDuckProcessor::sectorParameterId(base,0))!=nullptr;}
    click(*editor,"advancedTab3");for(auto name:{"toolramp","toolstep","toolgate","toolclear","toolquantise","tooldouble","toolhalve","traceWav","traceSidechain","segmentType"})pages &= find(*editor,name)!=nullptr;
    check(pages,"Advanced exposes real envelope,trigger,bands,trace and editing tools in the chrome frame");
    click(*editor,"toolclear"); const auto cleared=p.getCurveSnapshot(0);bool open=cleared.custom;
    for(int i=0;i<=100;++i)open &= OpenDuckProcessor::sampleCurve(cleared,i/100.f)>.999f;
    check(open,"CLEAR is connected to the processor curve operation");
    click(*editor,"advancedTab1");screenshot(*editor,folder,"native-trigger.png");
    click(*editor,"advancedTab2");
    p.setSectorParameter(0,"linked",0);p.setSectorParameter(0,"bands",2);
    auto *band=dynamic_cast<juce::ComboBox *>(find(*editor,"bandCurve"));if(band)band->setSelectedId(2,juce::sendNotificationSync);
    const auto main=p.getCurveSnapshot(0);const bool bandTyped=type(*editor,"amountValue","44");
    check(bandTyped && std::abs(p.getBandCurveSnapshot(0,0).amount-44)<.01f && p.getCurveSnapshot(0).amount==main.amount,"selecting an independent band binds primary controls to that band's curve only");
    screenshot(*editor,folder,"native-bands.png");
    if(band)band->setSelectedId(1,juce::sendNotificationSync);click(*editor,"details");
    auto *source=dynamic_cast<juce::ComboBox *>(find(*editor,"scopeSource"));auto *zoom=dynamic_cast<juce::ComboBox *>(find(*editor,"scopeZoom"));auto *window=dynamic_cast<juce::ComboBox *>(find(*editor,"assistWindow"));
    if(source)source->setSelectedId(3,juce::sendNotificationSync);if(zoom)zoom->setSelectedId(3,juce::sendNotificationSync);if(window)window->setSelectedId(2,juce::sendNotificationSync);
    const auto expected=p.getCurveSnapshot(0);const auto mix=p.value("mix");editor.reset();editor.reset(p.createEditor());
    check(same(expected,p.getCurveSnapshot(0)) && mix==p.value("mix"),"closing and reopening the editor preserves user curves and all primary parameter values");
    auto *menu=dynamic_cast<juce::ComboBox *>(find(*editor,"behaviorMenu"));check(menu!=nullptr && menu->getText().containsIgnoreCase("CUSTOM"),"a reopened custom curve is labeled CUSTOM instead of INIT");
    source=dynamic_cast<juce::ComboBox *>(find(*editor,"scopeSource"));zoom=dynamic_cast<juce::ComboBox *>(find(*editor,"scopeZoom"));window=dynamic_cast<juce::ComboBox *>(find(*editor,"assistWindow"));
    check(source!=nullptr&&zoom!=nullptr&&window!=nullptr&&source->getSelectedId()==3&&zoom->getSelectedId()==3&&window->getSelectedId()==2,"editor reopen preserves source,zoom and this sector's5or10second analysis window");
    p.state.state.setProperty("editorAssistStrength0",.37f,nullptr);p.state.state.setProperty("editorAssistStrength1",.82f,nullptr);
    editor.reset();editor.reset(p.createEditor());
    auto *strength=dynamic_cast<juce::Slider *>(find(*editor,"assistStrength"));
    bool retainedStrength=strength!=nullptr&&std::abs(strength->getValue()-.37)<1e-5&&!strength->isEnabled();
    click(*editor,"mode1");strength=dynamic_cast<juce::Slider *>(find(*editor,"assistStrength"));
    retainedStrength &= strength!=nullptr&&std::abs(strength->getValue()-.82)<1e-5&&!strength->isEnabled();
    click(*editor,"mode0");strength=dynamic_cast<juce::Slider *>(find(*editor,"assistStrength"));
    retainedStrength &= strength!=nullptr&&std::abs(strength->getValue()-.37)<1e-5&&same(expected,p.getCurveSnapshot(0))&&mix==p.value("mix");
    check(retainedStrength,"requested AI Strength survives editor reopen independently per sector without a proposal or DSP writes");
}
void rampAndScope(OpenDuckProcessor &p,juce::AudioProcessorEditor &editor)
{
    auto *canvas=find(editor,"curveCanvas");if(canvas==nullptr){check(false,"canvas is available for ramp/scope regression");return;}
    click(editor,"details");click(editor,"advancedTab3");auto *paint=dynamic_cast<juce::ComboBox *>(find(editor,"paintMode"));click(editor,"details");
    const auto at=[&](float x,float y){const auto r=static_cast<OpenDuckEditor &>(editor).getCurvePlotBounds();return juce::Point<float>(r.getX()+r.getWidth()*x,r.getY()+r.getHeight()*y);};
    const auto draw=[&](int paintId)
    {
        if(paint)paint->setSelectedId(paintId,juce::sendNotificationSync);
        auto c=p.getCurveSnapshot(0);c.custom=true;c.count=2;c.linear=true;c.edit=true;c.freeDraw=true;c.phase=0;c.points[0]={0,.5f};c.points[1]={1,.5f};p.setCustomCurve(0,c);
        const auto first=at(.15f,.75f),middle=at(.45f,.22f),last=at(.82f,.75f);const int mods=juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::altModifier;
        canvas->mouseDown(mouse(*canvas,first,mods,first));canvas->mouseDrag(mouse(*canvas,middle,mods,first,true));canvas->mouseDrag(mouse(*canvas,last,mods,first,true));canvas->mouseUp(mouse(*canvas,last,0,first,true));return p.getCurveSnapshot(0);
    };
    const auto free=draw(1),ramp=draw(2);float delta=0;
    for(int i=0;i<=100;++i)delta=juce::jmax(delta,std::abs(OpenDuckProcessor::sampleCurve(free,i/100.f)-OpenDuckProcessor::sampleCurve(ramp,i/100.f)));
    check(paint!=nullptr && delta>.05f,"RAMP uses the gesture endpoints while FREE DRAW preserves the intervening mouse path");
    if(paint)paint->setSelectedId(1,juce::sendNotificationSync);
    auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());p.setBusesLayout(layout);p.setSectorParameter(0,"bands",0);p.setSectorParameter(0,"smooth",0);
    auto zero=p.getCurveSnapshot(0);zero.custom=true;zero.count=2;zero.edit=false;zero.freeDraw=false;zero.amount=100;zero.points[0]={0,0};zero.points[1]={1,0};p.setCustomCurve(0,zero);set(p,"mix",100);p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> audio(4,256);juce::MidiBuffer midi;
    for(int block=0;block<190;++block){for(int i=0;i<256;++i){const float phase=(float)(block*256+i)/48000;const float main=.2f*std::sin(juce::MathConstants<float>::twoPi*70*phase),side=.8f*std::sin(juce::MathConstants<float>::twoPi*50*phase);audio.setSample(0,i,main);audio.setSample(1,i,main);audio.setSample(2,i,side);audio.setSample(3,i,side);}p.processBlock(audio,midi);}
    auto *source=dynamic_cast<juce::ComboBox *>(find(editor,"scopeSource"));float reference[3]{};
    if(source){source->setSelectedId(1,juce::sendNotificationSync);for(int id:{2,3,1}){source->setSelectedId(id,juce::sendNotificationSync);reference[id-1]=(float)canvas->getProperties()["scopeReferencePeak"];}}
    check(source!=nullptr && reference[0]>.75f && std::abs(reference[0]-reference[1])<1e-6f && std::abs(reference[0]-reference[2])<1e-6f,"input,sidechain and output share one frame's main/SC waveform reference without renormalizing the attenuated output");
}
void twoBandAvailability(OpenDuckProcessor &p,juce::AudioProcessorEditor &editor)
{
    click(editor,"details");p.setSectorParameter(0,"bands",1);p.setSectorParameter(0,"linked",0);click(editor,"advancedTab2");
    auto *curves=dynamic_cast<juce::ComboBox *>(find(editor,"bandCurve"));bool disabled=curves!=nullptr&&!curves->isItemEnabled(3)&&curves->isItemEnabled(4);
    const auto before=p.getCurveSnapshot(0);std::array<float,3> values{};const char *bases[]={"midAmount","midSolo","crossHigh"};
    for(int i=0;i<3;++i)
    {
        const auto id=OpenDuckProcessor::sectorParameterId(bases[i],0);values[(size_t)i]=p.value(id);auto *c=find(editor,id);disabled &= c!=nullptr&&!c->isEnabled();
        if(c){const auto start=c->getLocalBounds().toFloat().getCentre(),end=start.translated(30,0);const int mods=juce::ModifierKeys::leftButtonModifier;c->mouseDown(mouse(*c,start,mods,start));c->mouseDrag(mouse(*c,end,mods,start,true));c->mouseUp(mouse(*c,end,0,start,true));}
    }
    bool unchanged=same(before,p.getCurveSnapshot(0));for(int i=0;i<3;++i)unchanged &= p.value(OpenDuckProcessor::sectorParameterId(bases[i],0))==values[(size_t)i];
    check(disabled&&unchanged,"two-band mode disables MID amount,solo,upper crossover and MID curve without changing parameters or curve points");
    p.setSectorParameter(0,"bands",2);click(editor,"advancedTab2");curves=dynamic_cast<juce::ComboBox *>(find(editor,"bandCurve"));bool enabled=curves!=nullptr&&curves->isItemEnabled(3)&&curves->isItemEnabled(4);
    for(auto base:bases){auto *c=find(editor,OpenDuckProcessor::sectorParameterId(base,0));enabled &= c!=nullptr&&c->isEnabled();}
    check(enabled,"three-band mode restores all MID,upper crossover and independent curve controls");click(editor,"details");
}
void singleKickLoopView()
{
    struct Clock final : juce::AudioPlayHead
    {
        double ppq = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setBpm(145); p.setPpqPosition(ppq); p.setIsPlaying(true); return p;
        }
    };
    const auto file = juce::File(juce::String::fromUTF8(__FILE__)).getParentDirectory().getChildFile("fixtures/audio/LAB-Kick-145.wav");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
    if (reader == nullptr) { check(false,"real kick fixture is available for single-hit native UI regression"); return; }
    juce::AudioBuffer<float> fixture(2,(int)reader->lengthInSamples); reader->read(&fixture,0,fixture.getNumSamples(),0,true,true);
    for (double phase : {0.0,.17})
    {
        Clock clock; auto processor = std::make_unique<OpenDuckProcessor>();
        processor->setPlayHead(&clock); auto layout=processor->getBusesLayout(); layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
        const bool layoutOk=processor->setBusesLayout(layout);
        processor->setSectorParameter(0,"speed",0); // 1/1: four real kicks in one DSP cycle.
        const auto before=processor->getCurveSnapshot(0); processor->prepareToPlay(reader->sampleRate,257);
        juce::MidiBuffer midi;
        for(int start=0;start<fixture.getNumSamples();start+=257)
        {
            const int count=juce::jmin(257,fixture.getNumSamples()-start);juce::AudioBuffer<float> audio(4,count);
            for(int c=0;c<2;++c){audio.copyFrom(c,0,fixture,c,start,count);audio.copyFrom(c+2,0,fixture,c,start,count);}
            clock.ppq=start/reader->sampleRate*145/60+phase*4;processor->processBlock(audio,midi);
            if(processor->getScopeFrame(0).complete)break;
        }
        processor->setScopeFrozen(true);const auto frame=processor->getScopeFrame(0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());auto* canvas=find(*editor,"curveCanvas");
        bool correct=layoutOk&&frame.complete&&frame.bins==512&&canvas!=nullptr;
        if(canvas!=nullptr)
        {
            const auto& properties=canvas->getProperties();const int begin=(int)properties["scopeSingleHitStartBin"],count=(int)properties["scopeSingleHitBinCount"];
            int visibleRealPeaks=0;float peak=0;
            for(int quarter=0;quarter<4;++quarter)
            {
                int at=quarter*128;float maximum=0;
                for(int bin=quarter*128;bin<(quarter+1)*128;++bin)
                {
                    const float value=juce::jmax(std::abs(frame.sideMin[(size_t)bin]),std::abs(frame.sideMax[(size_t)bin]));
                    if(value>maximum){maximum=value;at=bin;}peak=juce::jmax(peak,value);
                }
                visibleRealPeaks+=maximum>1e-4f&&(at-begin+512)%512<count?1:0;
            }
            correct &= (int)properties["scopeDetectedOnsets"]==4&&(int)properties["scopeVisibleOnsets"]==1&&visibleRealPeaks==1
                &&count>100&&count<150&&peak>1e-4f&&std::abs(frame.periodSeconds-4*60./145)<.001
                &&std::abs((float)properties["scopeReferencePeak"]-peak)<1e-5f&&same(before,processor->getCurveSnapshot(0));
        }
        check(correct,phase==0?"native 1/1 four-kick loop masks three attacks without moving the gain curve or normalizing OUTPUT"
                               :"phase-offset native loop preserves original time bins and shows one complete kick rather than the previous tail");
    }
}
void sectorSafeModifiedLabel(OpenDuckProcessor &p,juce::AudioProcessorEditor &editor)
{
    const auto original=p.getCurveSnapshot(2);auto applied=original;applied.custom=false;applied.count=0;applied.presetId=0;applied.amount=37;
    p.setCurveSnapshot(2,applied,"AI-style nonfactory result label regression");click(editor,"mode2");
    auto *menu=dynamic_cast<juce::ComboBox *>(find(editor,"behaviorMenu"));auto *favorite=find(editor,"favorite");
    check(menu!=nullptr&&menu->getText()=="MULTIBAND / MODIFIED"&&favorite!=nullptr&&!favorite->isEnabled(),"an AI-style result with a preset ID from another sector shows MODIFIED rather than a foreign factory name");
    p.setCurveSnapshot(2,original);click(editor,"mode0");
}
void legacy()
{
    const auto file=juce::File(juce::String::fromUTF8(__FILE__)).getParentDirectory().getChildFile("fixtures/legacy-v0.1.0-init.odpx");juce::MemoryBlock data;
    if(!file.loadFileAsData(data)){check(false,"legacy UI test fixture is present");return;}
    auto p=std::make_unique<OpenDuckProcessor>();p->setStateInformation(data.getData(),(int)data.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());auto *up=find(*editor,"upgradeRevision3"),*speed=find(*editor,"speed");
    check(!p->usesRevision3(0)&&up!=nullptr&&up->isVisible()&&speed!=nullptr&&!speed->isEnabled(),"legacy audio stays on its original path and new controls require the inline upgrade button");
    type(*editor,"amountValue","72");check(!p->usesRevision3(0),"editing a legacy primary parameter never silently upgrades the audio routing");
    const bool clicked=click(*editor,"upgradeRevision3");bool upgraded=clicked;for(int m=0;m<6;++m)upgraded &= p->usesRevision3(m);
    check(upgraded,"explicit upgrade button opts the complete chain into revision3");
}
}
int main(int argc,char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder=argc>1?juce::File(juce::String::fromUTF8(argv[1])):juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OpenDuck030UiQA");folder.createDirectory();
    try
    {
        auto p=std::make_unique<OpenDuckProcessor>();p->prepareToPlay(48000,512);std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        screenshot(*editor,folder,"native-default.png");
        basicAndLibrary(*p,*editor);typedValues(*p,*editor);gestures(*p,*editor);rampAndScope(*p,*editor);twoBandAvailability(*p,*editor);sectorSafeModifiedLabel(*p,*editor);advancedAndPersistence(*p,editor,folder);
        for(int width:{1010,1326,1767}){editor->setSize(width,juce::roundToInt(width*890.0/1767));editor->resized();bool contained=true;for(const auto *name:{"amountValue","attackValue","releaseValue","tensionValue","phaseValue","mixValue","bandFocusValue","chromeResize"})if(auto *c=find(*editor,name))contained &= editor->getLocalBounds().contains(editor->getLocalArea(c,c->getLocalBounds()));else contained=false;check(contained,"primary readouts and resize grip remain inside the chrome at supported editor sizes");screenshot(*editor,folder,juce::String("native-width-")+juce::String(width)+".png");}
        singleKickLoopView();legacy();
    }
    catch(const std::exception &e){std::cout<<"FAIL exception: "<<e.what()<<'\n';++failed;}
    std::cout<<"RESULT "<<passed<<" passed, "<<failed<<" failed\n";return failed==0?0:1;
}
