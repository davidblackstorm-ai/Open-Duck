#ifndef ProductVersion
  #define ProductVersion "0.4.1"
#endif
#ifndef ProjectRoot
  #define ProjectRoot AddBackslash(SourcePath) + ".."
#endif
#ifdef QAUpgradeFrom030
  #ifndef QA
    #error QAUpgradeFrom030 is restricted to the isolated QA installer
  #endif
  // The archived 0.3.0 QA setup was compiled for this immutable v0.2.0 target.
  #define QARoot ProjectRoot + "\work\installer-qa-v0.2.0"
#else
  #define QARoot ProjectRoot + "\work\installer-qa-v" + ProductVersion
#endif

[Setup]
#ifdef QA
AppId={{6C36316E-2BA3-4530-BA65-4FDC729F08CA}
AppName=Open Duck - Installer QA
DefaultDirName={#QARoot}\OpenDuck Pro X.vst3
UninstallFilesDir={app}\Uninstall
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
UsePreviousAppDir=no
UninstallDisplayName=Open Duck {#ProductVersion} - Installer QA
#else
AppId={{A2C52811-2937-42FA-A186-0100469D28E5}
AppName=Open Duck
DefaultDirName={commoncf64}\VST3\OpenDuck Pro X.vst3
UninstallFilesDir={autopf}\OpenDuck Pro X
PrivilegesRequired=admin
UninstallDisplayName=Open Duck
#endif
AppVersion={#ProductVersion}
AppVerName=Black Circuit — Open Duck {#ProductVersion}
AppPublisher=Black Circuit
DisableWelcomePage=no
WizardImageFile={#ProjectRoot}\installer\WizardPanel.bmp
WizardSmallImageFile={#ProjectRoot}\installer\WizardSmall.bmp
WizardImageStretch=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#ProjectRoot}\releases
OutputBaseFilename=OpenDuck-Pro-X-{#ProductVersion}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=LICENSE.txt
InfoBeforeFile=RELEASE-NOTES.txt
WizardStyle=modern dark
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
VersionInfoVersion={#ProductVersion}.0
VersionInfoProductVersion={#ProductVersion}.0
VersionInfoProductTextVersion={#ProductVersion}
VersionInfoDescription=Black Circuit - Open Duck {#ProductVersion} - Windows x64 VST3 installer
SetupLogging=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
italian.DeletePresets=Eliminare anche i preset personali di OpenDuck Pro X dell'utente corrente? Seleziona No per conservarli. Questa operazione non elimina i progetti della DAW.
english.DeletePresets=Also delete the current user's personal OpenDuck Pro X presets? Select No to preserve them. This does not delete DAW projects.
italian.PreflightBusy=Open Duck è in uso. Salva il progetto e chiudi completamente la DAW e le altre applicazioni audio che usano il plugin, poi riprova. L'installer non chiude alcuna applicazione e non ha iniziato a modificare i file installati.
english.PreflightBusy=Open Duck is in use. Save your project and fully close the DAW and other audio applications using the plugin, then retry. Setup does not close applications and has not started changing the installed files.
italian.PreflightError=Impossibile verificare che i file di Open Duck siano disponibili. L'installazione è stata fermata prima di modificare i file. Chiudi le applicazioni audio e riprova; se il problema continua, conserva il log del setup.
english.PreflightError=Setup could not verify that the Open Duck files are available. Installation stopped before changing the files. Close audio applications and retry; if the problem persists, retain the setup log.
italian.PreflightApplications=Programmi rilevati: 
english.PreflightApplications=Detected applications: 

[Messages]
italian.WelcomeLabel2=Questo programma installerà Open Duck {#ProductVersion}, prodotto da Black Circuit, VST3 Windows x64.%n%n700 preset con inviluppi distinti e categorie, 16 shape e richiamo dello stato di ogni istanza.%n%nChiudi la DAW prima di aggiornare il plugin. I preset personali vengono conservati durante gli aggiornamenti.%n%nPremi Avanti per continuare.
english.WelcomeLabel2=This program will install Open Duck {#ProductVersion}, produced by Black Circuit, a Windows x64 VST3 plugin.%n%n700 presets with distinct envelopes and categories, 16 shapes and per-instance session recall.%n%nClose your DAW before updating the plugin. Personal presets are preserved during updates.%n%nClick Next to continue.

[Files]
Source: "{#ProjectRoot}\build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\*"; DestDir: "{app}\Contents"; Excludes: "*.pdb,*.ilk,*.ipdb,*.iobj"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef QA
Source: "LICENSE.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
Source: "RELEASE-NOTES.txt"; DestDir: "{app}\Documentation"; Flags: ignoreversion
#else
Source: "LICENSE.txt"; DestDir: "{autopf}\OpenDuck Pro X"; Flags: ignoreversion
Source: "RELEASE-NOTES.txt"; DestDir: "{autopf}\OpenDuck Pro X"; Flags: ignoreversion
#endif
Source: "{#ProjectRoot}\installer\OriginalTechno.wav"; Flags: dontcopy
Source: "{#ProjectRoot}\build\installer-preflight\OpenDuck-Installer-Preflight.exe"; Flags: dontcopy

[Code]
var
  AudioButton: TNewButton;
  AudioOn: Boolean;

function PlaySound(pszSound: String; hmod: Integer; fdwSound: Cardinal): Boolean;
  external 'PlaySoundW@winmm.dll stdcall';
function StopSound(pszSound: Integer; hmod: Integer; fdwSound: Cardinal): Boolean;
  external 'PlaySoundW@winmm.dll stdcall';

procedure StopMusic();
begin
  StopSound(0, 0, 0);
end;

procedure AudioClick(Sender: TObject);
begin
  AudioOn := not AudioOn;
  if AudioOn then begin
    AudioOn := PlaySound(ExpandConstant('{tmp}\OriginalTechno.wav'), 0, $00020009);
    Log('Installer audio playback requested; success=' + IntToStr(Ord(AudioOn)));
    if AudioOn then AudioButton.Caption := 'AUDIO ON' else AudioButton.Caption := 'AUDIO OFF';
  end else begin
    StopMusic();
    AudioButton.Caption := 'AUDIO OFF';
  end;
end;

procedure InitializeWizard();
begin
  ExtractTemporaryFile('OriginalTechno.wav');
  AudioButton := TNewButton.Create(WizardForm);
  AudioButton.Parent := WizardForm;
  AudioButton.Left := ScaleX(12);
  AudioButton.Top := WizardForm.NextButton.Top;
  AudioButton.Width := ScaleX(100);
  AudioButton.Caption := 'AUDIO OFF';
  AudioButton.OnClick := @AudioClick;
  AudioOn := False;
  if (not WizardSilent) and (ExpandConstant('{param:NOAUDIO|0}') <> '1') then AudioClick(AudioButton);
end;

procedure DeinitializeSetup();
begin
  StopMusic();
end;

function PreflightApplicationNames(const Details: String): String;
var
  Lines: TStringList;
  I, Count: Integer;
  Name: String;
begin
  Result := '';
  Lines := TStringList.Create;
  try
    Lines.Text := Details;
    Count := 0;
    for I := 0 to Lines.Count - 1 do
      if Pos('PROCESS_NAME=', Lines[I]) = 1 then begin
        Name := Trim(Copy(Lines[I], 14, 100));
        if (Name <> '') and (Pos(Name, Result) = 0) then begin
          if Result <> '' then Result := Result + ', ';
          Result := Result + Name;
          Count := Count + 1;
          if Count >= 3 then Break;
        end;
      end;
  finally
    Lines.Free;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Helper, ReportFile, Parameters, Details, Applications: String;
  ReportBytes: AnsiString;
  ResultCode: Integer;
begin
  Result := '';
  NeedsRestart := False;
#ifdef QA
  if CompareText(RemoveBackslashUnlessRoot(ExpandFileName(ExpandConstant('{app}'))),
                 RemoveBackslashUnlessRoot(ExpandFileName('{#QARoot}\OpenDuck Pro X.vst3'))) <> 0 then begin
    Result := 'This QA installer is restricted to its project test directory.';
    Exit;
  end;
#endif
  // Check before Inno enters installation: moduleinfo and every other payload
  // file remain untouched when the DLL is loaded. No shutdown/restart is used.
  try
    ExtractTemporaryFile('OpenDuck-Installer-Preflight.exe');
    Helper := ExpandConstant('{tmp}\OpenDuck-Installer-Preflight.exe');
    ReportFile := ExpandConstant('{tmp}\OpenDuck-Preflight.txt');
    Parameters := '--path "' + ExpandConstant('{app}\Contents\x86_64-win\OpenDuck Pro X.vst3') +
      '" --path "' + ExpandConstant('{app}\Contents\Resources\moduleinfo.json') +
      '" --report "' + ReportFile + '"';
    ResultCode := 33;
    if not Exec(Helper, Parameters, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
      Log('ODX_PREFLIGHT_HELPER_START_ERROR=' + IntToStr(ResultCode));
      Result := CustomMessage('PreflightError');
      Exit;
    end;
    Details := '';
    if LoadStringFromFile(ReportFile, ReportBytes) then
      Details := UTF8Decode(ReportBytes);
    Log('ODX_PREFLIGHT_HELPER_EXIT=' + IntToStr(ResultCode));
    Log(Details);
    if ResultCode = 32 then begin
      Result := CustomMessage('PreflightBusy');
      Applications := PreflightApplicationNames(Details);
      if Applications <> '' then
        Result := Result + #13#10#13#10 + CustomMessage('PreflightApplications') + ' ' + Applications;
    end else if ResultCode <> 0 then
      Result := CustomMessage('PreflightError');
  except
    Log('ODX_PREFLIGHT_EXCEPTION=' + GetExceptionMessage);
    Result := CustomMessage('PreflightError');
  end;
end;

// Presets are never part of [Files] or [UninstallDelete]. Silent uninstall preserves
// them. Interactive removal requires a separate explicit Yes, with No as default.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Presets: String;
begin
  if CurUninstallStep = usPostUninstall then begin
#ifdef QA
    Presets := ExpandConstant('{app}\..\Presets');
#else
    Presets := ExpandConstant('{userappdata}\OpenDuck\Pro X\Presets');
#endif
    if DirExists(Presets) and not UninstallSilent then
      if MsgBox(CustomMessage('DeletePresets'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(Presets, True, True, True);
  end;
end;
