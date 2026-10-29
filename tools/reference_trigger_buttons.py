from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.h';s=f.read_text().replace('std::array<juce::TextButton,6> modes,shapes;','std::array<juce::TextButton,6> modes,shapes;std::array<juce::TextButton,3> triggerButtons;');f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('addAndMakeVisible(trigger);trigger.addItemList','trigger.addItemList')
s=s.replace(' addAndMakeVisible(grid);',' for(int k=0;k<3;++k){addAndMakeVisible(triggerButtons[(size_t)k]);triggerButtons[(size_t)k].getProperties().set("artwork",true);triggerButtons[(size_t)k].onClick=[this,k]{set("trigger",(float)k);};triggerButtons[(size_t)k].setName(k==0?"HOST":k==1?"SIDECHAIN":"MIDI");}\n addAndMakeVisible(grid);grid.setScrollWheelEnabled(true);')
s=s.replace('trigger.setBounds(rect(1296,680,292,55));','for(int k=0;k<3;++k)triggerButtons[(size_t)k].setBounds(rect(1296+k*98,670,98,72));')
s=s.replace('g.setColour(juce::Colour(0xff020708));g.fillRoundedRectangle(1296,670,295,72,7);','''g.setColour(juce::Colour(0xff020708));g.fillRoundedRectangle(1296,670,295,72,7);
 const char* triggerLabels[]={"HOST","SIDECHAIN","MIDI"};for(int k=0;k<3;++k){float x=1296.0f+k*98;bool active=(int)proc.value("trigger")==k;g.setColour(active?modeColours[0]:juce::Colour(0xff8a9296));if(active)g.drawRoundedRectangle(x,670,92,72,7,2);g.drawEllipse(x+40,683,12,12,1.5f);if(active){g.setColour(modeColours[0]);g.fillEllipse(x+42,685,8,8);}g.setColour(juce::Colours::white);g.setFont(17);g.drawText(triggerLabels[k],(int)x,706,94,29,juce::Justification::centred);}
 ''')
f.write_text(s)
