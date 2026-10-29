from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('(int)(415*scale),(int)(688*scale),(int)(82*scale),(int)(82*scale)','(int)(423*scale),(int)(698*scale),(int)(66*scale),(int)(66*scale)')
a=s.index(' for(int n=0;n<80;++n)');b=s.index('\n const float a=start',a);s=s[:a]+s[b:];f.write_text(s)
f=p/'tests/ProcessorTests.cpp';s=f.read_text().replace('e->resized();{auto image=', '''e->resized();{juce::MemoryBlock preset;visual.getStateInformation(preset);folder.getChildFile("QA-StateRoundtrip.odpx").replaceWithData(preset.getData(),preset.getSize());}{auto image=''')
f.write_text(s)
