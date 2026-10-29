from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.h';s=f.read_text().replace('look;','look;').replace('void drawRotarySlider(', 'void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool)override; void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool)override; void drawRotarySlider(')
s=s.replace('juce::ComboBox trigger,grid;', 'std::array<juce::Slider,3> details;std::array<juce::Label,3> detailLabels;std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> detailAttachments;int lastSelected=-1;\n juce::TextButton undoButton{"Undo"},redoButton{"Redo"},savePreset{"Save"},loadPreset{"Load"};std::unique_ptr<juce::FileChooser> chooser;\n juce::Slider freeTempo;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;\n juce::ComboBox trigger,grid;')
s=s.replace('void timerCallback()override{repaint();}', 'void timerCallback()override;void updateDetails();')
f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text();pos=s.index('void OpenDuckEditor::MetalLook::drawRotarySlider');s=s[:pos]+'''void OpenDuckEditor::MetalLook::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& c,bool h,bool d){if(b.getProperties().contains("artwork"))return;juce::LookAndFeel_V4::drawButtonBackground(g,b,c,h,d);}
void OpenDuckEditor::MetalLook::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool h,bool d){if(b.getProperties().contains("artwork"))return;juce::LookAndFeel_V4::drawButtonText(g,b,h,d);}
''' +s[pos:]
s=s.replace('b.setButtonText(names[i]);','b.setButtonText(names[i]);b.getProperties().set("artwork",true);').replace('sh.setButtonText(juce::String(i+1));','sh.setButtonText(juce::String(i+1));sh.getProperties().set("artwork",true);')
s=s.replace('startTimerHz(30);','''
 addAndMakeVisible(freeTempo);freeTempo.setSliderStyle(juce::Slider::LinearBar);freeTempo.setTextBoxStyle(juce::Slider::TextBoxLeft,false,80,25);tempoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"freeBpm",freeTempo);freeTempo.setTooltip("Tempo indipendente quando HOST SYNC è disattivato");
 for(int k=0;k<3;++k){addAndMakeVisible(details[(size_t)k]);details[(size_t)k].setSliderStyle(juce::Slider::LinearHorizontal);details[(size_t)k].setTextBoxStyle(juce::Slider::TextBoxRight,false,65,20);addAndMakeVisible(detailLabels[(size_t)k]);detailLabels[(size_t)k].setFont(juce::FontOptions(13));}
 for(auto* b:{&undoButton,&redoButton,&savePreset,&loadPreset})addAndMakeVisible(b);
 undoButton.onClick=[this]{proc.undo.undo();};redoButton.onClick=[this]{proc.undo.redo();};
 auto folder=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenDuck/Pro X/Presets");
 savePreset.onClick=[this,folder]{folder.createDirectory();chooser=std::make_unique<juce::FileChooser>("Salva preset",folder.getChildFile("My preset.odpx"),"*.odpx");chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[this](const juce::FileChooser& f){auto file=f.getResult();if(file!=juce::File{}){juce::MemoryBlock data;proc.getStateInformation(data);file.replaceWithData(data.getData(),data.getSize());}});};
 loadPreset.onClick=[this,folder]{chooser=std::make_unique<juce::FileChooser>("Carica preset",folder,"*.odpx");chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[this](const juce::FileChooser& f){auto file=f.getResult();juce::MemoryBlock data;if(file.loadFileAsData(data))proc.setStateInformation(data.getData(),(int)data.getSize());});};
 updateDetails();startTimerHz(30);''')
s=s.replace('modes[(size_t)i].setBounds(rect(456+i*130,178,123,78));','modes[(size_t)i].setBounds(rect(456+i*130,158,123,99));')
s=s.replace('rect(459+i*130,121,63,38)','rect(462+i*130,120,70,32)').replace('rect(522+i*130,121,51,38)','rect(532+i*130,120,45,32)')
s=s.replace('sync.setBounds(rect(1290,120,160,51));','''sync.setBounds(rect(1290,120,160,51));freeTempo.setBounds(rect(1290,184,120,47));
 for(int k=0;k<3;++k){detailLabels[(size_t)k].setBounds(rect(80+k*305,304,115,24));details[(size_t)k].setBounds(rect(190+k*305,304,180,24));}
 undoButton.setBounds(rect(84,683,57,30));redoButton.setBounds(rect(145,683,57,30));savePreset.setBounds(rect(207,683,57,30));loadPreset.setBounds(rect(268,683,57,30));''')
s=s.replace('g.setColour(juce::Colours::black);g.fillRect(1289,185,125,47);', 'g.setColour(juce::Colours::black);g.fillRect(1289,185,125,47);')
s=s.replace('for(int i=0;i<6;++i){if((int)proc.value("selected")==i){g.setColour(modeColours[i]);g.drawRoundedRectangle(455.0f+i*130,115,126,147,12,2);}}','''
 const char* labels[]={"CLASSIC","SPECTRAL","MULTIBAND","MORPH","GROOVE","TRANSIENT"};
 for(int i=0;i<6;++i){float x=455.0f+i*130;auto col=modeColours[i];g.setColour(juce::Colour(0xff030709));g.fillRoundedRectangle(x,115,126,147,12);g.setColour(col.withAlpha((int)proc.value("selected")==i?1.0f:0.3f));g.drawRoundedRectangle(x,115,126,147,12,(int)proc.value("selected")==i?2.5f:1.0f);g.setColour(col);g.setFont(20);g.drawText(labels[i],(int)x+2,221,122,32,juce::Justification::centred);
 if(i==1||i==2||i==4){for(int k=0;k<5;++k){float h=(float)(12+(k==2?35:k==1||k==3?24:6));g.fillRect(x+35+k*12,208-h,8.0f,h);}}else{juce::Path icon;for(int k=0;k<60;++k){float t=k/59.0f;float y=i==3?std::sin(t*juce::MathConstants<float>::twoPi)*14:i==5?std::sin(t*juce::MathConstants<float>::twoPi*3)*std::exp(-std::abs(t-0.5f)*8)*25:(k>22&&k<36?(k<29?-(k-22)*4.0f:-28+(k-29)*4.0f):0);if(k==0)icon.startNewSubPath(x+24,188+y);else icon.lineTo(x+24+t*80,188+y);}g.strokePath(icon,juce::PathStrokeType(3));}
 float activity=proc.engineActivity[(size_t)i].load();bool on=proc.value("on"+juce::String(i))>0.5f;g.setColour(on?col.withAlpha(0.25f+0.75f*activity):juce::Colour(0xff252a2e));g.fillEllipse(x+58,155,10,10);
 }
 g.setColour(juce::Colour(0xff060b0d));g.fillRoundedRectangle(76,678,275,129,8);
 for(int i=0;i<6;++i){float x=78.0f+i*46;g.setColour((int)proc.value("shape")==i?modeColours[0]:juce::Colour(0xff657078));g.drawRoundedRectangle(x,738,42,64,5,1);juce::Path path;for(int k=0;k<40;++k){float t=k/39.0f;float v=OpenDuckProcessor::shape(t,i,proc.value("tension"));if(i==5){float z=t*7;int j=juce::jmin(6,(int)z);v=juce::jmap(z-j,proc.value("point"+juce::String(j)),proc.value("point"+juce::String(j+1)));}if(k==0)path.startNewSubPath(x+4,792-v*43);else path.lineTo(x+4+t*34,792-v*43);}g.strokePath(path,juce::PathStrokeType(1.5f));}
 ''')
s += '''
void OpenDuckEditor::timerCallback(){updateDetails();freeTempo.setVisible(proc.value("sync")<0.5f);undoButton.setEnabled(proc.undo.canUndo());redoButton.setEnabled(proc.undo.canRedo());repaint();}
void OpenDuckEditor::updateDetails(){int selected=(int)proc.value("selected");if(selected==lastSelected)return;lastSelected=selected;detailAttachments.clear();
 const char* params[6][3]={{"freeBpm","hold","threshold"},{"spectralLow","spectralHigh","spectralDepth"},{"bandLow","bandMid","bandHigh"},{"morph","crossLow","crossHigh"},{"grooveDepth","swing","freeBpm"},{"threshold","hysteresis","hold"}};
 for(int k=0;k<3;++k){auto id=juce::String(params[selected][k]);detailLabels[(size_t)k].setText(id,juce::dontSendNotification);detailAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,id,details[(size_t)k]));}
}
'''
f.write_text(s)
