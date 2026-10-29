from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.h';s=f.read_text().replace('#include "Engines.h"','#include "Engines.h"\n#include <map>\n#include <string_view>')
s=s.replace('float value(const juce::String& id) const {return state.getRawParameterValue(id)->load();}', '''float value(const juce::String& id) const {return state.getRawParameterValue(id)->load();}
 float value(const char* id) const {auto it=raw.find(std::string_view(id));return it==raw.end()?0:it->second->load(std::memory_order_relaxed);}
 juce::AudioProcessorParameter* getBypassParameter() const override {return state.getParameter("bypass");}''')
s=s.replace('double rate=48000,phase=0;', 'std::map<std::string,std::atomic<float>*,std::less<>> raw;\n std::array<std::atomic<float>*,8> pointRefs{};std::array<std::atomic<float>*,6> onRefs{},soloRefs{};std::array<std::atomic<float>*,16> stepRefs{};\n double rate=48000,phase=0,freeCycle=0;')
f.write_text(s)
f=p/'Source/PluginProcessor.cpp';s=f.read_text().replace('layout()){}','''layout()){
 for(auto* param:getParameters())if(auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(param))raw.emplace(named->paramID.toStdString(),state.getRawParameterValue(named->paramID));
 for(int k=0;k<8;++k)pointRefs[(size_t)k]=state.getRawParameterValue("point"+juce::String(k));
 for(int k=0;k<6;++k){onRefs[(size_t)k]=state.getRawParameterValue("on"+juce::String(k));soloRefs[(size_t)k]=state.getRawParameterValue("solo"+juce::String(k));}
 for(int k=0;k<16;++k)stepRefs[(size_t)k]=state.getRawParameterValue("step"+juce::String(k));
}''')
s=s.replace('rate=sr;phase=0.999999;','rate=sr;phase=0.999999;freeCycle=0;').replace('bypassSmooth=1;','bypassSmooth=value("bypass")>0.5f?0.0f:1.0f;')
s=s.replace('double cycleIndex=positioned?std::floor(ppq/beats):0;', 'double cycleIndex=positioned?std::floor(ppq/beats):freeCycle;')
s=s.replace('points[(size_t)k]=value("point"+juce::String(k))','points[(size_t)k]=pointRefs[(size_t)k]->load(std::memory_order_relaxed)').replace('on[(size_t)k]=value("on"+juce::String(k))>0.5f;solo[(size_t)k]=value("solo"+juce::String(k))>0.5f;', 'on[(size_t)k]=onRefs[(size_t)k]->load(std::memory_order_relaxed)>0.5f;solo[(size_t)k]=soloRefs[(size_t)k]->load(std::memory_order_relaxed)>0.5f;').replace('steps[(size_t)k]=value("step"+juce::String(k))>0.5f','steps[(size_t)k]=stepRefs[(size_t)k]->load(std::memory_order_relaxed)>0.5f')
s=s.replace(' inMeter=peakIn;', ' freeCycle=cycleIndex;inMeter=peakIn;')
f.write_text(s)
f=p/'Source/PluginEditor.h';s=f.read_text().replace('struct MetalLook:juce::LookAndFeel_V4{', 'struct MetalLook:juce::LookAndFeel_V4{juce::Image metal;').replace('std::array<juce::TextButton,6> modes,shapes;', 'std::array<juce::TextButton,6> modes,shapes;std::array<juce::ToggleButton,16> steps;std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> stepAttachments;')
f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text();s=s.replace('setLookAndFeel(&look);','look.metal=artwork;setLookAndFeel(&look);')
s=s.replace('g.setColour(juce::Colour(0xffcccccc));g.drawEllipse(face,2);','''if(lookAndFeelAlwaysFalse){}g.setColour(juce::Colour(0xffcccccc));g.drawEllipse(face,2);''').replace('if(lookAndFeelAlwaysFalse){}','''if(metal.isValid()){g.saveState();juce::Path clip;clip.addEllipse(face.reduced(1));g.reduceClipRegion(clip);float scale=metal.getWidth()/1672.0f;g.drawImage(metal,(int)face.getX(),(int)face.getY(),(int)face.getWidth(),(int)face.getHeight(),(int)(415*scale),(int)(688*scale),(int)(82*scale),(int)(82*scale));g.restoreState();}''')
s=s.replace('updateDetails();startTimerHz(30);','''for(int k=0;k<16;++k){addAndMakeVisible(steps[(size_t)k]);steps[(size_t)k].setButtonText(juce::String(k+1));stepAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state,"step"+juce::String(k),steps[(size_t)k]));}updateDetails();startTimerHz(30);''')
s=s.replace('undoButton.setBounds(rect(', 'for(int k=0;k<16;++k)steps[(size_t)k].setBounds(rect(80+k*75,562,74,29));\n undoButton.setBounds(rect(')
s=s.replace('void OpenDuckEditor::timerCallback(){updateDetails();','void OpenDuckEditor::timerCallback(){updateDetails();for(auto& step:steps)step.setVisible((int)proc.value("selected")==4);')
s=s.replace('lastSelected=selected;detailAttachments.clear();','lastSelected=selected;for(auto& step:steps)step.setVisible(selected==4);detailAttachments.clear();')
s=s.replace('void OpenDuckEditor::mouseDown(const juce::MouseEvent& e){mouseDrag(e);}', '''void OpenDuckEditor::mouseDown(const juce::MouseEvent& e){proc.undo.beginNewTransaction("Edit curve");float s=1672.0f/getWidth();auto p=e.position*s;if(p.x>=60&&p.x<=1332&&p.y>=330&&p.y<=550&&(int)proc.value("shape")!=5){int type=(int)proc.value("shape");for(int k=0;k<8;++k)set("point"+juce::String(k),OpenDuckProcessor::shape(k/7.0f,type,proc.value("tension")));}mouseDrag(e);}''')
s=s.replace('g.strokePath(curve,juce::PathStrokeType(2));','''g.strokePath(curve,juce::PathStrokeType(2));
 for(int k=0;k<8;++k){float t=k/7.0f;float v=(int)proc.value("shape")==5?proc.value("point"+juce::String(k)):OpenDuckProcessor::shape(t,(int)proc.value("shape"),proc.value("tension"));float x=60+t*1272,y=570-v*235;g.setColour(juce::Colour(0xff071018));g.fillEllipse(x-6,y-6,12,12);g.setColour(colour);g.drawEllipse(x-6,y-6,12,12,2);}
 ''')
f.write_text(s)
