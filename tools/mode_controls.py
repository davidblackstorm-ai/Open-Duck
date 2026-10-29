from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.h';s=f.read_text().replace('int lastSelected=-1;','int lastSelected=-1,lastTrigger=-1;').replace('juce::ComboBox trigger,grid;','juce::ComboBox trigger,grid,bandPage;');f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('addAndMakeVisible(sync);','addAndMakeVisible(bandPage);bandPage.addItemList({"BAND AMOUNT","BAND ATTACK","BAND RELEASE","CROSSOVERS"},1);bandPage.setSelectedId(1);bandPage.onChange=[this]{lastSelected=-1;updateDetails();};\n addAndMakeVisible(sync);')
s=s.replace('for(int k=0;k<3;++k){detailLabels[(size_t)k].setBounds','bandPage.setBounds(rect(1080,303,220,25));\n for(int k=0;k<3;++k){detailLabels[(size_t)k].setBounds')
a=s.index('void OpenDuckEditor::updateDetails()');s=s[:a]+'''void OpenDuckEditor::updateDetails(){int selected=(int)proc.value("selected"),trig=(int)proc.value("trigger");if(selected==lastSelected&&trig==lastTrigger)return;lastSelected=selected;lastTrigger=trig;bandPage.setVisible(selected==2);for(auto& step:steps)step.setVisible(selected==4);detailAttachments.clear();
 const char* params[6][3]={{"threshold","hysteresis","hold"},{"spectralLow","spectralHigh","spectralDepth"},{"bandLow","bandMid","bandHigh"},{"morph","tension","shift"},{"grooveDepth","swing","freeBpm"},{"threshold","hysteresis","hold"}};
 const char* labels[6][3]={{"Threshold dB","Hysteresis dB","Hold ms"},{"Low Hz","High Hz","Depth"},{"Low","Mid","High"},{"Morph","Tension","Shift"},{"Pattern depth","Swing","Free BPM"},{"Threshold dB","Hysteresis dB","Hold ms"}};
 const char* attacks[]={"bandAttack0","bandAttack1","bandAttack2"};const char* releases[]={"bandRelease0","bandRelease1","bandRelease2"};const char* cross[]={"crossLow","crossHigh","bandLow"};
 for(int k=0;k<3;++k){const char* id=params[selected][k];const char* label=labels[selected][k];if(selected==2){int page=bandPage.getSelectedId();if(page==2)id=attacks[k];if(page==3)id=releases[k];if(page==4){id=cross[k];label=k==0?"Low Hz":k==1?"High Hz":"Low amount";}}
 bool show=(selected!=0||trig==1)&&(!(selected==3||selected==4)||k==0);details[(size_t)k].setVisible(show);detailLabels[(size_t)k].setVisible(show);detailLabels[(size_t)k].setText(label,juce::dontSendNotification);detailAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,id,details[(size_t)k]));details[(size_t)k].textFromValueFunction=[](double v){return juce::String(v,std::abs(v)<10?2:0);};details[(size_t)k].updateText();}
}
''';f.write_text(s)
