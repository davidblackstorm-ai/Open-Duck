from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.cpp';s=f.read_text()
s=s.replace('attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,ids[i],k));','attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,ids[i],k));k.textFromValueFunction=[i](double v){return juce::String(v,(i==1)?1:0)+(i==1||i==2?" ms":"%");};')
s=s.replace('syncAttachment=std::make_unique','sync.setButtonText("");syncAttachment=std::make_unique')
s=s.replace('updateDetails();startTimerHz(30);','freeTempo.setVisible(proc.value("sync")<0.5f);updateDetails();startTimerHz(30);')
s=s.replace('rect(388+176*i,665,139,165)','rect(388+176*i,660,138,164)').replace('rect(1472,112,139,168)','rect(1474,111,138,164)').replace('rect(1492,773,77,68)','rect(1491,768,72,70)')
s=s.replace('sync.setBounds(rect(1290,120,160,51))','sync.setBounds(rect(1290,128,26,36))')
s=s.replace('float h=(float)(12+(k==2?35:k==1||k==3?24:6));','float h=(float)(12+(k==2?35:k==1||k==3?24:6));')
# Draw the original mode icon pixels, preserving their visual design exactly.
s=s.replace('float activity=proc.engineActivity','float sourceScale=artwork.getWidth()/1672.0f;g.drawImage(artwork,(int)x+20,150,90,65,(int)((x+20)*sourceScale),(int)(150*sourceScale),(int)(90*sourceScale),(int)(65*sourceScale));\n float activity=proc.engineActivity')
s=s.replace('for(int i=0;i<5;++i){g.setColour(juce::Colour(0xff303437));g.fillRect(391+i*176,663,136,168);}g.setColour(juce::Colour(0xff20282c));g.fillRect(1470,111,141,170);g.fillRect(1490,773,82,70);','''
 float assetScale=artwork.getWidth()/1672.0f;
 for(int i=0;i<5;++i){g.drawImage(artwork,388+i*176,660,138,166,(int)(1230*assetScale),(int)(660*assetScale),(int)(26*assetScale),(int)(166*assetScale));}
 g.setColour(juce::Colour(0xff161b1d));g.fillEllipse(1473,111,141,141);g.fillRect(1486,242,119,39);g.fillEllipse(1490,767,75,75);
 g.setColour(juce::Colour(0xff020708));g.fillRoundedRectangle(1296,670,295,72,7);
 ''')
# Avoid snapshot overlay of the static LED and ensure a coherent sync display.
s=s.replace('g.setColour(juce::Colours::black);g.fillRect(1289,185,125,47);','g.setColour(juce::Colours::black);g.fillRect(1289,185,125,47);')
s=s.replace('detailLabels[(size_t)k].setText(id,','details[(size_t)k].textFromValueFunction=[](double v){return juce::String(v,std::abs(v)<10?2:0);};detailLabels[(size_t)k].setText(id,')
f.write_text(s)
