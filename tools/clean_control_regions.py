from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.cpp';s=f.read_text()
a=s.index(' for(int i=0;i<5;++i){g.drawImage(artwork,388');b=s.index('\n}',a)
s=s[:a]+''' for(int i=0;i<5;++i){g.setColour(juce::Colours::black);g.fillRoundedRectangle(398.0f+i*176,789,121,40,7);}
 g.setColour(juce::Colour(0xff080b0d));g.fillEllipse(1470,107,147,147);g.fillRect(1486,238,119,28);g.fillEllipse(1489,767,76,76);g.fillRect(1562,805,36,24);
 g.setColour(juce::Colours::white);g.setFont(22);g.drawText(juce::String(proc.value("mix"),0)+"%",1486,238,119,28,juce::Justification::centred);g.setFont(15);g.drawText(juce::String(proc.value("swing"),0)+"%",1562,805,36,24,juce::Justification::centred);
 g.setColour(juce::Colour(0xff020708));g.fillRoundedRectangle(1296,670,295,72,7);
''' +s[b:]
s=s.replace('float assetScale=artwork.getWidth()/1672.0f;','')
s=s.replace('knobs[5].setBounds(rect(1474,111,138,164));knobs[6].setBounds(rect(1491,768,72,70));','knobs[5].setBounds(rect(1474,111,138,138));knobs[6].setBounds(rect(1491,768,72,72));')
s=s.replace(' const char* names[]=', ' knobs[5].setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knobs[6].setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);\n const char* names[]=')
s=s.replace('float activity=proc.engineActivity[(size_t)i].load();bool on=proc.value("on"+juce::String(i))>0.5f;g.setColour(on?col.withAlpha(0.25f+0.75f*activity):juce::Colour(0xff252a2e));g.fillEllipse(x+58,155,10,10);','')
s=s.replace('details[(size_t)k].textFromValueFunction=[](double v){return juce::String(v,std::abs(v)<10?2:0);};detailLabels', 'detailLabels')
s=s.replace('detailAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,id,details[(size_t)k]));','detailAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,id,details[(size_t)k]));details[(size_t)k].textFromValueFunction=[](double v){return juce::String(v,std::abs(v)<10?2:0);};details[(size_t)k].updateText();')
s=s.replace('lastSelected=selected;for(auto& step:steps)', 'lastSelected=selected;for(int k=0;k<3;++k){details[(size_t)k].setVisible(selected!=0);detailLabels[(size_t)k].setVisible(selected!=0);}for(auto& step:steps)')
f.write_text(s)
f=p/'installer/OpenDuck.iss';s=f.read_text().replace('procedure StopMusic();','function StopSound(pszSound: Integer; hmod: Integer; fdwSound: Cardinal): Boolean;\n external \'PlaySoundW@winmm.dll stdcall\';\nprocedure StopMusic();').replace("PlaySound('', 0, 0);",'StopSound(0, 0, 0);');f.write_text(s)
