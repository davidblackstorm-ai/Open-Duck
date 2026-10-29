from pathlib import Path
p=Path(r'E:\Codex\Lavori\OpenDuck Pro X')
f=p/'Source/PluginEditor.cpp';s=f.read_text().replace('artwork=juce::ImageCache::getFromMemory(BinaryData::DesignMaster_png,BinaryData::DesignMaster_pngSize);','artwork=juce::SoftwareImageType().convert(juce::ImageCache::getFromMemory(BinaryData::DesignMaster_png,BinaryData::DesignMaster_pngSize));setOpaque(true);');f.write_text(s)
f=p/'tests/ProcessorTests.cpp';s=f.read_text().replace('auto stream=file.createOutputStream();stream->setPosition(0);stream->truncate();auto* streamPtr=stream.release();std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(streamPtr,48000,2,24,{},0));','std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();stream->setPosition(0);static_cast<juce::FileOutputStream*>(stream.get())->truncate();auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));')
f.write_text(s)
f=p/'installer/OpenDuck.iss';s=f.read_text();s=s.replace('[Setup]\nAppId=', '''[Setup]
#ifdef QA
AppId={{6C36316E-2BA3-4530-BA65-4FDC729F08CA}
DefaultDirName=..\\work\\installer-qa\\OpenDuck Pro X.vst3
UninstallFilesDir={app}\\Uninstall
#else
AppId=''')
s=s.replace('AppName=OpenDuck Pro X','AppName=OpenDuck Pro X',1).replace('AppVersion={#ProductVersion}', 'AppVersion={#ProductVersion}',1)
# Move existing default/uninstaller directives inside the production branch.
s=s.replace('DefaultDirName={commoncf64}\\VST3\\OpenDuck Pro X.vst3\n','').replace('UninstallFilesDir={autopf}\\OpenDuck Pro X\n','')
s=s.replace('AppId={{A2C52811-2937-42FA-A186-0100469D28E5}', 'AppId={{A2C52811-2937-42FA-A186-0100469D28E5}\nDefaultDirName={commoncf64}\\VST3\\OpenDuck Pro X.vst3\nUninstallFilesDir={autopf}\\OpenDuck Pro X\n#endif')
s=s.replace('Source: "LICENSE.txt"; DestDir: "{autopf}\\OpenDuck Pro X"; Flags: ignoreversion','''#ifdef QA
Source: "LICENSE.txt"; DestDir: "{app}\\Documentation"; Flags: ignoreversion
Source: "RELEASE-NOTES.txt"; DestDir: "{app}\\Documentation"; Flags: ignoreversion
#else
Source: "LICENSE.txt"; DestDir: "{autopf}\\OpenDuck Pro X"; Flags: ignoreversion''')
s=s.replace('Source: "RELEASE-NOTES.txt"; DestDir: "{autopf}\\OpenDuck Pro X"; Flags: ignoreversion','Source: "RELEASE-NOTES.txt"; DestDir: "{autopf}\\OpenDuck Pro X"; Flags: ignoreversion\n#endif')
s=s.replace("PlaySound(ExpandConstant('{tmp}\\OriginalTechno.wav'),0,$00020009);", "AudioOn := PlaySound(ExpandConstant('{tmp}\\OriginalTechno.wav'),0,$00020009);\n  Log('Installer audio playback requested; success=' + IntToStr(Ord(AudioOn)));")
s=s.replace("AudioButton.Caption := 'AUDIO ON';", "if AudioOn then AudioButton.Caption := 'AUDIO ON' else AudioButton.Caption := 'AUDIO OFF';")
s=s.replace('if not WizardSilent then AudioClick(AudioButton);',"if (not WizardSilent) and (ExpandConstant('{param:NOAUDIO|0}') <> '1') then AudioClick(AudioButton);")
f.write_text(s)
