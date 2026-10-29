from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.h';s=f.read_text().replace('juce::Slider freeTempo;','juce::Label mixValue,swingValue;\n juce::Slider freeTempo;');f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('" ms":"%");};}', '" ms":"%");};k.updateText();}')
s=s.replace(' const char* names[]=', ''' addAndMakeVisible(mixValue);addAndMakeVisible(swingValue);for(auto* label:{&mixValue,&swingValue}){label->setJustificationType(juce::Justification::centred);label->setInterceptsMouseClicks(false,false);label->setColour(juce::Label::backgroundColourId,juce::Colour(0xff080b0d));}mixValue.setFont(juce::FontOptions(20));swingValue.setFont(juce::FontOptions(15));mixValue.setText(juce::String(proc.value("mix"),0)+"%",juce::dontSendNotification);swingValue.setText(juce::String(proc.value("swing"),0)+"%",juce::dontSendNotification);
 const char* names[]=''')
s=s.replace('bandPage.setBounds(rect(', 'mixValue.setBounds(rect(1486,241,119,27));swingValue.setBounds(rect(1562,805,36,24));\n bandPage.setBounds(rect(')
s=s.replace('void OpenDuckEditor::timerCallback(){updateDetails();','void OpenDuckEditor::timerCallback(){mixValue.setText(juce::String(proc.value("mix"),0)+"%",juce::dontSendNotification);swingValue.setText(juce::String(proc.value("swing"),0)+"%",juce::dontSendNotification);updateDetails();')
f.write_text(s)
