from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginProcessor.cpp';s=f.read_text().replace('const float detector=hasSc?std::abs(side):trigger==1?0:inputLevel;', 'const bool externalActive=hasSc&&sideSilence<(int)(rate*0.5);const float detector=trigger==1?std::abs(side):externalActive?std::abs(side):inputLevel;');f.write_text(s)
f=p/'tests/ProcessorTests.cpp';s=f.read_text().replace('0.2f*std::sin((float)(juce::MathConstants<double>::twoPi*1000*(block*512+i)/48000))','(float)(0.2*std::sin(juce::MathConstants<double>::twoPi*1000*(block*512+i)/48000))')
s=s.replace('bool finite=true;for(int block=0;block<100;++block)for(auto& e:engines)', 'bool instancesFinite=true;for(int block=0;block<100;++block)for(auto& e:engines)').replace('finite&=std::isfinite(audio.getSample(0,i));}double ratio=', 'instancesFinite&=std::isfinite(audio.getSample(0,i));}double ratio=').replace('check(finite&&ratio<1,','check(instancesFinite&&ratio<1,')
# Preview uses the exact editor but never touches an audio device. It is a QA tool, not a product format.
pos=s.index('int main(int argc');s=s[:pos]+'''class PreviewWindow final:public juce::DocumentWindow {public:PreviewWindow():DocumentWindow("OpenDuck UI QA — software renderer",juce::Colours::black,DocumentWindow::closeButton){processor.prepareToPlay(48000,512);setUsingNativeTitleBar(true);setContentOwned(processor.createEditor(),true);centreWithSize(getWidth(),getHeight());setVisible(true);}void closeButtonPressed()override{juce::MessageManager::getInstance()->stopDispatchLoop();}private:OpenDuckProcessor processor;};
''' +s[pos:]
s=s.replace('juce::ScopedJuceInitialiser_GUI gui;int failed=0,passed=0;', 'juce::ScopedJuceInitialiser_GUI gui;if(argc>1&&juce::String(argv[1])=="--preview"){PreviewWindow window;juce::MessageManager::getInstance()->runDispatchLoop();return 0;}int failed=0,passed=0;')
f.write_text(s)
