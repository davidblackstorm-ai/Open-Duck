#include "SingleDuckEditor.h"
#include "PremiumLook.h"
#include "ScopeWaveformView.h"
#include <BinaryData.h>

namespace {
const char* ids4[] = {"duckMix4","duration4","curveTension0"};
juce::Colour cyan4() { return juce::Colour(0xff00e4f6); }
juce::String percent4(double value,bool signedValue=false){return (signedValue&&value>0?"+":"")+juce::String(value,1).replaceCharacter('.',',')+"%";}
void text4(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h,
           juce::Colour c=juce::Colours::white,const char* style=nullptr) {
    auto options=juce::FontOptions("Segoe UI",h,juce::Font::plain);
    if(style!=nullptr)options=options.withStyle(style);
    g.setColour(c); g.setFont(juce::Font(options));
    g.drawFittedText(t,r.toNearestInt(),juce::Justification::centred,1,.9f);
}
void readoutPlate4(juce::Graphics& g,juce::Rectangle<float> r){
    g.setColour(juce::Colour(0xff101a20));g.fillRect(r);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff415660),r.getTopLeft(),juce::Colour(0xff111d25),r.getBottomLeft(),false));
    g.fillRoundedRectangle(r,5);g.setColour(juce::Colour(0xff7a8d95));g.drawRoundedRectangle(r.reduced(.5f),5,1);
    const auto inset=r.reduced(2);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff03090f),inset.getTopLeft(),juce::Colour(0xff081923),inset.getBottomLeft(),false));
    g.fillRoundedRectangle(inset,3);g.setColour(juce::Colour(0xff02060a));g.drawRoundedRectangle(inset,3,1);
}
void circuitBadge4(juce::Graphics& g){
    premium::panel(g,{1179,36,404,94},10);
    const juce::Rectangle<float> r(1187,44,388,78);const float cut=8;
    juce::Path frame;frame.startNewSubPath(r.getX()+cut,r.getY());frame.lineTo(r.getRight()-cut,r.getY());
    frame.lineTo(r.getRight(),r.getY()+cut);frame.lineTo(r.getRight(),r.getBottom()-cut);
    frame.lineTo(r.getRight()-cut,r.getBottom());frame.lineTo(r.getX()+cut,r.getBottom());
    frame.lineTo(r.getX(),r.getBottom()-cut);frame.lineTo(r.getX(),r.getY()+cut);frame.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff112c3a),r.getTopLeft(),juce::Colour(0xff020a12),r.getBottomLeft(),false));g.fillPath(frame);
    g.saveState();g.reduceClipRegion(frame);
    g.setGradientFill(juce::ColourGradient(cyan4().withAlpha(.11f),r.getCentreX(),r.getCentreY(),juce::Colours::transparentBlack,r.getCentreX(),r.getBottom(),false));
    g.fillRect(r.reduced(3));g.restoreState();
    g.setColour(cyan4().withAlpha(.045f));g.strokePath(frame,juce::PathStrokeType(7));
    g.setColour(cyan4().withAlpha(.16f));g.strokePath(frame,juce::PathStrokeType(3));
    g.setColour(juce::Colour(0xff63c7d6).withAlpha(.62f));g.strokePath(frame,juce::PathStrokeType(.9f));
    g.setColour(cyan4().withAlpha(.88f));g.drawLine(r.getX()+17,r.getY(),r.getX()+64,r.getY(),1.6f);
    g.drawLine(r.getRight()-64,r.getBottom(),r.getRight()-17,r.getBottom(),1.6f);
    g.setColour(juce::Colour(0xff527988));g.drawLine(r.getX()+14,r.getCentreY()-8,r.getX()+26,r.getCentreY()-8,1);
    g.drawLine(r.getRight()-26,r.getCentreY()-8,r.getRight()-14,r.getCentreY()-8,1);
    const auto brandFont=juce::Font(juce::FontOptions("Bahnschrift",26,juce::Font::plain).withStyle("SemiBold"));
    juce::GlyphArrangement glyphs;glyphs.addFittedText(brandFont,"BLACK CIRCUIT",1204,53,354,34,juce::Justification::centred,1,.95f);
    juce::Path letters;glyphs.createPath(letters);const auto ink=letters.getBounds();
    g.setColour(cyan4().withAlpha(.11f));g.strokePath(letters,juce::PathStrokeType(4.2f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(cyan4().withAlpha(.30f));g.strokePath(letters,juce::PathStrokeType(1.8f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff0ffff),ink.getTopLeft(),juce::Colour(0xff86e9f4),ink.getBottomLeft(),false));g.fillPath(letters);
    g.saveState();g.reduceClipRegion(letters);
    g.setColour(juce::Colour(0xffd8ffff));g.fillRect(ink.getX(),ink.getY()+ink.getHeight()*.39f,ink.getWidth(),1.25f);
    g.setColour(cyan4().withAlpha(.45f));g.fillRect(ink.getX(),ink.getY()+ink.getHeight()*.72f,ink.getWidth(),.7f);g.restoreState();
    text4(g,"EDITION",{1210,91,342,18},13,juce::Colour(0xff78a4b4),"Semibold");
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
    auto r=b.getLocalBounds().toFloat().reduced(scale); const bool on=b.getToggleState()&&b.isEnabled();
    if (b.getName()=="power4") {
        g.setColour(juce::Colour(0xff020a0d)); g.fillEllipse(r); g.setColour(on?cyan4():juce::Colour(0xff64797b)); g.drawEllipse(r.reduced(scale),2*scale);
        const auto c=r.getCentre(); const float radius=r.getWidth()*.24f;juce::Path arc;arc.addCentredArc(c.x,c.y,radius,radius,0,.5f,juce::MathConstants<float>::twoPi-.5f,true);
        g.strokePath(arc,juce::PathStrokeType(3*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));g.drawLine(c.x,c.y-radius*1.35f,c.x,c.y,3*scale); return;
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(on?0xff123c4c:0xff162c3b),r.getTopLeft(),juce::Colour(0xff050c15),r.getBottomLeft(),false));
    g.fillRoundedRectangle(r,4*scale);g.setColour(on?cyan4().withAlpha(.9f):juce::Colour(0xff536d7b));g.drawRoundedRectangle(r,4*scale,on?1.6f*scale:scale);
    g.setColour(juce::Colour(0xffc8e7f2).withAlpha(on?.14f:.08f));g.drawRoundedRectangle(r.reduced(2*scale),3*scale,scale);
    if(over||down){g.setColour(cyan4().withAlpha(down?.11f:.045f));g.fillRoundedRectangle(r.reduced(scale),3*scale);}
    if(b.getName()=="presetPrevious4"||b.getName()=="presetNext4"){
        const float direction=b.getName()=="presetPrevious4"?-1.f:1.f;const auto centre=r.getCentre();
        juce::Path arrow;arrow.startNewSubPath(centre.x-direction*5*scale,centre.y-11*scale);
        arrow.lineTo(centre.x+direction*5*scale,centre.y);arrow.lineTo(centre.x-direction*5*scale,centre.y+11*scale);
        g.setColour(juce::Colour(0xffe4f2f7));g.strokePath(arrow,juce::PathStrokeType(2.8f*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));return;
    }
    if(b.getName()=="mirrorHorizontal4"||b.getName()=="mirrorVertical4"){
        const auto centre=juce::Point<float>(r.getX()+31*scale,r.getCentreY());const bool horizontal=b.getName()=="mirrorHorizontal4";
        const juce::Point<float> direction=horizontal?juce::Point<float>(1,0):juce::Point<float>(0,1),normal(-direction.y,direction.x);
        const auto a=centre-direction*12*scale,z=centre+direction*12*scale;
        juce::Path icon;icon.startNewSubPath(a);icon.lineTo(z);
        icon.startNewSubPath(a+direction*5*scale+normal*5*scale);icon.lineTo(a);icon.lineTo(a+direction*5*scale-normal*5*scale);
        icon.startNewSubPath(z-direction*5*scale+normal*5*scale);icon.lineTo(z);icon.lineTo(z-direction*5*scale-normal*5*scale);
        g.setColour(juce::Colour(0xffd4e7ef));g.strokePath(icon,juce::PathStrokeType(2*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));return;
    }
    if(b.getProperties().contains("moveCurveDirection4")) {
        const float direction=(int)b.getProperties()["moveCurveDirection4"]<0?-1.f:1.f;
        const auto centre=r.getCentre();const float half=9*scale;
        juce::Path arrow;arrow.startNewSubPath(centre.x-direction*half*.6f,centre.y-half);
        arrow.lineTo(centre.x+direction*half*.6f,centre.y);
        arrow.lineTo(centre.x-direction*half*.6f,centre.y+half);
        g.setColour(juce::Colour(0xffe3edf0));
        if((bool)b.getProperties()["moveCurveFine4"])
            g.strokePath(arrow,juce::PathStrokeType(2*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        else {arrow.closeSubPath();g.fillPath(arrow);}return;
    }
    if(b.getName()=="undoCurve4") {
        const auto centre=r.getCentre();const float radius=9*scale;
        juce::Path arc;arc.addCentredArc(centre.x,centre.y,radius,radius,0,-.65f,-5.75f,true);
        g.setColour(b.isEnabled()?juce::Colour(0xffe1e9eb):juce::Colour(0xff526c79));
        g.strokePath(arc,juce::PathStrokeType(1.8f*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        const juce::Point<float> tip(centre.x+std::sin(-5.75f)*radius,centre.y-std::cos(-5.75f)*radius),direction(-std::cos(-5.75f),-std::sin(-5.75f)),normal(-direction.y,direction.x);
        juce::Path head;head.startNewSubPath(tip);head.lineTo(tip-direction*5*scale+normal*3.3f*scale);
        head.lineTo(tip-direction*5*scale-normal*3.3f*scale);head.closeSubPath();g.fillPath(head);return;
    }
    if(b.getName()=="bandListen4") {
        const auto centre=r.getCentre().translated(0,-1.5f*scale);const float radius=9*scale;
        g.setColour(!b.isEnabled()?juce::Colour(0xff53666e):on?cyan4():juce::Colour(0xffdde8eb));
        juce::Path headphones;headphones.addCentredArc(centre.x,centre.y+2*scale,radius,radius,0,-juce::MathConstants<float>::halfPi,juce::MathConstants<float>::halfPi,true);
        g.strokePath(headphones,juce::PathStrokeType(2*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.fillRoundedRectangle(centre.x-radius-2*scale,centre.y,5*scale,10*scale,2*scale);
        g.fillRoundedRectangle(centre.x+radius-3*scale,centre.y,5*scale,10*scale,2*scale);return;
    }
    if(b.getProperties().contains("shape4")) {
        const int shape=(int)b.getProperties()["shape4"];
        auto source=processor?processor->getTemplateShapePreview(shape):OpenDuckProcessor::CurveSnapshot{};
        if(!processor){source.shape=shape;source.custom=false;source.count=0;source.modern3=true;source.template4=true;}
        source.position4=50;source.amount=100;
        const double seconds=processor?processor->cycleSeconds(0):.5;
        auto plot=r.reduced(15*scale,12*scale);juce::Path path;
        for(int i=0;i<=256;++i){const float t=i/256.f;
            const float gain=OpenDuckProcessor::effectiveCurveSample(source,t,seconds,0,0);
            const float y=plot.getBottom()-gain*plot.getHeight();
            if(i==0)path.startNewSubPath(plot.getX(),y);else path.lineTo(plot.getX()+t*plot.getWidth(),y);}
        g.setColour(on?cyan4():juce::Colour(0xffe9edef));g.strokePath(path,juce::PathStrokeType(2.4f*scale,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    }
}
void SingleDuckEditor::Look::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {
    if(b.getProperties().contains("shape4") || b.getProperties().contains("moveCurveDirection4") || b.getName()=="power4" || b.getName()=="undoCurve4" || b.getName()=="bandListen4" || b.getName()=="presetPrevious4" || b.getName()=="presetNext4")return;
    if(b.getName()=="preset4") {
        const auto r=b.getLocalBounds().toFloat();const auto content=b.getButtonText();const auto split=content.indexOf("    ");
        const auto fullName=(split>=0?content.substring(0,split):content);const int slash=fullName.indexOfChar('/');
        const auto name=(slash>=0?fullName.substring(0,slash):fullName).trim();
        const auto category=slash>=0?fullName.substring(slash+1).trim():juce::String{};
        text4(g,"PRESET",{12*scale,8*scale,r.getWidth()-24*scale,14*scale},16*scale,juce::Colour(0xffbac8ce));
        text4(g,name,{12*scale,(category.isEmpty()?29.f:26.f)*scale,r.getWidth()-126*scale,28*scale},25*scale,juce::Colours::white,"Semibold");
        if(category.isNotEmpty())text4(g,category,{12*scale,54*scale,r.getWidth()-126*scale,15*scale},12*scale,juce::Colour(0xff8ba8b7));
        text4(g,split>=0?content.substring(split+4):"",{r.getWidth()-110*scale,34*scale,98*scale,25*scale},17*scale,juce::Colour(0xffa5b8c0));return;
    }
    auto bounds=b.getLocalBounds().toFloat().reduced(4*scale);
    if(b.getName()=="mirrorHorizontal4"||b.getName()=="mirrorVertical4")bounds=bounds.withTrimmedLeft(45*scale);
    text4(g,b.getButtonText(),bounds,20*scale,b.isEnabled()?(b.getToggleState()?cyan4():juce::Colour(0xffe3edf3)):juce::Colour(0xff65767a),"Semibold");
}
void SingleDuckEditor::Look::drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
    float sliderPos,float minSliderPos,float maxSliderPos,juce::Slider::SliderStyle style,juce::Slider& slider){
    if(slider.getName()!="bandCutoff4"){
        juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,sliderPos,minSliderPos,maxSliderPos,style,slider);return;
    }
    const bool enabled=processor&&processor->value("bandEnable4")>.5f;
    const float cy=y+height*.5f,left=x+3*scale,right=x+width-3*scale;
    const auto rail=juce::Rectangle<float>(left,cy-5*scale,juce::jmax(1.f,right-left),10*scale);
    g.setColour(juce::Colour(0xff02080e));g.fillRoundedRectangle(rail.expanded(scale),3*scale);
    g.setColour(juce::Colour(0xff3b5565));g.drawRoundedRectangle(rail.expanded(scale),3*scale,scale);
    g.setColour(juce::Colour(0xff10232e));g.fillRoundedRectangle(rail,2*scale);
    const float at=juce::jlimit(left,right,sliderPos);
    const auto inner=rail.reduced(2*scale);
    g.setColour(enabled?cyan4().withAlpha(.82f):juce::Colour(0xff415b68));
    g.fillRoundedRectangle(inner.withWidth(juce::jmax(0.f,at-inner.getX())),1.5f*scale);
    const auto thumb=juce::Rectangle<float>(14*scale,20*scale).withCentre({at,cy});
    g.setColour(juce::Colours::black.withAlpha(.5f));g.fillRoundedRectangle(thumb.translated(0,2*scale).expanded(scale),2*scale);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd7e2e7),thumb.getTopLeft(),juce::Colour(0xff4f6877),thumb.getBottomLeft(),false));
    g.fillRoundedRectangle(thumb,2*scale);g.setColour(juce::Colour(0xffe4f0f4).withAlpha(.7f));g.drawRoundedRectangle(thumb,2*scale,scale);
    g.setColour(enabled?juce::Colour(0xff005a6c):juce::Colour(0xff263d4b));g.drawLine(at,cy-6*scale,at,cy+6*scale,1.5f*scale);
}
juce::Label* SingleDuckEditor::Look::createSliderTextBox(juce::Slider& slider){
    auto* label=juce::LookAndFeel_V4::createSliderTextBox(slider);
    if(slider.getName()=="bandCutoff4")label->setFont(juce::Font(juce::FontOptions("Segoe UI",18*scale,juce::Font::plain).withStyle("Semibold")));
    return label;
}
juce::Font SingleDuckEditor::Look::getPopupMenuFont(){
    return juce::Font(juce::FontOptions("Segoe UI",juce::jmax(14.f,16*scale),juce::Font::plain));
}
SingleDuckEditor::Graph::Graph(SingleDuckEditor& e):owner(e) {
    undoCurve.setName("undoCurve4");undoCurve.setTooltip("Annulla ultima modifica (Ctrl+Z)");
    undoCurve.onClick=[this]{owner.proc.undoTemplateEdit4();owner.restoreWaveView();owner.timerCallback();};
    addAndMakeVisible(undoCurve);undoCurve.addMouseListener(this,false);
}
void SingleDuckEditor::Graph::refreshUndo(){
    const bool available=owner.proc.canUndoTemplateEdit4();undoCurve.setEnabled(available);
    undoCurve.setTooltip(available?"Annulla "+owner.proc.getTemplateUndoLabel4()+" (Ctrl+Z)":"Nessuna modifica da annullare (Ctrl+Z)");
}
void SingleDuckEditor::Graph::resized(){
    const auto r=plot();const float s=owner.scale;
    undoCurve.setBounds(juce::Rectangle<float>(r.getRight()-44*s,r.getBottom()-44*s,38*s,38*s).toNearestInt());
}
void SingleDuckEditor::Graph::updatePointer(bool inside){
    if(pointerInside!=inside){pointerInside=inside;repaint();}
}
void SingleDuckEditor::Graph::mouseEnter(const juce::MouseEvent& e){
    updatePointer(getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
}
void SingleDuckEditor::Graph::mouseMove(const juce::MouseEvent& e){
    updatePointer(getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
}
void SingleDuckEditor::Graph::mouseExit(const juce::MouseEvent& e){
    updatePointer(e.eventComponent!=this&&getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
}
SingleDuckEditor::SingleDuckEditor(OpenDuckProcessor& p):AudioProcessorEditor(p),graph(*this),proc(p) {
    setLookAndFeel(&look);look.processor=&proc;setOpaque(true);setWantsKeyboardFocus(true);
    int n=0; if(auto* data=BinaryData::getNamedResource("TemplateReference_png",n))reference=juce::ImageFileFormat::loadFrom(data,(size_t)n);
    for(size_t i=0;i<3;++i){auto& k=knobs[i];k.setName(ids4[i]);
        k.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);k.setDoubleClickReturnValue(true,0);
        k.onDragStart=[this,i]{if(isShowing())grabKeyboardFocus();proc.beginTemplateEdit4(i==0?"MIX":i==1?"PHASE":"CURVE");};
        k.onDragEnd=[this]{proc.commitTemplateEdit4();timerCallback();};
        addAndMakeVisible(k);attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,ids4[i],k);
        auto& label=readouts[i];label.setName(juce::String(ids4[i])+"Value");label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId,cyan4());label.setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);
        label.setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
        label.setColour(juce::Label::backgroundWhenEditingColourId,juce::Colour(0xff061015));
        label.setEditable(false,true);label.onTextChange=[this,i]{if(readouts[i].isBeingEdited())return;performEdit(i==0?"Valore MIX":i==1?"Valore PHASE":"Valore CURVE",[this,i]{set(ids4[i],readouts[i].getText().replaceCharacter(',','.').getFloatValue());});};
        addAndMakeVisible(label);
    }
    for(size_t i=0;i<3;++i) knobs[i].onValueChange=[this,i]{
        if(!readouts[i].isBeingEdited())readouts[i].setText(percent4(knobs[i].getValue(),i==1),juce::dontSendNotification);
        graph.repaint();if(i!=0)for(auto& shape:shapes)shape.repaint();
    };
    knobs[0].setTooltip("MIX: profondita del ducking, 0..100%. Trascina verticalmente; Shift = regolazione fine. POWER deve essere acceso. INIT: 0%, Power spento.");
    knobs[1].setTooltip("PHASE: durata attorno al centro, -50..+50%. Trascina verticalmente; Shift = regolazione fine. Profondita e posizione indipendenti.");
    knobs[2].setTooltip("CURVE: solo curvatura. Trascina verticalmente; Shift = regolazione fine. Posizione, durata e profondita indipendenti.");
    for(int i=0;i<16;++i){auto& b=shapes[(size_t)i];b.setName("shape4_"+juce::String(i));b.getProperties().set("shape4",i);
        b.setTooltip("Forma completa con Shift/Slope; il grafico applica anche MIX");
        b.onClick=[this,i]{performEdit("Forma "+juce::String(i+1),[this,i]{proc.selectFactoryShape(i,0);});};addAndMakeVisible(b);}
    const char* names[]={"1/8","1/4","1/2","1/1"};const int indices[]={3,2,1,0};
    for(int i=0;i<4;++i){auto& b=rates[(size_t)i];b.setButtonText(names[i]);b.onClick=[this,i,indices]{performEdit("Durata ciclo",[this,i,indices]{proc.setSectorParameter(0,"speedMode",0);proc.setSectorParameter(0,"speed",(float)indices[i]);});};addAndMakeVisible(b);}
    const char* triggerNames[]={"SYNC","MIDI","AUDIO"};const int triggers4[]={0,2,1};
    for(int i=0;i<3;++i){auto& b=triggers[(size_t)i];b.setButtonText(triggerNames[i]);b.onClick=[this,i,triggers4]{performEdit("Trigger",[this,i,triggers4]{proc.setSectorParameter(0,"trigger",(float)triggers4[i]);});};addAndMakeVisible(b);}
    triggers[2].setTooltip("External sidechain kick. Route the source track using Ableton's device sidechain chooser. Right-click the panel for detector settings.");
    for(auto* b:{&previous,&next,&preset,&power,&mirrorH,&mirrorV})addAndMakeVisible(*b);
    for(int i=0;i<4;++i){auto& arrow=moveArrows[(size_t)i];arrow.setName("moveCurveArrow4_"+juce::String(i));
        arrow.getProperties().set("moveCurveDirection4",i<2?-1:1);arrow.getProperties().set("moveCurveFine4",i==1||i==2);
        arrow.setTooltip(i==1||i==2?"Move Curve (Fine)":"Move Curve");
        arrow.onClick=[this,i]{performEdit("MOVE curva",[this,i]{proc.shiftTemplateCurve(i<2?-1:1,i==1||i==2);});};
        addAndMakeVisible(arrow);}
    const char* viewNames[]={"AUTO","1 CURVA","LOOP"};
    for(int i=0;i<3;++i){auto& button=curveViews[(size_t)i];button.setName("curveView4_"+juce::String(i));button.setButtonText(viewNames[i]);
        button.onClick=[this,i]{performEdit("Vista curva",[this,i]{curveView=i;saveWaveView();});};addAndMakeVisible(button);}
    curveViews[0].setTooltip("AUTO: una curva per un colpo o suono sostenuto; loop con piu attacchi reali. Cambio stabilizzato su acquisizioni complete.");
    curveViews[1].setTooltip("1 CURVA: un intero ciclo dell'inviluppo, con la porzione audio reale corrispondente.");
    curveViews[2].setTooltip("LOOP: l'intera finestra audio acquisita; zoom e scorrimento disponibili.");
    bandEnable.setName("bandEnable4");bandListen.setName("bandListen4");bandCutoff.setName("bandCutoff4");
    addAndMakeVisible(bandEnable);addAndMakeVisible(bandListen);addAndMakeVisible(bandCutoff);
    bandEnable.onClick=[this]{performEdit("BAND",[this]{set("bandEnable4",proc.value("bandEnable4")>.5f?0.f:1.f);});};
    bandListen.onClick=[this]{performEdit("Ascolto BAND",[this]{set("bandListen4",proc.value("bandListen4")>.5f?0.f:1.f);});};
    bandCutoff.setSliderStyle(juce::Slider::LinearHorizontal);bandCutoff.setTextBoxStyle(juce::Slider::TextBoxRight,false,100,28);
    bandCutoff.setNumDecimalPlacesToDisplay(0);
    bandCutoff.setColour(juce::Slider::trackColourId,cyan4());bandCutoff.setColour(juce::Slider::thumbColourId,juce::Colour(0xffe1edf0));
    bandCutoff.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffcbdbe0));bandCutoff.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff071116));
    bandCutoff.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    bandCutoff.onDragStart=[this]{if(isShowing())grabKeyboardFocus();proc.beginTemplateEdit4("Crossover BAND");};
    bandCutoff.onDragEnd=[this]{proc.commitTemplateEdit4();timerCallback();};
    bandCutoffAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"bandCutoff4",bandCutoff);
    bandEnable.setTooltip("BAND: apply ducking only below the crossover; the high band stays unchanged.");
    bandCutoff.setTooltip("BAND crossover frequency, 20-5120 Hz.");bandListen.setTooltip("Headphones: audition only the processed low band while BAND is on.");
    preset.setName("preset4");previous.setName("presetPrevious4");next.setName("presetNext4");
    mirrorH.setName("mirrorHorizontal4");mirrorV.setName("mirrorVertical4");preset.onClick=[this]{popupPresets();};
    previous.onClick=[this]{selectPreset((proc.currentBehaviorPresetId(0)+699)%700);};next.onClick=[this]{selectPreset((proc.currentBehaviorPresetId(0)+1)%700);};
    power.onClick=[this]{performEdit("POWER",[this]{set("bypass",proc.value("bypass")>.5f?0.f:1.f);});};
    power.setName("power4");
    power.setTooltip("Power: enable ducking. New INIT opens OFF with unattenuated, latency-aligned audio. The input waveform remains visible while OFF.");
    mirrorH.onClick=[this]{performEdit("Mirror H",[this]{proc.transformCurve(OpenDuckProcessor::CurveOperation::mirrorHorizontal,0);});};
    mirrorV.onClick=[this]{performEdit("Mirror V",[this]{proc.transformCurve(OpenDuckProcessor::CurveOperation::mirrorVertical,0);});};
    addAndMakeVisible(graph);addChildComponent(mini);mini.setOpaque(false);mini.addAndMakeVisible(miniTitle);mini.addAndMakeVisible(close);
    close.onClick=[this]{settingsOpen=false;mini.setVisible(false);repaint();};
    const char* settingIds[]={"threshold","hysteresis","holdoff","smooth"};const char* settingNames[]={"Soglia / dB","Isteresi / dB","Retrigger / ms","Smoothing / ms"};
    for(size_t i=0;i<4;++i){auto& slider=settings[i];slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,24);
        slider.onDragStart=[this,i]{if(isShowing())grabKeyboardFocus();proc.beginTemplateEdit4(settingsLabels[i].getText());};
        slider.onDragEnd=[this]{proc.commitTemplateEdit4();timerCallback();};
        mini.addAndMakeVisible(slider);settingsLabels[i].setText(settingNames[i],juce::dontSendNotification);mini.addAndMakeVisible(settingsLabels[i]);
        settingAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,OpenDuckProcessor::sectorParameterId(settingIds[i],0),slider);}
    routing.setName("sidechainStatus4");routing.setJustificationType(juce::Justification::topLeft);mini.addAndMakeVisible(routing);
    antiClick.setName("antiClick4");mini.addAndMakeVisible(antiClick);mini.addAndMakeVisible(latencyInfo);
    antiClick.onClick=[this]{performEdit("Anti-click",[this]{set("antiClick4",proc.value("antiClick4")>.5f?0.f:1.f);});};
    antiClick.setTooltip("Protezione dai salti di guadagno, anche in SYNC (raccordo 2 ms senza latenza). MIDI/AUDIO: protezione retrigger e latenza indicata accanto.");
    latencyInfo.setJustificationType(juce::Justification::centredLeft);
    triggerLoop.setName("triggerLoop4");mini.addAndMakeVisible(triggerLoop);
    triggerLoop.onClick=[this]{performEdit("Loop del trigger",[this]{proc.setSectorParameter(0,"loop",proc.value(OpenDuckProcessor::sectorParameterId("loop",0))>.5f?0.f:1.f);});};
    triggerLoop.setTooltip("MIDI/AUDIO: ONE SHOT esegue una curva per trigger; LOOP la ripete fino al trigger successivo. Questa scelta e indipendente dalla vista del grafico.");
    miniTitle.setJustificationType(juce::Justification::centredLeft);
    waveSource=juce::jlimit(0,2,(int)proc.state.state.getProperty("templateWaveSource",0));
    const bool legacySingle=(bool)proc.state.state.getProperty("templateWaveSingle",false);
    waveSingle=(bool)proc.state.state.getProperty("templateWaveMaskSingle4",false);
    curveView=proc.state.state.hasProperty("templateCurveView4")?juce::jlimit(0,2,(int)proc.state.state.getProperty("templateCurveView4")):(legacySingle?1:0);
    proc.state.state.setProperty("templateCurveView4",curveView,nullptr);
    waveZoom=juce::jlimit(1.f,8.f,(float)proc.state.state.getProperty("templateWaveZoom",1.f));
    waveOffset=juce::jlimit(0.f,1.f-1.f/waveZoom,(float)proc.state.state.getProperty("templateWaveOffset",0.f));
    graph.setName("duckGraph4");graph.setTooltip("La guida SLOPE appare con il puntatore nel grafico. Trascina la zona ciano per la pendenza, fuori dalla zona per spostare la curva. Shift + trascina: scorri l'onda ingrandita. VISTA: sorgente, loop e detector.");
    setResizable(true,false);setResizeLimits(864,456,2592,1368);getConstrainer()->setFixedAspectRatio(1728.0/912);
    resizeGrip=std::make_unique<juce::ResizableCornerComponent>(this,getConstrainer());addAndMakeVisible(*resizeGrip);
    const int width=juce::jlimit(864,2592,(int)proc.state.state.getProperty("templateWidth",1296));setSize(width,juce::roundToInt(width*912.f/1728));
    timerCallback();startTimerHz(60);
}
SingleDuckEditor::~SingleDuckEditor(){proc.commitTemplateEdit4();stopTimer();setLookAndFeel(nullptr);}
juce::Rectangle<int> SingleDuckEditor::box(float x,float y,float w,float h)const{return {juce::roundToInt(x*scale),juce::roundToInt(y*scale),juce::roundToInt(w*scale),juce::roundToInt(h*scale)};}
void SingleDuckEditor::set(const juce::String& id,float v){if(auto* p=proc.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}}
void SingleDuckEditor::performEdit(const juce::String& label,const std::function<void()>& action){
    if(isShowing())grabKeyboardFocus();proc.beginTemplateEdit4(label);action();proc.commitTemplateEdit4();timerCallback();
}
bool SingleDuckEditor::keyPressed(const juce::KeyPress& key){
    if(key.getModifiers().isCtrlDown()&&!key.getModifiers().isShiftDown()&&!key.getModifiers().isAltDown()&&(key.getKeyCode()=='z'||key.getKeyCode()=='Z')){
        if(proc.canUndoTemplateEdit4())proc.undoTemplateEdit4();restoreWaveView();timerCallback();return true;
    }
    return false;
}
void SingleDuckEditor::resized(){
    scale=getWidth()/1728.f;look.scale=scale;mini.scale=scale;proc.state.state.setProperty("templateWidth",getWidth(),nullptr);
    knobs[0].setBounds(box(69,150,398,390));knobs[1].setBounds(box(70,619,166,160));knobs[2].setBounds(box(291,619,166,160));
    readouts[0].setBounds(box(197,562,142,41));readouts[1].setBounds(box(91,809,124,42));readouts[2].setBounds(box(305,809,138,42));
    for(auto& l:readouts)l.setFont(premium::font(29*scale));
    previous.setBounds(box(539,50,76,68));preset.setBounds(box(627,48,440,73));next.setBounds(box(1077,50,77,68));
    power.setBounds(box(1587,42,88,88));
    for(int i=0;i<4;++i)rates[(size_t)i].setBounds(box(516+i*118.f,170,112,54));
    triggers[0].setBounds(box(1050,170,112,54));triggers[1].setBounds(box(1169,170,111,54));triggers[2].setBounds(box(1294,170,125,54));
    graph.setBounds(box(515,244,1141,300));
    moveArrows[0].setBounds(box(518,550,96,34));moveArrows[1].setBounds(box(622,550,96,34));
    moveArrows[2].setBounds(box(1445,550,96,34));moveArrows[3].setBounds(box(1553,550,96,34));
    for(int i=0;i<3;++i)curveViews[(size_t)i].setBounds(box(856+i*152.f,550,152,34));
    for(int i=0;i<16;++i)shapes[(size_t)i].setBounds(box(518+(i%8)*144.f,590+(i/8)*86.f,128,72));
    mirrorH.setBounds(box(516,770,228,67));mirrorV.setBounds(box(756,770,228,67));
    bandEnable.setBounds(box(1004,778,112,51));bandCutoff.setBounds(box(1128,778,428,51));bandListen.setBounds(box(1566,778,78,51));
    bandCutoff.setTextBoxStyle(juce::Slider::TextBoxRight,false,juce::roundToInt(100*scale),juce::roundToInt(28*scale));
    mini.setBounds(box(1030,236,623,333));miniTitle.setBounds(box(18,10,525,33));close.setBounds(box(555,10,48,33));
    for(int i=0;i<4;++i){settingsLabels[(size_t)i].setBounds(box(18,52+i*40.f,160,33));settings[(size_t)i].setBounds(box(181,52+i*40.f,423,33));}
    triggerLoop.setBounds(box(18,216,174,33));antiClick.setBounds(box(204,216,174,33));latencyInfo.setBounds(box(394,216,210,33));
    routing.setBounds(box(18,260,585,62));miniTitle.setFont(juce::Font(juce::FontOptions("Segoe UI",19*scale,juce::Font::plain)));routing.setFont(juce::Font(juce::FontOptions("Segoe UI",14*scale,juce::Font::plain)));
    latencyInfo.setFont(juce::Font(juce::FontOptions("Segoe UI",15*scale,juce::Font::plain)));
    if(resizeGrip)resizeGrip->setBounds(box(1625,854,40,32));
}
void SingleDuckEditor::paint(juce::Graphics& graphics){
    auto& g=graphics;g.addTransform(juce::AffineTransform::scale(scale));
    if(reference.isValid())g.drawImage(reference,{0,0,1728,912});else g.fillAll(juce::Colour(0xff9a9b96));
    // All dynamic source pixels are covered by live components/opaque backplates.
    readoutPlate4(g,{192,559,152,47});readoutPlate4(g,{86,806,134,48});readoutPlate4(g,{300,806,148,48});
    premium::panel(g,{529,42,635,82},8);
    circuitBadge4(g);
    g.setColour(juce::Colour(0xff03090b));g.fillRect(509,157,919,78);
    g.setColour(juce::Colour(0xffa0aaaa));g.drawLine(1015,174,1015,216,1.2f);

    g.setColour(juce::Colour(0xff050b0e));g.fillRect(1445,164,215,65);
    text4(g,juce::String(proc.bpm.load(),1)+" BPM",{1450,172,205,49},28);
    g.setColour(juce::Colour(0xff060d11));g.fillRect(514,544,1143,223);
    for(int i=0;i<16;++i)text4(g,juce::String(i+1),{518+(i%8)*144.f,661+(i/8)*86.f,128,17},16);
    g.setColour(juce::Colour(0xff060d11));g.fillRect(514,768,1143,72);premium::panel(g,{996,770,657,67},5);

}
juce::String SingleDuckEditor::activityLabel4()const{
    if(proc.value("bypass")>.5f)return "POWER OFF";
    if(proc.value("on0")<.5f)return "DUCK OFF";
    if(proc.value("duckMix4")<.0001f)return "MIX 0% / DRY";
    const int trigger=int(proc.value("v3Trigger0"));
    if(trigger==0)return "SYNC";
    const auto received=proc.templateTriggerCount4.load(std::memory_order_relaxed);
    if(trigger==2)return received>0?"MIDI / GAIN REALE":"ATTESA MIDI";
    if(!proc.templateSidechainEnabled4.load(std::memory_order_relaxed))return "SC NON COLLEGATO";
    if(received>0)return "AUDIO / GAIN REALE";
    const float peak=proc.templateSidechainPeak4.load(std::memory_order_relaxed);
    if(peak<1.e-7f)return "ATTESA KICK";
    const float detector=proc.templateDetectorPeak4.load(std::memory_order_relaxed);
    const float threshold=juce::Decibels::decibelsToGain(proc.value(OpenDuckProcessor::sectorParameterId("threshold",0)));
    return detector<threshold?"SC SOTTO SOGLIA":"ATTESA TRIGGER";
}
void SingleDuckEditor::timerCallback(){
    curveView=juce::jlimit(0,2,(int)proc.state.state.getProperty("templateCurveView4",curveView));
    for(int i=0;i<3;++i)curveViews[(size_t)i].setToggleState(curveView==i,juce::dontSendNotification);
    for(size_t i=0;i<3;++i)if(!readouts[i].isBeingEdited())readouts[i].setText(percent4(knobs[i].getValue(),i==1),juce::dontSendNotification);
    const auto curve=proc.getCurveSnapshot(0);for(int i=0;i<16;++i)shapes[(size_t)i].setToggleState(!curve.custom&&curve.shape==i,juce::dontSendNotification);
    const int speed=(int)proc.value(OpenDuckProcessor::sectorParameterId("speed",0));const int values[]={3,2,1,0};for(int i=0;i<4;++i)rates[(size_t)i].setToggleState(speed==values[i],juce::dontSendNotification);
    const int trigger=(int)proc.value(OpenDuckProcessor::sectorParameterId("trigger",0));const int choices[]={0,2,1};for(int i=0;i<3;++i)triggers[(size_t)i].setToggleState(trigger==choices[i],juce::dontSendNotification);
    power.setToggleState(proc.value("bypass")<.5f,juce::dontSendNotification);
    bandEnable.setToggleState(proc.value("bandEnable4")>.5f,juce::dontSendNotification);
    bandListen.setToggleState(proc.value("bandListen4")>.5f,juce::dontSendNotification);
    bandListen.setEnabled(proc.value("bandEnable4")>.5f);
    antiClick.setToggleState(proc.value("antiClick4")>.5f,juce::dontSendNotification);
    const bool measured=curve.profileModel4==1,triggered=trigger!=0;
    if(measured!=settingsMeasured){
        settingsMeasured=measured;settingAttachments[3].reset();
        settingAttachments[3]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,OpenDuckProcessor::sectorParameterId(measured?"end":"smooth",0),settings[3]);
        settingsLabels[3].setText(measured?"Fine curva / %":"Smoothing / ms",juce::dontSendNotification);
        settings[3].setTooltip(measured?"Fine della curva ONE SHOT: il guadagno finale resta mantenuto fino al trigger successivo.":"Smoothing dell'inviluppo.");
        if(measured){settings[3].textFromValueFunction=[](double v){return juce::String(v*100,1)+" %";};
            settings[3].valueFromTextFunction=[](const juce::String& t){return t.replaceCharacter(',','.').getDoubleValue()*.01;};}
    }
    const bool looping=proc.value(OpenDuckProcessor::sectorParameterId("loop",0))>.5f;
    triggerLoop.setButtonText(looping?"LOOP":"ONE SHOT");triggerLoop.setToggleState(looping,juce::dontSendNotification);
    triggerLoop.setEnabled(measured&&triggered);antiClick.setEnabled(measured);
    settings[3].setEnabled(!measured||(triggered&&!looping));
    const auto sr=proc.getSampleRate();
    latencyInfo.setText(juce::String(sr>0?1000.0*proc.getLatencySamples()/sr:0,2)+" ms",juce::dontSendNotification);
    latencyInfo.setTooltip("Latenza effettivamente comunicata alla DAW.");
    const bool sideEnabled=proc.templateSidechainEnabled4.load(std::memory_order_relaxed);
    const float sidePeak=proc.templateSidechainPeak4.load(std::memory_order_relaxed);
    const auto sideLevel=!sideEnabled?juce::String("non collegato"):sidePeak<1.e-7f?juce::String("silenzio"):juce::String(juce::Decibels::gainToDecibels(sidePeak),1)+" dBFS";
    routing.setText("SIDECHAIN: "+sideLevel+" / TRIGGER: "+juce::String((juce::int64)proc.templateTriggerCount4.load(std::memory_order_relaxed))
        +"\n"+activityLabel4()+"\nAbleton: Audio From > traccia kick > punto di prelievo.",juce::dontSendNotification);
    const int storedId=proc.currentBehaviorPresetId(0),id=juce::jlimit(0,699,storedId);const auto& factory=OpenDuckProcessor::factoryPresets()[(size_t)id];
    const float factoryLength=id==0?1.f:.4f+.14f*(id%4)+.02f*(id/100);
    const bool modified=curve.custom||curve.shape!=factory.shape||std::abs(curve.tension-factory.tension)>.001f||std::abs(curve.amount-factory.amount)>.001f||std::abs(curve.length-factoryLength)>.001f||std::abs(proc.value("duckMix4")-(id==0?0.f:100.f))>.001f||std::abs(proc.value("duration4"))>.001f||std::abs(proc.value("moveCurve4")-(id==0?0.f:factory.phase))>.001f||std::abs(proc.value("slope4"))>.001f;
    preset.setButtonText((storedId<0?juce::String("Custom Shape"):OpenDuckProcessor::templatePresetName(id)+(modified?" *":""))+"    "+(storedId<0?juce::String("---"):juce::String(id+1).paddedLeft('0',3))+" / 700");
    graph.refreshUndo();if(++thumbnailRefreshDivider%2==0)for(auto& shape:shapes)shape.repaint();graph.repaint();
}
void SingleDuckEditor::showSettings(){settingsOpen=true;miniTitle.setText("SIDECHAIN / DETECTOR",juce::dontSendNotification);for(auto& slider:settings)slider.setVisible(true);for(auto& label:settingsLabels)label.setVisible(true);routing.setVisible(true);mini.setVisible(true);mini.toFront(false);repaint();}
void SingleDuckEditor::mouseDown(const juce::MouseEvent& e){if(box(1179,36,404,94).contains(e.getPosition()))return;if(e.mods.isPopupMenu())showSettings();}
void SingleDuckEditor::selectPreset(int id){
    performEdit("Preset "+juce::String(id+1),[this,id]{proc.applyTemplatePreset(id);});
}
void SingleDuckEditor::popupPresets(){juce::PopupMenu menu;for(int cat=0;cat<7;++cat){juce::PopupMenu section;for(int n=0;n<100;++n){int id=cat*100+n;section.addItem(id+1,juce::String(id+1).paddedLeft('0',3)+"  "+OpenDuckProcessor::templatePresetName(id));}menu.addSubMenu(OpenDuckProcessor::factoryCategoryNames()[(size_t)cat],section);}juce::Component::SafePointer<SingleDuckEditor> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(preset),[safe](int id){if(safe&&id>0)safe->selectPreset(id-1);});}
juce::Rectangle<float> SingleDuckEditor::Graph::plot()const{return getLocalBounds().toFloat().withTrimmedLeft(65*owner.scale).withTrimmedTop(5*owner.scale).withTrimmedBottom(36*owner.scale).withTrimmedRight(7*owner.scale);}
bool SingleDuckEditor::Graph::triggeredTimeline()const{
    return owner.proc.value("profileModel4")>.5f&&owner.proc.value("v3Trigger0")>.5f;
}
OpenDuckProcessor::ScopeFrame SingleDuckEditor::Graph::scopeFrame()const{
    auto overview=owner.proc.getInputScopeFrame(triggeredTimeline());
    if(triggeredTimeline()&&!viewWindow(overview).loop){
        auto single=owner.proc.getScopeFrame(0);
        if(single.hasEnvelopeTimeline&&single.envelopeTriggerCapture&&single.bins>0)return single;
    }
    return overview;
}
SingleDuckEditor::Graph::ViewWindow SingleDuckEditor::Graph::viewWindow(const OpenDuckProcessor::ScopeFrame& frame)const{
    if((moving||sloping)&&dragWindow.valid)return dragWindow;
    owner.curveView=juce::jlimit(0,2,(int)owner.proc.state.state.getProperty("templateCurveView4",owner.curveView));
    const double period=frame.bins>0?frame.periodSeconds:owner.proc.getInputScopeBeats()*60/owner.proc.bpm.load();
    // Classify complete physical captures; painting the live trace must not
    // turn partial frames or repeated repaints into fresh onset evidence.
    const auto classifier=triggeredTimeline()?owner.proc.getInputScopeFrame():frame;
    const auto& lo=owner.waveSource==1?classifier.sideMin:owner.waveSource==2?classifier.outMin:classifier.mainMin;
    const auto& hi=owner.waveSource==1?classifier.sideMax:owner.waveSource==2?classifier.outMax:classifier.mainMax;
    viewDecision=viewSelector.observe(static_cast<odx::view::CurveViewMode>(owner.curveView),classifier.generation,classifier.complete,
        classifier.firstBin,classifier.bins,lo,hi,classifier.periodSeconds,owner.waveSource);
    if(triggeredTimeline()&&frame.envelopeTriggerCapture){
        const double cycle=frame.envelopeCycleSeconds>0?frame.envelopeCycleSeconds:period;
        return odx::view::curveViewWindow(false,period,cycle,0,-1,0,frame.bins);
    }
    const int onset=viewDecision.generation==frame.generation?viewDecision.onsetBin:-1;
    return odx::view::curveViewWindow(viewDecision.loop,period,owner.proc.cycleSeconds(0),frame.bins>0?frame.curvePhaseStart:0,
        onset,frame.firstBin,frame.bins,owner.waveOffset,owner.waveZoom);
}
std::vector<juce::Rectangle<float>> SingleDuckEditor::Graph::slopeRegions()const{
    const auto frame=scopeFrame();return slopeRegions(viewWindow(frame),&frame);
}
std::vector<juce::Rectangle<float>> SingleDuckEditor::Graph::slopeRegions(const ViewWindow& window,const OpenDuckProcessor::ScopeFrame* captured)const{
    std::vector<juce::Rectangle<float>> result;
    if(!guideVisible()||!window.valid)return result;
    const auto r=plot();const auto curve=owner.proc.getCurveSnapshot(0);const double seconds=window.cycleSeconds;
    const double viewStart=window.cycleAtDisplay(0),viewEnd=window.cycleAtDisplay(1);
    auto source=curve.profileModel4==1?odx::template4::slopeTargetBounds(curve.shape,curve.slope4,1):odx::template4::slopeBounds(curve.shape,0);
    if(curve.profileModel4==1&&curve.mirrorH4)source={1-source.end,1-source.start};
    const double begin=OpenDuckProcessor::rawCurveTimeToCycle(curve,source.start,seconds);
    const double end=OpenDuckProcessor::rawCurveTimeToCycle(curve,source.end,seconds);
    if(end<=begin||viewEnd<=viewStart)return result;
    if(triggeredTimeline()&&captured&&captured->hasEnvelopeTimeline&&!captured->envelopeTriggerCapture){
        // AUDIO/MIDI resets need their actual origins, rather than a grid of
        // invented periodic repeats. Keep exactly one selectable guide.
        std::vector<double> origins;
        const double cycle=captured->envelopeCycleSeconds;
        if(cycle<=0)return result;
        for(size_t i=0;i<512;++i)if(captured->envelopeValid[i]&&captured->envelopeActive[i]){
            const double origin=captured->envelopeTimeSeconds[i]-double(captured->envelopePhase[i])*cycle;
            if(origins.empty()||std::abs(origin-origins.back())>juce::jmax(1.e-5,std::abs(origin)*1.e-6))origins.push_back(origin);
        }
        const double left=window.startSeconds,right=left+window.durationSeconds,centre=(left+right)*.5;
        const bool repeats=owner.proc.value("v3Loop0")>.5f;
        bool chosen=false,fullChosen=false;double bestWidth=0,bestDistance=0;juce::Rectangle<float> selected;
        for(size_t event=0;event<origins.size();++event){
            const double origin=origins[event],stop=event+1<origins.size()?origins[event+1]:right;
            const int first=repeats?juce::jmax(0,int(std::floor((left-origin)/cycle))-1):0;
            const int last=repeats?juce::jmax(first,int(std::ceil((juce::jmin(right,stop)-origin)/cycle))):0;
            for(int repeat=first;repeat<=last;++repeat){
                const double a=origin+(begin+repeat)*cycle,b=juce::jmin(origin+(end+repeat)*cycle,stop);
                const double lo=juce::jmax(left,a),hi=juce::jmin(right,b);if(hi<=lo)continue;
                const bool full=a>=left-1.e-7&&b<=right+1.e-7;
                const double width=hi-lo,distance=std::abs((lo+hi)*.5-centre);
                if(chosen&&!(full?(!fullChosen||distance<bestDistance):(!fullChosen&&(width>bestWidth+1.e-7||(std::abs(width-bestWidth)<1.e-7&&distance<bestDistance)))))continue;
                selected={r.getX()+float((lo-left)/window.durationSeconds)*r.getWidth(),r.getY(),float(width/window.durationSeconds)*r.getWidth(),r.getHeight()};
                chosen=true;fullChosen=full;bestWidth=width;bestDistance=distance;
            }
        }
        if(chosen)result.push_back(selected);return result;
    }
    const int first=int(std::floor(viewStart-end))-1,last=int(std::ceil(viewEnd-begin))+1;
    const double centre=(viewStart+viewEnd)*.5;
    bool chosen=false,fullWindow=false;double bestWidth=0,bestDistance=0;
    juce::Rectangle<float> selected;
    for(int repeat=first;repeat<=last;++repeat){
        const double lo=juce::jmax(viewStart,begin+repeat),hi=juce::jmin(viewEnd,end+repeat);
        if(hi<=lo)continue;
        const bool full=begin+repeat>=viewStart-1.e-7&&end+repeat<=viewEnd+1.e-7;
        const double widthInCycles=hi-lo,distance=std::abs((lo+hi)*.5-centre);
        const bool better=full?(!fullWindow||distance<bestDistance-1.e-7):
            (!fullWindow&&(!chosen||widthInCycles>bestWidth+1.e-7||
                (std::abs(widthInCycles-bestWidth)<1.e-7&&distance<bestDistance-1.e-7)));
        if(!better)continue;
        const float x=r.getX()+float((lo-viewStart)/(viewEnd-viewStart))*r.getWidth();
        const float width=float((hi-lo)/(viewEnd-viewStart))*r.getWidth();
        selected={x,r.getY(),width,r.getHeight()};chosen=true;fullWindow=full;
        bestWidth=widthInCycles;bestDistance=distance;
    }
    if(chosen)result.push_back(selected);
    return result;
}
void SingleDuckEditor::saveWaveView(){
    auto& state=proc.state.state;state.setProperty("templateWaveSource",waveSource,nullptr);
    state.setProperty("templateWaveMaskSingle4",waveSingle,nullptr);state.setProperty("templateWaveZoom",waveZoom,nullptr);
    state.setProperty("templateWaveOffset",waveOffset,nullptr);state.setProperty("templateCurveView4",curveView,nullptr);graph.repaint();
}
void SingleDuckEditor::restoreWaveView(){
    const int oldSource=waveSource,oldView=curveView;
    const auto& state=proc.state.state;waveSource=juce::jlimit(0,2,(int)state.getProperty("templateWaveSource",0));
    waveSingle=(bool)state.getProperty("templateWaveMaskSingle4",false);
    curveView=juce::jlimit(0,2,(int)state.getProperty("templateCurveView4",0));
    waveZoom=juce::jlimit(1.f,8.f,(float)state.getProperty("templateWaveZoom",1.f));
    waveOffset=juce::jlimit(0.f,1.f-1.f/waveZoom,(float)state.getProperty("templateWaveOffset",0.f));
    proc.setInputScopeBeats((double)state.getProperty("templateWaveBeats",4.0));
    if(oldSource!=waveSource||oldView!=curveView)graph.resetViewClassifier();
    const int width=juce::jlimit(864,2592,(int)state.getProperty("templateWidth",getWidth()));
    if(width!=getWidth())setSize(width,juce::roundToInt(width*912.f/1728));
}
void SingleDuckEditor::Graph::showViewMenu(){
    juce::PopupMenu menu,source,span,zoom;
    const bool loop=viewWindow(scopeFrame()).loop;
    const char* sources[]={"Ingresso del canale (default)","Sidechain esterno","Uscita processata"};
    for(int i=0;i<3;++i)source.addItem(10+i,sources[i],true,owner.waveSource==i);
    for(int i=0;i<5;++i){const int beats=1<<i;span.addItem(20+i,juce::String(beats)+(beats==1?" beat":" beat"),true,owner.proc.getInputScopeBeats()==beats);}
    for(int i=0;i<4;++i)zoom.addItem(30+i,juce::String(1<<i)+"x",loop,owner.waveZoom==float(1<<i));
    menu.addSectionHeader("ONDA REALE / VISTA");menu.addSubMenu("Sorgente",source);
    menu.addItem(1,"Onda completa nella vista",true,!owner.waveSingle);menu.addItem(2,"Isola il primo colpo (solo onda)",true,owner.waveSingle);
    menu.addSubMenu("Finestra temporale",span);menu.addSubMenu("Ingrandimento",zoom);
    menu.addItem(3,"Mostra tutta la finestra");
    menu.addItem(5,"Scorri porzione a sinistra",loop&&owner.waveZoom>1);
    menu.addItem(6,"Scorri porzione a destra",loop&&owner.waveZoom>1);
    menu.addItem(7,"Centra porzione ingrandita",loop&&owner.waveZoom>1);
    menu.addSeparator();menu.addItem(9,"Reset curva (MOVE / SLOPE)");menu.addItem(4,"Impostazioni sidechain / detector");
    juce::Component::SafePointer<Graph> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int id){
        if(!safe||id==0)return;auto& o=safe->owner;
        if(id!=4)o.proc.beginTemplateEdit4(id==9?"Reset curva":"Vista");
        if(id>=10&&id<13)o.waveSource=id-10;
        else if(id>=20&&id<25){o.proc.setInputScopeBeats(1<<(id-20));o.waveZoom=1;o.waveOffset=0;}
        else if(id>=30&&id<34){const float centre=o.waveOffset+.5f/o.waveZoom;o.waveZoom=float(1<<(id-30));o.waveOffset=juce::jlimit(0.f,1.f-1.f/o.waveZoom,centre-.5f/o.waveZoom);}
        else if(id==1)o.waveSingle=false;
        else if(id==2)o.waveSingle=true;
        else if(id==3){o.waveZoom=1;o.waveOffset=0;}
        else if(id==5||id==6)o.waveOffset=juce::jlimit(0.f,1.f-1.f/o.waveZoom,o.waveOffset+(id==5?-.25f:.25f)/o.waveZoom);
        else if(id==7)o.waveOffset=(1.f-1.f/o.waveZoom)*.5f;
        else if(id==9)o.proc.resetTemplateCurveEdits();
        else if(id==4)o.showSettings();
        o.saveWaveView();if(id!=4)o.proc.commitTemplateEdit4();o.timerCallback();
    });
}
void SingleDuckEditor::Graph::paint(juce::Graphics& g){
    getProperties().set("onsetVisible4",false);getProperties().set("onsetBin4",-1);getProperties().set("onsetX4",-1.f);
    const float s=owner.scale;const auto r=plot();g.fillAll(juce::Colour(0xff030c10));
    g.setColour(juce::Colour(0xff153540));for(int i=0;i<=8;++i){float x=r.getX()+r.getWidth()*i/8;g.drawLine(x,r.getY(),x,r.getBottom(),.75f*s);}for(int i=0;i<=4;++i){float y=r.getY()+r.getHeight()*i/4;g.drawLine(r.getX(),y,r.getRight(),y,.75f*s);}
    for(int i=0;i<3;++i)text4(g,juce::String(100-i*50)+"%",{0,r.getY()+r.getHeight()*i/2-(i==0?0.f:i==2?24.f:12.f)*s,60*s,24*s},20*s);
    const auto frame=scopeFrame();
    const auto window=viewWindow(frame);const double period=window.captureSeconds;
    getProperties().set("curveViewRequested4",owner.curveView);getProperties().set("curveViewEffectiveLoop4",window.loop);
    getProperties().set("viewStartSeconds4",window.startSeconds);getProperties().set("viewDurationSeconds4",window.durationSeconds);
    getProperties().set("viewCycleCount4",window.durationSeconds/window.cycleSeconds);getProperties().set("capturePeriodSeconds4",period);
    getProperties().set("curvePhaseStart4",frame.bins>0?frame.curvePhaseStart:0);getProperties().set("viewCycleStart4",window.cycleAtStart);
    getProperties().set("autoDetectedOnsets4",viewDecision.detectedOnsets);
    for(int i=0;i<5;++i){const double ms=window.ticksMs[(size_t)i];const auto label=ms>=1000?juce::String(ms/1000,2)+"s":juce::String(juce::roundToInt(ms))+"ms";auto b=juce::Rectangle<float>(r.getX()+r.getWidth()*i/4-40*s,r.getBottom()+7*s,80*s,23*s);if(i==0)b.setX(r.getX());if(i==4)b.setRight(r.getRight());text4(g,label,b,17*s);}
    g.saveState();g.reduceClipRegion(r.toNearestInt());
    const float dash[]={4*s,4*s};
    const auto regions=slopeRegions(window,&frame);getProperties().set("slopeGuideVisible4",guideVisible());
    getProperties().set("slopeRegionCount4",int(regions.size()));
    for(const auto& region:regions){
        g.setColour(cyan4().withAlpha(.10f));g.fillRect(region);
        g.setColour(cyan4().withAlpha(.75f));
        g.drawDashedLine({region.getX(),r.getY(),region.getX(),r.getBottom()},dash,2,2.f*s);
        g.drawDashedLine({region.getRight(),r.getY(),region.getRight(),r.getBottom()},dash,2,2.f*s);
    }
    const auto& lo=owner.waveSource==1?frame.sideMin:owner.waveSource==2?frame.outMin:frame.mainMin;
    const auto& hi=owner.waveSource==1?frame.sideMax:owner.waveSource==2?frame.outMax:frame.mainMax;
    auto hit=odx::view::isolateSingleHit(lo,hi,frame.periodSeconds,frame.bins);
    // Partial capture has no known audio before firstBin: never invent a kick
    // from an unknown prefix or draw it as a completed waveform.
    if(frame.firstBin>0)hit.binCount=0;
    const float drawZoom=float(window.captureZoom);
    if(frame.bins>frame.firstBin){g.setColour(juce::Colour(0xffe1e5e7).withAlpha(.72f));
        for(int i=frame.firstBin;i<frame.bins&&i<512;++i){if(owner.waveSingle&&!hit.contains(i))continue;
            const float display=float(window.displayForCaptureFraction((i+.5)/512));if(display<0||display>1)continue;
            if(std::max(std::abs(lo[(size_t)i]),std::abs(hi[(size_t)i]))<1.e-7f)continue;
            const float x=r.getX()+r.getWidth()*display;
            g.drawLine(x,r.getCentreY()-hi[(size_t)i]*r.getHeight()*.46f,x,r.getCentreY()-lo[(size_t)i]*r.getHeight()*.46f,juce::jmax(.8f*s,r.getWidth()*drawZoom/512+.15f*s));}}
    if(hit.onsetBin>=0&&hit.binCount>0&&frame.firstBin==0){
        const double onsetDisplay=window.displayForCaptureFraction(hit.onsetBin/512.0);
        // Keep boundary onsets visible despite sub-sample floating-point
        // roundoff, without moving any captured audio or curve geometry.
        if(onsetDisplay>=-1.e-9&&onsetDisplay<=1.+1.e-9){
            const float display=float(juce::jlimit(0.,1.,onsetDisplay));
            const float x=r.getX()+display*r.getWidth();const int ms=juce::roundToInt(hit.onsetBin/512.0*period*1000);
            getProperties().set("onsetVisible4",true);getProperties().set("onsetBin4",hit.onsetBin);
            getProperties().set("onsetX4",x);getProperties().set("onsetMilliseconds4",ms);
            getProperties().set("onsetTop4",r.getY()+14*s);getProperties().set("onsetBottom4",r.getBottom());
            g.setColour(juce::Colour(0xffe5edf0).withAlpha(.68f));g.drawLine(x,r.getY()+14*s,x,r.getBottom(),.9f*s);
            juce::Path marker;marker.startNewSubPath(x-4*s,r.getY()+7*s);marker.lineTo(x+4*s,r.getY()+7*s);marker.lineTo(x,r.getY()+14*s);marker.closeSubPath();
            g.setColour(juce::Colour(0xffecf5f8));g.fillPath(marker);
            const auto label=juce::Rectangle<float>(juce::jlimit(r.getX()+5*s,r.getRight()-143*s,x+7*s),r.getY()+30*s,138*s,22*s);
            g.setColour(juce::Colour(0xff07131c).withAlpha(.9f));g.fillRoundedRectangle(label,3*s);
            text4(g,"INIZIO "+juce::String(ms)+" ms",label,13*s,juce::Colour(0xffd9e8ee),"Semibold");
        }
    }
    juce::Path path;const int count=juce::jmax(800,juce::roundToInt(r.getWidth()));double previousAt=0;
    const double firstCycle=window.cycleAtDisplay(0),visibleCycles=window.durationSeconds/window.cycleSeconds;
    const bool timeline=triggeredTimeline()&&frame.hasEnvelopeTimeline;
    getProperties().set("envelopeTimelineUsed4",timeline);
    getProperties().set("envelopeTriggerCapture4",timeline&&frame.envelopeTriggerCapture);
    getProperties().set("envelopeTriggerSample4",double(frame.triggerSample));
    int timelinePoints=0;float maxRawPhase=0,lastGain=1;double lastDisplay=0;
    if(timeline){
        // These samples are the gain coefficients actually used by the audio
        // path, with the same delay as the waveform. No modulo/repeated curve
        // can invent an AUDIO/MIDI retrigger or undo a ONE SHOT hold.
        for(size_t i=0;i<512;++i)if(frame.envelopeValid[i]){
            const double display=window.displayForCaptureFraction(frame.envelopeTimeSeconds[i]/period);
            const float gain=frame.envelopeGain[i];
            if(!std::isfinite(display)||!std::isfinite(gain))continue;
            const float x=r.getX()+float(display)*r.getWidth(),y=r.getBottom()-gain*r.getHeight();
            if(timelinePoints==0){
                if(frame.firstBin==0&&display>=0&&frame.envelopeTimeSeconds[i]<=period/256.)path.startNewSubPath(r.getX(),y);
                else path.startNewSubPath(x,y);
            }
            path.lineTo(x,y);++timelinePoints;lastDisplay=display;lastGain=gain;
            maxRawPhase=juce::jmax(maxRawPhase,frame.envelopePhase[i]);
        }
        if(timelinePoints>0&&frame.envelopeTriggerCapture&&frame.complete&&lastDisplay<1)
            path.lineTo(r.getRight(),r.getBottom()-lastGain*r.getHeight());
    }else if(triggeredTimeline()){
        // Before a real accepted trigger there is no gain history to invent.
        path.startNewSubPath(r.getX(),r.getY());path.lineTo(r.getRight(),r.getY());
    }else for(int i=0;i<=count;++i){float t=i/float(count);const double at=window.cycleAtDisplay(t);
        float cycle=float(at-std::floor(at)),x=r.getX()+t*r.getWidth(),y=r.getBottom()-owner.proc.previewCurveGain(0,cycle)*r.getHeight();
        if(i==0)path.startNewSubPath(x,y);
        else if(std::floor(at)>std::floor(previousAt)){
            const double boundary=std::floor(at),position=(boundary-firstCycle)/visibleCycles;
            const float edge=r.getX()+float(position)*r.getWidth();
            path.lineTo(edge,r.getBottom()-owner.proc.previewCurveGain(0,1)*r.getHeight());
            // A cycle ending at the right border keeps its full final value.
            // The next cycle starts only when there is visible time after it.
            if(i<count||at-boundary>1.e-7){path.lineTo(edge,r.getBottom()-owner.proc.previewCurveGain(0,0)*r.getHeight());path.lineTo(x,y);}
        }else path.lineTo(x,y);previousAt=at;}
    getProperties().set("envelopeTimelinePointCount4",timelinePoints);
    getProperties().set("envelopeMaxRawPhase4",maxRawPhase);
    // Keep the entire stroke visible at0/100% and at the right endpoint.
    // The waveform remains clipped to its plot; only the real curve stroke
    // gets its half-width margin, so it cannot disappear against the border.
    g.restoreState();g.saveState();g.reduceClipRegion(r.expanded(5.5f*s).toNearestInt());
    g.setColour(cyan4().withAlpha(.14f));g.strokePath(path,juce::PathStrokeType(10.4f*s));
    g.setColour(cyan4());g.strokePath(path,juce::PathStrokeType(6.f*s,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    const char* sourceName[]={"INGRESSO","SIDECHAIN","USCITA"};
    const auto actual=juce::String(window.loop?"LOOP":"1 CURVA");
    const auto stateLabel=owner.activityLabel4();
    getProperties().set("activityLabel4",stateLabel);
    const auto caption=juce::String(sourceName[owner.waveSource])+" / "+actual+" / "+stateLabel;
    getProperties().set("viewModeLabel4",(owner.curveView==0?juce::String("AUTO: "):juce::String{})+actual);
    const auto badge=juce::Rectangle<float>(r.getRight()-420*s,r.getY()+5*s,414*s,23*s);
    g.setColour(juce::Colour(0xff071219).withAlpha(.94f));g.fillRoundedRectangle(badge,3*s);text4(g,caption,badge,13*s,juce::Colour(0xffcbd4d8));
    g.restoreState();
}
void SingleDuckEditor::Graph::mouseDown(const juce::MouseEvent& e){
    updatePointer(getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
    if(e.eventComponent!=this)return;
    if(e.mods.isPopupMenu()||(e.y<35*owner.scale&&e.x>plot().getRight()-430*owner.scale)){showViewMenu();return;}
    if(!plot().contains(e.position))return;
    const auto frame=scopeFrame();dragWindow=viewWindow(frame);
    panStart=owner.waveOffset;panMouse=e.position.x;moveStart=owner.proc.value("moveCurve4");slopeStart=owner.proc.value("slope4");
    if(e.mods.isShiftDown()&&!dragWindow.loop)return;
    panning=e.mods.isShiftDown();sloping=false;
    if(!panning)for(const auto& region:slopeRegions(dragWindow,&frame))if(region.contains(e.position)){sloping=true;break;}
    moving=!panning&&!sloping;
    if(moving||sloping||panning){if(owner.isShowing())owner.grabKeyboardFocus();owner.proc.beginTemplateEdit4(sloping?"SLOPE":moving?"MOVE curva":"Scorri onda");}
    if(moving||sloping)if(auto* parameter=owner.proc.state.getParameter(sloping?"slope4":"moveCurve4"))parameter->beginChangeGesture();
}
void SingleDuckEditor::Graph::mouseDrag(const juce::MouseEvent& e){
    updatePointer(getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
    if(e.eventComponent!=this)return;
    const auto r=plot();if(r.getWidth()<=0)return;
    if(panning&&owner.waveZoom>1){owner.waveOffset=juce::jlimit(0.f,1.f-1.f/owner.waveZoom,panStart-(e.position.x-panMouse)/r.getWidth()*float(dragWindow.durationSeconds/dragWindow.captureSeconds));repaint();}
    else if(moving||sloping){
        const float cycles=float(dragWindow.durationSeconds/dragWindow.cycleSeconds);
        const float delta=(e.position.x-panMouse)/r.getWidth()*cycles;
        float next=moveStart+delta*100;
        if(sloping){const auto curve=owner.proc.getCurveSnapshot(0);const auto bounds=odx::template4::slopeBounds(curve.shape,curve.profileModel4);
            const auto initial=odx::template4::slopeTargetBounds(curve.shape,slopeStart,curve.profileModel4);
            const float rawScale=juce::jmax(.001f,curve.length*(1+curve.duration4*.01f));
            const float direction=curve.profileModel4==1&&curve.mirrorH4?-1.f:1.f;
            const float translation=initial.start-bounds.start+direction*delta/rawScale;
            next=odx::template4::slopePercentForDelta(curve.shape,translation,curve.profileModel4);
        }
        if(auto* parameter=owner.proc.state.getParameter(sloping?"slope4":"moveCurve4"))parameter->setValueNotifyingHost(parameter->convertTo0to1(next));
        repaint();}
}
void SingleDuckEditor::Graph::mouseUp(const juce::MouseEvent& e){
    updatePointer(getLocalBounds().toFloat().contains(e.getEventRelativeTo(this).position));
    const bool edited=moving||sloping||panning;
    if(moving||sloping)if(auto* parameter=owner.proc.state.getParameter(sloping?"slope4":"moveCurve4"))parameter->endChangeGesture();
    if(panning)owner.saveWaveView();moving=panning=sloping=false;dragWindow={};
    if(edited)owner.proc.commitTemplateEdit4();owner.timerCallback();repaint();
}
void SingleDuckEditor::Graph::mouseDoubleClick(const juce::MouseEvent& e){juce::ignoreUnused(e);}
void SingleDuckEditor::Graph::mouseWheelMove(const juce::MouseEvent&e,const juce::MouseWheelDetails&w){
    if(!viewWindow(scopeFrame()).loop)return;const auto r=plot();
    const float anchor=juce::jlimit(0.f,1.f,(e.position.x-r.getX())/r.getWidth()),at=owner.waveOffset+anchor/owner.waveZoom;
    owner.performEdit("Zoom onda",[this,anchor,at,w]{owner.waveZoom=juce::jlimit(1.f,8.f,owner.waveZoom*(w.deltaY>0?2.f:.5f));owner.waveOffset=juce::jlimit(0.f,1.f-1.f/owner.waveZoom,at-anchor/owner.waveZoom);owner.saveWaveView();});
}



void SingleDuckEditor::paintOverChildren(juce::Graphics& g){
    if(!reference.isValid())return;
    const auto target=box(182,526,170,34);
    const int bottom=box(192,559,152,47).getY();
    g.saveState();g.reduceClipRegion({target.getX(),target.getY(),target.getWidth(),juce::jmax(0,bottom-target.getY())});
    g.drawImage(reference,target.getX(),target.getY(),target.getWidth(),target.getHeight(),182,526,170,34);g.restoreState();
}
