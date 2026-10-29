from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.h';s=f.read_text().replace('void resized()override;', 'void resized()override;void parentHierarchyChanged() override;');f.write_text(s)
f=p/'Source/PluginEditor.cpp';s=f.read_text();s += '''
void OpenDuckEditor::parentHierarchyChanged(){if(auto* peer=getPeer())peer->setCurrentRenderingEngine(0);}
''';s=s.replace('void OpenDuckEditor::timerCallback(){','void OpenDuckEditor::timerCallback(){if(auto* peer=getPeer())if(peer->getCurrentRenderingEngine()!=0)peer->setCurrentRenderingEngine(0);');f.write_text(s)
