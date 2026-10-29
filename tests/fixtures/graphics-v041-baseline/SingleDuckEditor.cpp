#include "SingleDuckEditor.h"
#include "PremiumLook.h"
#include "ScopeWaveformView.h"
#include <BinaryData.h>

namespace {
const char* ids4[] = {"position4","duration4","curveTension0"};
juce::Colour cyan4() { return juce::Colour(0xff00e4f6); }
juce::String percent4(double value,bool signedValue=false){return (signedValue&&value>0?"+":"")+juce::String(value,1).replaceCharacter('.',',')+"%";}
void text4(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h,
           juce::Colour c=juce::Colours::white) {
    g.setColour(c); g.setFont(premium::font(h));
    g.drawFittedText(t,r.toNearestInt(),juce::Justification::centred,1,.9f);
}
}
SingleDuckEditor::Look::Look() : faceTexture(juce::Image::RGB,512,512,true) {
    juce::Image::BitmapData pixels(faceTexture,juce::Image::BitmapData::writeOnly);
    for(int y=0;y<512;++y)for(int x=0;x<512;++x){const float dx=x-255.5f,dy=y-255.5f;
        const float a=std::atan2(dx,-dy),r=std::sqrt(dx*dx+dy*dy);
        const unsigned noise=(unsigned(x)*1664525u+unsigned(y)*1013904223u)^(unsigned(x*y)*22695477u);
        float light=.62f+.22f*std::cos(2*a+.3f)+.055f*std::cos(6*a)+.005f*std::sin(r*.7f)+(int(noise%100)-50)*.00012f;
        light=juce::jlimit(.15f,.96f,light);pixels.setPixelColour(x,y,juce::Colour::fromFloatRGBA(light,light,light*.985f,1));
    }
}
void SingleDuckEditor::Look::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,
                                            float,float,juce::Slider& slider) {
    const auto a=odx::template4::Rotary::from((float)slider.getValue(),(float)slider.getMinimum(),(float)slider.getMaximum());
    juce::ignoreUnused(pos);
    const float d=float(juce::jmin(w,h))-8*scale;
    const auto centre=juce::Point<float>(x+w*.5f,y+h*.5f);
    const auto disc=juce::Rectangle<float>(d,d).withCentre(centre);
    g.setColour(juce::Colour(0xff101010));g.fillEllipse(disc);
    g.setColour(juce::Colour(0xfff0f0ee));g.drawEllipse(disc.reduced(1*scale),2*scale);
    g.setColour(juce::Colour(0xff4c4c49));g.drawEllipse(disc.reduced(4*scale),2*scale);
    const float radius=d*.5f-11*scale, stroke=juce::jmax(2.f,d*.022f);
    juce::Path track;track.addCentredArc(centre.x,centre.y,radius,radius,0,a.start,a.end,true);
    g.setColour(juce::Colour(0xff272c2c));g.strokePath(track,juce::PathStrokeType(stroke));
    if(a.normalized>0) {
        juce::Path active;active.addCentredArc(centre.x,centre.y,radius,radius,0,a.start,a.angle,true);
        // Butt ends ensure neither cap extends beyond the parameter's radial boundary.
        // Glow uses the same clipped angular sector, rather than a second loose arc.
        juce::Path sector;sector.startNewSubPath(centre);
        sector.lineTo(centre.x+std::sin(a.start)*(radius+stroke*2),centre.y-std::cos(a.start)*(radius+stroke*2));
        sector.addCentredArc(centre.x,centre.y,radius+stroke*2,radius+stroke*2,0,a.start,a.angle,false);
        sector.closeSubPath();g.saveState();g.reduceClipRegion(sector);
        g.setColour(cyan4().withAlpha(.12f));g.strokePath(active,juce::PathStrokeType(stroke*2));
        g.setColour(cyan4());g.strokePath(active,juce::PathStrokeType(stroke));g.restoreState();
    }
    const auto face=disc.reduced(d*.095f);const float rr=face.getWidth()*.5f;
    juce::Path faceClip;faceClip.addEllipse(face);g.saveState();g.reduceClipRegion(faceClip);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImage(faceTexture,face);g.restoreState();
    g.setColour(juce::Colour(0xffefefeb));g.drawEllipse(face,2*scale);
    g.setColour(juce::Colour(0xff454542));g.drawEllipse(face.reduced(3*scale),scale);
    const juce::Point<float> dir(std::sin(a.angle),-std::cos(a.angle));
    g.setColour(cyan4().withAlpha(.22f));g.drawLine({centre+dir*rr*.65f,centre+dir*rr*.94f},6*scale);
    g.setColour(cyan4());g.drawLine({centre+dir*rr*.65f,centre+dir*rr*.94f},3*scale);
}
void SingleDuckEditor::Look::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down) {
    auto r=b.getLocalBounds().toFloat().reduced(scale); const bool on=b.getToggleState();
    if (b.getName()=="power4") {
        g.setColour(juce::Colour(0xff020a0d)); g.fillEllipse(r); g.setColour(on?cyan4():juce::Colour(0xff64797b)); g.drawEllipse(r.reduced(scale),2*scale);
        const auto c=r.getCentre(); const float radius=r.getWidth()*.24f;juce::Path arc;arc.addCentredArc(c.x,c.y,radius,radius,0,.5f,juce::MathConstants<float>::twoPi-.5f,true);
        g.strokePath(arc,juce::PathStrokeType(3*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));g.drawLine(c.x,c.y-radius*1.35f,c.x,c.y,3*scale); return;
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(on?0xff07303a:0xff222626),r.getTopLeft(),juce::Colour(0xff05090a),r.getBottomRight(),false));
    g.fillRoundedRectangle(r,5*scale);g.setColour(on?cyan4():juce::Colour(0xffa0aaa9));g.drawRoundedRectangle(r,5*scale,on?2*scale:scale);
    if(over||down){g.setColour(cyan4().withAlpha(down?.15f:.05f));g.fillRoundedRectangle(r,5*scale);}
    if(b.getProperties().contains("shape4")) {
        auto source=OpenDuckProcessor::CurveSnapshot{};source.shape=(int)b.getProperties()["shape4"];
        source.phase=0;source.tension=0;source.modern3=true;source.template4=true;
        auto plot=r.reduced(18*scale,13*scale);juce::Path path;
        for(int i=0;i<=96;++i){const float t=i/96.f,y=plot.getBottom()-OpenDuckProcessor::sampleCurve(source,t)*plot.getHeight();
            if(i==0)path.startNewSubPath(plot.getX(),y);else path.lineTo(plot.getX()+t*plot.getWidth(),y);}
        g.setColour(on?cyan4():juce::Colours::white);g.strokePath(path,juce::PathStrokeType(2.6f*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    }
}
void SingleDuckEditor::Look::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {
    if(b.getProperties().contains("shape4") || b.getName()=="power4")return;
    if(b.getName()=="preset4") {
        const auto r=b.getLocalBounds().toFloat();const auto content=b.getButtonText();const auto split=content.indexOf("    ");
        text4(g,"PRESET",r.withHeight(25*scale),15*scale);
        text4(g,split>=0?content.substring(0,split):content,r.withTrimmedTop(25*scale).withTrimmedRight(110*scale),23*scale);
        text4(g,split>=0?content.substring(split+4):"",r.withTrimmedTop(25*scale).withTrimmedLeft(r.getWidth()-110*scale),18*scale);return;
    }
    text4(g,b.getButtonText(),b.getLocalBounds().toFloat().reduced(4*scale),
        (b.getName()=="preset4"?23.f:18.f)*scale,b.isEnabled()?(b.getToggleState()?cyan4():juce::Colours::white):juce::Colour(0xff65767a));
}
SingleDuckEditor::SingleDuckEditor(OpenDuckProcessor& p):AudioProcessorEditor(p),graph(*this),proc(p) {
    setLookAndFeel(&look);setOpaque(true);
    int n=0; if(auto* data=BinaryData::getNamedResource("TemplateReference_png",n))reference=juce::ImageFileFormat::loadFrom(data,(size_t)n);
    for(size_t i=0;i<3;++i){auto& k=knobs[i];k.setName(ids4[i]);k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);k.setMouseDragSensitivity(1000);k.setDoubleClickReturnValue(true,0);
        addAndMakeVisible(k);attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,ids4[i],k);
        auto& label=readouts[i];label.setName(juce::String(ids4[i])+"Value");label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId,cyan4());label.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061015));
        label.setEditable(false,true);label.onTextChange=[this,i]{if(readouts[i].isBeingEdited())return;set(ids4[i],readouts[i].getText().replaceCharacter(',','.').getFloatValue());};
        addAndMakeVisible(label);
    }
    for(size_t i=0;i<3;++i) knobs[i].onValueChange=[this,i]{
        if(!readouts[i].isBeingEdited())readouts[i].setText(percent4(knobs[i].getValue(),i==1),juce::dontSendNotification);
        graph.repaint();
    };
    knobs[0].setTooltip("MIX: linear horizontal position, 0..100% = -50..+50% of a cycle; centred position 50%. New INIT: 0.0%, Power off. Enable Power to apply ducking. No hidden gain change.");
    knobs[1].setTooltip("PHASE: duration around the envelope centre, -50..+50% = 0.5..1.5 times the original duration. MIX keeps the same position.");
    knobs[2].setTooltip("CURVE: curvature only; position, duration and depth stay unchanged.");
    for(int i=0;i<16;++i){auto& b=shapes[(size_t)i];b.setName("shape4_"+juce::String(i));b.getProperties().set("shape4",i);
        b.onClick=[this,i]{proc.selectFactoryShape(i,0);timerCallback();};addAndMakeVisible(b);}
    const char* names[]={"1/8","1/4","1/2","1/1"};const int indices[]={3,2,1,0};
    for(int i=0;i<4;++i){auto& b=rates[(size_t)i];b.setButtonText(names[i]);b.onClick=[this,i,indices]{proc.setSectorParameter(0,"speedMode",0);proc.setSectorParameter(0,"speed",(float)indices[i]);timerCallback();};addAndMakeVisible(b);}
    const char* triggerNames[]={"SYNC","MIDI","AUDIO"};const int triggers4[]={0,2,1};
    for(int i=0;i<3;++i){auto& b=triggers[(size_t)i];b.setButtonText(triggerNames[i]);b.onClick=[this,i,triggers4]{proc.setSectorParameter(0,"trigger",(float)triggers4[i]);timerCallback();};addAndMakeVisible(b);}
    triggers[2].setTooltip("External sidechain kick. Route the source track using Ableton's device sidechain chooser. Right-click the panel for detector settings.");
    for(auto* b:{&previous,&next,&preset,&manual,&ai,&five,&ten,&analyze,&power,&mirrorH,&mirrorV,&editPoints})addAndMakeVisible(*b);
    preset.setName("preset4");preset.onClick=[this]{popupPresets();};
    previous.onClick=[this]{selectPreset((proc.currentBehaviorPresetId(0)+699)%700);};next.onClick=[this]{selectPreset((proc.currentBehaviorPresetId(0)+1)%700);};
    manual.onClick=[this]{aiMode=false;settingsOpen=false;mini.setVisible(false);proc.state.state.setProperty("templateAiView",false,nullptr);timerCallback();};
    ai.onClick=[this]{aiMode=true;proc.state.state.setProperty("templateAiView",true,nullptr);showAi();timerCallback();};
    five.onClick=[this]{seconds=5;proc.state.state.setProperty("templateCaptureSeconds",5,nullptr);timerCallback();};
    ten.onClick=[this]{seconds=10;proc.state.state.setProperty("templateCaptureSeconds",10,nullptr);timerCallback();};
    analyze.onClick=[this]{aiMode=true;proc.startLocalAssistCapture(0,seconds);showAi();timerCallback();};
    power.onClick=[this]{set("bypass",proc.value("bypass")>.5f?0.f:1.f);timerCallback();};
    power.setName("power4");
    power.setTooltip("Power: enable ducking. New INIT opens OFF with unattenuated, latency-aligned audio. The input waveform remains visible while OFF.");
    mirrorH.onClick=[this]{proc.transformCurve(OpenDuckProcessor::CurveOperation::mirrorHorizontal,0);timerCallback();};
    mirrorV.onClick=[this]{proc.transformCurve(OpenDuckProcessor::CurveOperation::mirrorVertical,0);timerCallback();};
    editPoints.onClick=[this]{proc.setEditPointsEnabled(!proc.editPointsEnabled(0),0);timerCallback();};
    addAndMakeVisible(graph);addChildComponent(mini);mini.setOpaque(false);mini.addAndMakeVisible(miniTitle);mini.addAndMakeVisible(close);
    close.onClick=[this]{settingsOpen=false;mini.setVisible(false);repaint();};
    mini.addAndMakeVisible(aiMessage);mini.addAndMakeVisible(apply);mini.addAndMakeVisible(undo);mini.addAndMakeVisible(cancel);
    apply.onClick=[this]{proc.applyLocalAssist(0);timerCallback();};undo.onClick=[this]{proc.undoLocalAssist(0);timerCallback();};cancel.onClick=[this]{proc.cancelLocalAssist(0);timerCallback();};
    const char* settingIds[]={"threshold","hysteresis","holdoff","smooth"};const char* settingNames[]={"Soglia / dB","Isteresi / dB","Retrigger / ms","Smoothing / ms"};
    for(size_t i=0;i<4;++i){auto& slider=settings[i];slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,24);
        mini.addAndMakeVisible(slider);settingsLabels[i].setText(settingNames[i],juce::dontSendNotification);mini.addAndMakeVisible(settingsLabels[i]);
        settingAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,OpenDuckProcessor::sectorParameterId(settingIds[i],0),slider);}
    routing.setText("Ableton: dispositivo > Sidechain > Audio From > traccia kick > punto di prelievo.\nMain: basso / materiale da attenuare. Nessuna modifica alle preferenze globali.",juce::dontSendNotification);routing.setJustificationType(juce::Justification::topLeft);mini.addAndMakeVisible(routing);
    miniTitle.setJustificationType(juce::Justification::centredLeft);aiMessage.setJustificationType(juce::Justification::topLeft);
    aiMode=(bool)proc.state.state.getProperty("templateAiView",false);seconds=(int)proc.state.state.getProperty("templateCaptureSeconds",5)==10?10:5;
    waveSource=juce::jlimit(0,2,(int)proc.state.state.getProperty("templateWaveSource",0));
    waveSingle=(bool)proc.state.state.getProperty("templateWaveSingle",false);
    waveZoom=juce::jlimit(1.f,8.f,(float)proc.state.state.getProperty("templateWaveZoom",1.f));
    waveOffset=juce::jlimit(0.f,1.f-1.f/waveZoom,(float)proc.state.state.getProperty("templateWaveOffset",0.f));
    graph.setTooltip("Onda reale dell'ingresso. Clic destro o VISTA: sorgente, loop, singolo colpo e zoom. Trascina per scorrere una porzione ingrandita; rotella per zoom.");
    setResizable(true,false);setResizeLimits(864,456,2592,1368);getConstrainer()->setFixedAspectRatio(1728.0/912);
    resizeGrip=std::make_unique<juce::ResizableCornerComponent>(this,getConstrainer());addAndMakeVisible(*resizeGrip);
    const int width=juce::jlimit(864,2592,(int)proc.state.state.getProperty("templateWidth",1296));setSize(width,juce::roundToInt(width*912.f/1728));
    timerCallback();startTimerHz(30);
}
SingleDuckEditor::~SingleDuckEditor(){stopTimer();setLookAndFeel(nullptr);}
juce::Rectangle<int> SingleDuckEditor::box(float x,float y,float w,float h)const{return {juce::roundToInt(x*scale),juce::roundToInt(y*scale),juce::roundToInt(w*scale),juce::roundToInt(h*scale)};}
void SingleDuckEditor::set(const juce::String& id,float v){if(auto* p=proc.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}}
void SingleDuckEditor::resized(){
    scale=getWidth()/1728.f;look.scale=scale;mini.scale=scale;proc.state.state.setProperty("templateWidth",getWidth(),nullptr);
    knobs[0].setBounds(box(69,150,398,390));knobs[1].setBounds(box(70,619,166,160));knobs[2].setBounds(box(291,619,166,160));
    readouts[0].setBounds(box(197,565,140,37));readouts[1].setBounds(box(91,811,124,37));readouts[2].setBounds(box(306,811,130,37));
    for(auto& l:readouts)l.setFont(premium::font(29*scale));
    previous.setBounds(box(539,50,76,68));preset.setBounds(box(627,48,440,73));next.setBounds(box(1077,50,77,68));
    manual.setBounds(box(1188,49,88,70));ai.setBounds(box(1285,50,75,69));five.setBounds(box(1368,50,51,69));ten.setBounds(box(1429,50,50,69));analyze.setBounds(box(1488,50,82,69));power.setBounds(box(1587,42,88,88));
    for(int i=0;i<4;++i)rates[(size_t)i].setBounds(box(516+i*118.f,170,112,54));
    triggers[0].setBounds(box(1050,170,112,54));triggers[1].setBounds(box(1169,170,111,54));triggers[2].setBounds(box(1294,170,125,54));
    graph.setBounds(box(515,244,1141,330));
    for(int i=0;i<16;++i)shapes[(size_t)i].setBounds(box(518+(i%8)*144.f,590+(i/8)*86.f,128,72));
    mirrorH.setBounds(box(516,770,337,67));mirrorV.setBounds(box(872,770,344,67));editPoints.setBounds(box(1234,770,419,67));
    mini.setBounds(box(1030,236,623,333));miniTitle.setBounds(box(18,10,525,33));close.setBounds(box(555,10,48,33));
    aiMessage.setBounds(box(18,55,585,195));apply.setBounds(box(18,268,180,46));undo.setBounds(box(211,268,180,46));cancel.setBounds(box(404,268,199,46));
    for(int i=0;i<4;++i){settingsLabels[(size_t)i].setBounds(box(18,52+i*45.f,160,33));settings[(size_t)i].setBounds(box(181,52+i*45.f,423,33));}
    routing.setBounds(box(18,238,585,82));miniTitle.setFont(premium::font(19*scale));aiMessage.setFont(premium::font(17*scale,false));routing.setFont(premium::font(14*scale,false));
    if(resizeGrip)resizeGrip->setBounds(box(1625,854,40,32));
}
void SingleDuckEditor::paint(juce::Graphics& graphics){
    auto& g=graphics;g.addTransform(juce::AffineTransform::scale(scale));
    if(reference.isValid())g.drawImage(reference,{0,0,1728,912});else g.fillAll(juce::Colour(0xff9a9b96));
    // All dynamic source pixels are covered by live components/opaque backplates.
    premium::panel(g,{1179,36,404,94},10);
    g.setColour(juce::Colour(0xff03090b));g.fillRect(509,157,919,78);
    g.setColour(juce::Colour(0xffa0aaaa));g.drawLine(1015,174,1015,216,1.2f);

    g.setColour(juce::Colour(0xff050b0e));g.fillRect(1445,164,215,65);
    text4(g,juce::String(proc.bpm.load(),1)+" BPM",{1450,172,205,49},28);
    g.setColour(juce::Colour(0xff060d11));g.fillRect(514,579,1143,188);
    for(int i=0;i<16;++i)text4(g,juce::String(i+1),{518+(i%8)*144.f,661+(i/8)*86.f,128,17},16);

}
void SingleDuckEditor::timerCallback(){
    for(size_t i=0;i<3;++i)if(!readouts[i].isBeingEdited())readouts[i].setText(percent4(knobs[i].getValue(),i==1),juce::dontSendNotification);
    const auto curve=proc.getCurveSnapshot(0);for(int i=0;i<16;++i)shapes[(size_t)i].setToggleState(!curve.custom&&curve.shape==i,juce::dontSendNotification);
    const int speed=(int)proc.value(OpenDuckProcessor::sectorParameterId("speed",0));const int values[]={3,2,1,0};for(int i=0;i<4;++i)rates[(size_t)i].setToggleState(speed==values[i],juce::dontSendNotification);
    const int trigger=(int)proc.value(OpenDuckProcessor::sectorParameterId("trigger",0));const int choices[]={0,2,1};for(int i=0;i<3;++i)triggers[(size_t)i].setToggleState(trigger==choices[i],juce::dontSendNotification);
    manual.setToggleState(!aiMode,juce::dontSendNotification);ai.setToggleState(aiMode,juce::dontSendNotification);five.setToggleState(seconds==5,juce::dontSendNotification);ten.setToggleState(seconds==10,juce::dontSendNotification);
    power.setToggleState(proc.value("bypass")<.5f,juce::dontSendNotification);editPoints.setToggleState(curve.edit,juce::dontSendNotification);editPoints.setButtonText(curve.edit?"EDIT POINTS / ON":"EDIT POINTS / OFF");
    const int storedId=proc.currentBehaviorPresetId(0),id=juce::jlimit(0,699,storedId);const auto& factory=OpenDuckProcessor::factoryPresets()[(size_t)id];
    const float factoryLength=id==0?1.f:.4f+.14f*(id%4)+.02f*(id/100);
    const bool modified=curve.custom||curve.shape!=factory.shape||std::abs(curve.tension-factory.tension)>.001f||std::abs(curve.amount-factory.amount)>.001f||std::abs(curve.length-factoryLength)>.001f||std::abs(proc.value("position4")-(id==0?0.f:50+factory.phase))>.001f||std::abs(proc.value("duration4"))>.001f;
    preset.setButtonText((storedId<0?juce::String("Custom Shape"):OpenDuckProcessor::templatePresetName(id)+(modified?" *":""))+"    "+(storedId<0?juce::String("---"):juce::String(id+1).paddedLeft('0',3))+" / 700");
    if(aiMode&&!settingsOpen){const auto status=proc.localAssistStatus(0);juce::String message(status.message);odx::ai::Proposal proposal;
        const bool ready=proc.localAssistProposal(0,proposal)&&proposal.improved;
        if(ready)message+="\nObjective: "+juce::String(proposal.baseline.objective,5)+" -> "+juce::String(proposal.result.objective,5);
        if(status.state==odx::ai::State::capturing)message+="\nCattura reale: "+juce::String(status.capturedSeconds,1)+" / "+juce::String(seconds)+" s";
        aiMessage.setText(message,juce::dontSendNotification);apply.setEnabled(ready);undo.setEnabled(proc.canUndoLocalAssist(0));}
    graph.repaint();repaint();
}
void SingleDuckEditor::showSettings(){settingsOpen=true;miniTitle.setText("SIDECHAIN / DETECTOR",juce::dontSendNotification);for(auto& s:settings)s.setVisible(true);for(auto& l:settingsLabels)l.setVisible(true);routing.setVisible(true);aiMessage.setVisible(false);apply.setVisible(false);undo.setVisible(false);cancel.setVisible(false);mini.setVisible(true);mini.toFront(false);repaint();}
void SingleDuckEditor::showAi(){settingsOpen=false;miniTitle.setText("AI LOCALE / PROPOSTA",juce::dontSendNotification);for(auto& s:settings)s.setVisible(false);for(auto& l:settingsLabels)l.setVisible(false);routing.setVisible(false);aiMessage.setVisible(true);apply.setVisible(true);undo.setVisible(true);cancel.setVisible(true);mini.setVisible(true);mini.toFront(false);repaint();}
void SingleDuckEditor::mouseDown(const juce::MouseEvent& e){if(e.mods.isPopupMenu())showSettings();}
void SingleDuckEditor::selectPreset(int id){
    proc.applyTemplatePreset(id);timerCallback();
}
void SingleDuckEditor::popupPresets(){juce::PopupMenu menu;for(int cat=0;cat<7;++cat){juce::PopupMenu section;for(int n=0;n<100;++n){int id=cat*100+n;section.addItem(id+1,juce::String(id+1).paddedLeft('0',3)+"  "+OpenDuckProcessor::templatePresetName(id));}menu.addSubMenu(OpenDuckProcessor::factoryCategoryNames()[(size_t)cat],section);}juce::Component::SafePointer<SingleDuckEditor> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(preset),[safe](int id){if(safe&&id>0)safe->selectPreset(id-1);});}
juce::Rectangle<float> SingleDuckEditor::Graph::plot()const{return getLocalBounds().toFloat().withTrimmedLeft(65*owner.scale).withTrimmedTop(5*owner.scale).withTrimmedBottom(36*owner.scale).withTrimmedRight(7*owner.scale);}
void SingleDuckEditor::saveWaveView(){
    auto& state=proc.state.state;state.setProperty("templateWaveSource",waveSource,nullptr);
    state.setProperty("templateWaveSingle",waveSingle,nullptr);state.setProperty("templateWaveZoom",waveZoom,nullptr);
    state.setProperty("templateWaveOffset",waveOffset,nullptr);graph.repaint();
}
void SingleDuckEditor::Graph::showViewMenu(){
    juce::PopupMenu menu,source,span,zoom;
    const char* sources[]={"Ingresso del canale (default)","Sidechain esterno","Uscita processata"};
    for(int i=0;i<3;++i)source.addItem(10+i,sources[i],true,owner.waveSource==i);
    for(int i=0;i<5;++i){const int beats=1<<i;span.addItem(20+i,juce::String(beats)+(beats==1?" beat":" beat"),true,owner.proc.getInputScopeBeats()==beats);}
    for(int i=0;i<4;++i)zoom.addItem(30+i,juce::String(1<<i)+"x",true,owner.waveZoom==float(1<<i));
    menu.addSectionHeader("ONDA REALE / VISTA");menu.addSubMenu("Sorgente",source);
    menu.addItem(1,"Intero loop",true,!owner.waveSingle);menu.addItem(2,"Singolo colpo",true,owner.waveSingle);
    menu.addSubMenu("Finestra temporale",span);menu.addSubMenu("Ingrandimento",zoom);
    menu.addItem(3,"Mostra tutta la finestra");menu.addSeparator();menu.addItem(4,"Impostazioni sidechain / detector");
    juce::Component::SafePointer<Graph> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int id){
        if(!safe||id==0)return;auto& o=safe->owner;
        if(id>=10&&id<13)o.waveSource=id-10;
        else if(id>=20&&id<25){o.proc.setInputScopeBeats(1<<(id-20));o.waveZoom=1;o.waveOffset=0;}
        else if(id>=30&&id<34){const float centre=o.waveOffset+.5f/o.waveZoom;o.waveZoom=float(1<<(id-30));o.waveOffset=juce::jlimit(0.f,1.f-1.f/o.waveZoom,centre-.5f/o.waveZoom);}
        else if(id==1)o.waveSingle=false;
        else if(id==2)o.waveSingle=true;
        else if(id==3){o.waveZoom=1;o.waveOffset=0;}
        else if(id==4)o.showSettings();
        o.saveWaveView();
    });
}
void SingleDuckEditor::Graph::paint(juce::Graphics& g){
    const float s=owner.scale;const auto r=plot();g.fillAll(juce::Colour(0xff030c10));
    g.setColour(juce::Colour(0xff153540));for(int i=0;i<=8;++i){float x=r.getX()+r.getWidth()*i/8;g.drawLine(x,r.getY(),x,r.getBottom(),.75f*s);}for(int i=0;i<=4;++i){float y=r.getY()+r.getHeight()*i/4;g.drawLine(r.getX(),y,r.getRight(),y,.75f*s);}
    for(int i=0;i<3;++i)text4(g,juce::String(100-i*50)+"%",{0,r.getY()+r.getHeight()*i/2-(i==0?0.f:i==2?24.f:12.f)*s,60*s,24*s},20*s);
    const auto frame=owner.proc.getInputScopeFrame();const bool editing=owner.proc.editPointsEnabled(0);
    const double period=frame.bins>0?frame.periodSeconds:owner.proc.getInputScopeBeats()*60/owner.proc.bpm.load();
    const double cyclesInWindow=period/owner.proc.cycleSeconds(0),phaseStart=frame.bins>0?frame.curvePhaseStart:0;
    const auto axis=odx::view::scopeAxis(editing?owner.proc.cycleSeconds(0):period,editing?0:owner.waveOffset,editing?1:owner.waveZoom,frame.phase);
    for(int i=0;i<5;++i){const double ms=axis.ticksMs[(size_t)i];const auto label=ms>=1000?juce::String(ms/1000,2)+"s":juce::String(ms,0)+"ms";auto b=juce::Rectangle<float>(r.getX()+r.getWidth()*i/4-40*s,r.getBottom()+7*s,80*s,23*s);if(i==0)b.setX(r.getX());if(i==4)b.setRight(r.getRight());text4(g,label,b,17*s);}
    g.saveState();g.reduceClipRegion(r.toNearestInt());
    const auto& lo=owner.waveSource==1?frame.sideMin:owner.waveSource==2?frame.outMin:frame.mainMin;
    const auto& hi=owner.waveSource==1?frame.sideMax:owner.waveSource==2?frame.outMax:frame.mainMax;
    auto hit=odx::view::isolateSingleHit(lo,hi,frame.periodSeconds,frame.bins);
    // Partial capture has no known audio before firstBin: never invent a kick
    // from an unknown prefix or draw it as a completed waveform.
    if(frame.firstBin>0)hit.binCount=0;
    const float drawZoom=editing?float(cyclesInWindow):owner.waveZoom;
    const double firstKnownCycle=phaseStart+frame.firstBin/512.0*cyclesInWindow;
    const float drawOffset=editing?float((std::ceil(firstKnownCycle-1.e-7)-phaseStart)/cyclesInWindow):owner.waveOffset;
    if(frame.bins>frame.firstBin){g.setColour(juce::Colour(0xffe1e5e7).withAlpha(.72f));
        for(int i=frame.firstBin;i<frame.bins&&i<512;++i){if(owner.waveSingle&&!hit.contains(i))continue;
            const float display=((i+.5f)/512-drawOffset)*drawZoom;if(display<0||display>1)continue;
            if(std::max(std::abs(lo[(size_t)i]),std::abs(hi[(size_t)i]))<1.e-7f)continue;
            const float x=r.getX()+r.getWidth()*display;
            g.drawLine(x,r.getCentreY()-hi[(size_t)i]*r.getHeight()*.46f,x,r.getCentreY()-lo[(size_t)i]*r.getHeight()*.46f,juce::jmax(.8f*s,r.getWidth()*drawZoom/512+.15f*s));}}
    juce::Path path;const int count=juce::jmax(800,juce::roundToInt(r.getWidth()));double previousAt=0;
    for(int i=0;i<=count;++i){float t=i/float(count);const double at=phaseStart+(owner.waveOffset+t/owner.waveZoom)*cyclesInWindow;
        float cycle=editing?t:float(at-std::floor(at)),x=r.getX()+t*r.getWidth(),y=r.getBottom()-owner.proc.previewCurveGain(0,cycle)*r.getHeight();
        if(i==0)path.startNewSubPath(x,y);
        else if(!editing&&std::floor(at)>std::floor(previousAt)){
            const double boundary=std::floor(at),position=((boundary-phaseStart)/cyclesInWindow-owner.waveOffset)*owner.waveZoom;
            const float edge=r.getX()+float(position)*r.getWidth();
            path.lineTo(edge,r.getBottom()-owner.proc.previewCurveGain(0,1)*r.getHeight());
            // A cycle ending at the right border keeps its full final value.
            // The next cycle starts only when there is visible time after it.
            if(i<count||at-boundary>1.e-7){path.startNewSubPath(edge,r.getBottom()-owner.proc.previewCurveGain(0,0)*r.getHeight());path.lineTo(x,y);}
        }else path.lineTo(x,y);previousAt=at;}
    // Keep the entire stroke visible at0/100% and at the right endpoint.
    // The waveform remains clipped to its plot; only the real curve stroke
    // gets its half-width margin, so it cannot disappear against the border.
    g.restoreState();g.saveState();g.reduceClipRegion(r.expanded(4*s).toNearestInt());
    g.setColour(cyan4().withAlpha(.14f));g.strokePath(path,juce::PathStrokeType(7.2f*s));
    g.setColour(cyan4());g.strokePath(path,juce::PathStrokeType(3.2f*s,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    const auto c=owner.proc.getCurveSnapshot(0);if(c.edit&&c.custom)for(int i=0;i<c.count;++i){const auto& p=c.points[(size_t)i];const float x=r.getX()+owner.proc.rawToPreviewCycle(0,p.x)*r.getWidth();g.setColour(juce::Colours::white);g.fillEllipse(x-3*s,r.getBottom()-owner.proc.previewCurveGain(0,(x-r.getX())/r.getWidth())*r.getHeight()-3*s,6*s,6*s);}
    const char* sourceName[]={"INGRESSO","SIDECHAIN","USCITA"};
    const auto caption=editing?juce::String("EDIT / 1 INVILUPPO"):juce::String(sourceName[owner.waveSource])+" / "+juce::String(owner.proc.getInputScopeBeats(),0)+" BEAT / "+(owner.waveSingle?"SINGOLO":"LOOP")+" / "+juce::String(owner.waveZoom,0)+"x  |  VISTA";
    const auto badge=juce::Rectangle<float>(r.getRight()-420*s,r.getY()+5*s,414*s,23*s);
    g.setColour(juce::Colour(0xff071219).withAlpha(.94f));g.fillRoundedRectangle(badge,3*s);text4(g,caption,badge,13*s,juce::Colour(0xffcbd4d8));
    g.restoreState();
}
void SingleDuckEditor::Graph::edit(juce::Point<float> position,bool add){
    if(!owner.proc.editPointsEnabled(0))return;auto c=owner.proc.getCurveSnapshot(0);auto r=plot();
    if(!c.custom){c.custom=true;c.shape=16;c.count=16;auto src=c;src.custom=false;src.shape=owner.proc.getCurveSnapshot(0).shape;for(int i=0;i<c.count;++i)c.points[(size_t)i]={i/15.f,OpenDuckProcessor::sampleCurve(src,i/15.f),0};}
    const float display=juce::jlimit(0.f,1.f,(position.x-r.getX())/r.getWidth());const float source=display;
    const float x=owner.proc.previewCycleToRaw(0,source),y=OpenDuckProcessor::gainToEnvelope(juce::jlimit(0.f,1.f,(r.getBottom()-position.y)/r.getHeight()),c.amount);
    if(add&&c.count<256){c.points[(size_t)c.count++]={x,y,0};
        std::sort(c.points.begin(),c.points.begin()+c.count,[](const auto&a,const auto&b){return a.x<b.x;});
        node=0;for(int i=0;i<c.count;++i)if(std::abs(c.points[(size_t)i].x-x)<.00001f){node=i;break;}
    }else { if(node<0){float best=2;for(int i=0;i<c.count;++i){float d=std::abs(c.points[(size_t)i].x-x);if(d<best){best=d;node=i;}}}
        if(node>=0){c.points[(size_t)node].x=juce::jlimit(node==0?0.f:c.points[(size_t)(node-1)].x+.0001f,node==c.count-1?1.f:c.points[(size_t)(node+1)].x-.0001f,x);c.points[(size_t)node].y=y;}
    }
    std::sort(c.points.begin(),c.points.begin()+c.count,[](const auto&a,const auto&b){return a.x<b.x;});owner.proc.setCurveSnapshot(0,c);repaint();
}
void SingleDuckEditor::Graph::mouseDown(const juce::MouseEvent&e){if(e.mods.isPopupMenu()||(e.y<35*owner.scale&&e.x>plot().getRight()-430*owner.scale)){showViewMenu();return;}node=-1;panStart=owner.waveOffset;panMouse=e.position.x;if(owner.proc.editPointsEnabled(0)){owner.proc.undo.beginNewTransaction("Edit template points");edit(e.position,false);}}
void SingleDuckEditor::Graph::mouseDrag(const juce::MouseEvent&e){if(owner.proc.editPointsEnabled(0))edit(e.position,false);else if(owner.waveZoom>1){owner.waveOffset=juce::jlimit(0.f,1.f-1.f/owner.waveZoom,panStart-(e.position.x-panMouse)/(plot().getWidth()*owner.waveZoom));repaint();}}
void SingleDuckEditor::Graph::mouseUp(const juce::MouseEvent&){node=-1;owner.saveWaveView();}
void SingleDuckEditor::Graph::mouseDoubleClick(const juce::MouseEvent&e){if(owner.proc.editPointsEnabled(0))edit(e.position,true);else{owner.waveZoom=1;owner.waveOffset=0;owner.saveWaveView();}}
void SingleDuckEditor::Graph::mouseWheelMove(const juce::MouseEvent&e,const juce::MouseWheelDetails&w){if(owner.proc.editPointsEnabled(0))return;const auto r=plot();const float anchor=juce::jlimit(0.f,1.f,(e.position.x-r.getX())/r.getWidth());const float at=owner.waveOffset+anchor/owner.waveZoom;owner.waveZoom=juce::jlimit(1.f,8.f,owner.waveZoom*(w.deltaY>0?2.f:.5f));owner.waveOffset=juce::jlimit(0.f,1.f-1.f/owner.waveZoom,at-anchor/owner.waveZoom);owner.saveWaveView();}



void SingleDuckEditor::paintOverChildren(juce::Graphics& g){
    if(!reference.isValid())return;
    const auto target=box(182,526,170,34);
    g.drawImage(reference,target.getX(),target.getY(),target.getWidth(),target.getHeight(),182,526,170,34);
}
