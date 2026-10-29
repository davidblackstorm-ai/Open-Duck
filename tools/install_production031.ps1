[CmdletBinding()]
param()

# PREPARED FOR THE COORDINATOR; NOT EXECUTED BY ITS AUTHOR.
# One authorized official production Setup, then file-only Ableton QA baseline.
# Never opens Live, automates UAC, stops a process, or writes a personal preset.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Project = 'E:\Codex\Lavori\OpenDuck Pro X'
$Python = 'C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$PowerShell = 'C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
$Setup = Join-Path $Project 'releases\OpenDuck-Pro-X-0.3.1-Windows-x64-Setup.exe'
$Bundle = 'C:\Program Files\Common Files\VST3\OpenDuck Pro X.vst3'
$InstalledContents = Join-Path $Bundle 'Contents'
$InstalledBinary = Join-Path $InstalledContents 'x86_64-win\OpenDuck Pro X.vst3'
$SourceContents = Join-Path $Project 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents'
$SourceBinary = Join-Path $SourceContents 'x86_64-win\OpenDuck Pro X.vst3'
$InstalledDocuments = 'C:\Program Files\OpenDuck Pro X'
$Presets = 'C:\Users\PC\AppData\Roaming\OpenDuck\Pro X\Presets'
$Preferences = 'C:\Users\PC\AppData\Roaming\Ableton\Live 12.4.15\Preferences'
$OriginalSet = Join-Path $Project 'OpenDuck Pro X - TEST LAB Project\OpenDuck Pro X - TEST LAB - 0.2.0.als'
$QaSet = Join-Path $Project 'OpenDuck Pro X - TEST LAB Project\OpenDuck Pro X - TEST LAB - 0.3.1.als'
$QaRoot = Join-Path $Project 'work\Ableton-QA-v0.3.1'
$BaselineHelper = Join-Path $Project 'tools\prepare_ableton_qa_v031.ps1'
$NonDawPath = Join-Path $Project 'build\non-daw-v0.3.1.json'
$ProductionManifest = Join-Path $Project 'build\installer-production-v0.3.1.json'
$RegistryKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{A2C52811-2937-42FA-A186-0100469D28E5}_is1'
$Utf8 = [Text.UTF8Encoding]::new($false)

function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Assert-LiveClosed {
    $live = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'Ableton Live*' })
    Require ($live.Count -eq 0) 'An Ableton Live process is open; nothing will close or force-stop it'
}
function Read-Json([string]$Path) {
    [void](Assert-InstallerPath $Path)
    $file = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    Require (-not $file.PSIsContainer -and $file.Length -lt 16777216) 'Invalid or oversized evidence JSON'
    return [IO.File]::ReadAllText($Path) | ConvertFrom-Json
}
function Evidence-Time($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    $text = [string]$Value
    if ($text -match '^\d{8}T\d{12}Z$') {
        return [DateTime]::ParseExact($text,'yyyyMMddTHHmmssffffffZ',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::AssumeUniversal).ToUniversalTime()
    }
    return [DateTime]::Parse($text,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
function Assert-HashRecord([string]$Path,[string]$Expected) {
    Require ($Expected -cmatch '^[0-9a-f]{64}$') 'Invalid expected SHA256'
    Require ((Get-InstallerFileHash $Path) -ceq $Expected) "Evidence fingerprint changed: $Path"
}
function Registry-Product {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $base.OpenSubKey($RegistryKey,$false)
        if (-not $key) { return $null }
        try { return [ordered]@{displayName=[string]$key.GetValue('DisplayName'); displayVersion=[string]$key.GetValue('DisplayVersion'); installLocation=[string]$key.GetValue('InstallLocation')} }
        finally { $key.Dispose() }
    } finally { $base.Dispose() }
}
function Assert-NonDawContract031($Proof) {
    $expectedCounts=[ordered]@{legacy=76;dsp=51;quality=16;audio=35;ui=43;ai=76;bridge=179;parity=15;host=11;lifecycle=41;vst3_lifecycle=82;state_commit=7;assist_exception=6;waveform_capture=108;scope_freeze=18;scope_edge=21;waveform_ui=109}
    $expectedExtra=[ordered]@{lifecycle=41;vst3_lifecycle=82;state_commit=7;assist_exception=6;waveform_capture=108;scope_freeze=18;scope_edge=21;waveform_ui=109}
    $gateNames=@($expectedCounts.Keys)+@('steinberg','pluginval')
    Require ($Proof.version -ceq '0.3.1' -and $Proof.complete -eq $true -and @($Proof.checks.PSObject.Properties).Count -eq 19 -and $Proof.pairedAudioEquivalence.passed -eq $true) 'Complete current19-gate non-DAW manifest and actual paired audio PASS required'
    Require (@(Compare-Object -CaseSensitive -ReferenceObject $gateNames -DifferenceObject @($Proof.checks.PSObject.Properties.Name)).Count -eq 0) 'Exact nineteen mandatory check names required'
    foreach($pair in @(@($Proof.expectedNativeCounts,$expectedCounts),@($Proof.expectedExtraCounts,$expectedExtra))){
        Require (@($pair[0].PSObject.Properties).Count -eq $pair[1].Count) 'Reviewed native/waveform count map is missing'
        foreach($name in $pair[1].Keys){Require ($null -ne $pair[1][$name] -and $pair[0].$name -eq $pair[1][$name]) "Reviewed strict count map mismatch: $name"}
    }
    foreach($name in $gateNames){
        $check=$Proof.checks.$name
        Require ($check.passed -eq $true -and $check.exitCode -eq 0) "Non-DAW check failed: $name"
        if($expectedCounts.Contains($name)){Require ($check.count -eq $expectedCounts[$name]) "Native/waveform assertion count mismatch: $name"}
    }
    Require ($Proof.checks.steinberg.count -eq 47 -and $null -eq $Proof.checks.pluginval.count) 'Steinberg47/pluginval SUCCESS summaries required'
    Require (@($Proof.checks.steinberg.variants.PSObject.Properties).Count -eq 2) 'Both extensive Steinberg lifetimes required'
    foreach($name in @('extensive-global','extensive-local')){
        $variant=$Proof.checks.steinberg.variants.$name
        Require ($variant.passed -eq $true -and $variant.exitCode -eq 0 -and $variant.count -eq 537) 'Both extensive Steinberg lifetimes must pass537 tests'
    }
    Require (@($Proof.configurationSHA256.PSObject.Properties).Count -eq 2 -and @(
        Compare-Object -CaseSensitive -ReferenceObject @('CMakeLists.txt','cmake/OpenDuckWindowsResources.cmake') -DifferenceObject @($Proof.configurationSHA256.PSObject.Properties.Name)
    ).Count -eq 0) 'CMake project/resource-helper fingerprints required'
    foreach($relative in @('build/ai-quality-lineage-v0.3.1.json','build/quality-audio-v0.3.1.json','build/host-hotfix-audio-equivalence-v0.3.1.json','build/ai-heldout-v0.3.1.json','build/visual-qa-v0.3.1/dsp030-measures.json','build/visual-qa-v0.3.1/assist-bridge-summary.json','build/visual-qa-v0.3.1/scope-capture031-measures.json','build/visual-qa-v0.3.1/native-waveform-width1010.png')){
        Require ($null -ne $Proof.generatedEvidenceSHA256.PSObject.Properties[$relative]) "Required current evidence missing: $relative"
    }
    foreach($relative in @('docs/research-v0.3.0/golden.csv','docs/research-v0.3.0/training.json','docs/research-v0.3.0/teacher-final/manifest.json','work/hotfix031/waveform/SCOPE-EXACT-TRANSFORMS.json')){
        Require ($null -ne $Proof.inputEvidenceSHA256.PSObject.Properties[$relative]) "Required R2/scope input fingerprint missing: $relative"
    }
    foreach($relative in @('tools/verify_revision_v031.py','tools/quality_host_hotfix_lineage031.py','tools/verify_quality_audio031.py','tools/verify_host_hotfix_audio_equivalence031.py')){
        Require ($null -ne $Proof.verifierToolSHA256.PSObject.Properties[$relative]) "Required verifier tool fingerprint missing: $relative"
    }
    return [pscustomobject]@{gateNames=$gateNames;expectedCounts=$expectedCounts}
}
function Assert-CurrentNonDawFingerprints031($Proof) {
    foreach ($dictionary in @($Proof.sourceSHA256,$Proof.configurationSHA256,$Proof.inputEvidenceSHA256,$Proof.verifierToolSHA256,$Proof.generatedEvidenceSHA256)) {
        Require (@($dictionary.PSObject.Properties).Count -gt 0) 'Missing current-source/configuration/evidence/tool fingerprints'
        foreach ($entry in $dictionary.PSObject.Properties) { Assert-HashRecord (Assert-InstallerPath (Join-Path $Project $entry.Name) $Project) $entry.Value }
    }
    $actual=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach($directory in @('Source','tests')){
        foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $Project $directory) -Recurse -File -Force)){
            [void](Assert-InstallerPath $file.FullName $Project)
            $relative=$file.FullName.Substring($Project.Length+1).Replace('\','/')
            $actual.Add($relative,(Get-InstallerFileHash $file.FullName))
        }
    }
    Require ($actual.Count -eq @($Proof.sourceSHA256.PSObject.Properties).Count) 'Current Source/tests inventory differs from the verified build'
    foreach($entry in $Proof.sourceSHA256.PSObject.Properties){Require ($actual.ContainsKey($entry.Name) -and $actual[$entry.Name] -ceq $entry.Value) 'Current Source/tests inventory fingerprint mismatch'}
}
function Validate-InstallerRelease {
    # Real read-only validator: this is validate(), never a renderer or a cached
    # document claiming PASS. -B prevents import cache writes to project tools.
    $code = 'import json,sys;from pathlib import Path;r=Path(sys.argv[1]);sys.path.insert(0,str(r/"tools"));import installer_release_evidence_v031 as e;print(json.dumps(e.validate(r)))'
    $lines = & $Python -B -c $code $Project 2>&1
    $codeExit = $LASTEXITCODE
    Require ($codeExit -eq 0) ('Current installer_release_evidence_v031.validate failed: ' + ($lines -join [Environment]::NewLine))
    $result = ($lines -join [Environment]::NewLine) | ConvertFrom-Json
    Require ($result.passed -is [bool] -and $result.passed -and $result.productionInstallPerformed -eq $false) 'Current installer binding did not pass'
    return $result
}

foreach ($path in @($Project,$Python,$PowerShell)) {
    Require (Test-Path -LiteralPath $path) "Missing fixed dependency: $path"
}
. (Join-Path $Project 'tools\installer_evidence_v031.ps1') # Functions only; no mutation on import.
foreach ($path in @($Project,$Python,$PowerShell,$Setup,$Bundle,$SourceBinary,$InstalledDocuments,$Presets,$Preferences,$OriginalSet,$QaSet,$QaRoot,$BaselineHelper,$NonDawPath,$ProductionManifest)) {
    [void](Assert-InstallerPath $path)
}
Require ([Environment]::GetFolderPath('ApplicationData') -ieq 'C:\Users\PC\AppData\Roaming') 'Current user is not the explicitly preserved PC profile'
Assert-LiveClosed
Require (-not (Test-Path -LiteralPath $QaRoot)) 'QA031 baseline directory already exists; refusing overwrite or a second installation attempt'
Require (-not (Test-Path -LiteralPath $QaSet)) 'Planned QA031 set already exists; refusing overwrite'
Require ([IO.File]::Exists($OriginalSet) -and [IO.Directory]::Exists($Preferences)) 'Named TESTLAB0.2 or Live12.4.15 profile is missing'

# Record the token without imposing an extra elevation requirement. Ordinary
# official Setup may ask Windows for consent; this script never automates or
# bypasses that dialog and never invokes RunAs. Cancellation is an install FAIL.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$attempt = Assert-InstallerPath (Join-Path $Project ('work\v0.3.1\production-install-private\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N'))) $Project
[void][IO.Directory]::CreateDirectory($attempt)
$reportPath = Join-Path $attempt 'installation.json'
$installLog = Join-Path $attempt 'official-setup.log'
$report = [ordered]@{schema='openduck-production-install-v1';version='0.3.1';startedUTC=[DateTime]::UtcNow.ToString('O');complete=$false;
    productionInstallAttempted=$false;productionInstallPerformed=$false;installedVerified=$false;dawLaunched=$false;setup=$Setup;setupLog=$installLog;installedBundle=$Bundle;
    scriptSHA256=(Get-InstallerFileHash $PSCommandPath);administrativeToken=$isAdmin;currentUser=$identity.Name;privateArtifacts=$true}
function Save-Report {
    [void](Assert-InstallerPath $reportPath $attempt)
    $temporary = $reportPath + '.tmp'
    [IO.File]::WriteAllText($temporary,($report | ConvertTo-Json -Depth 40),$Utf8)
    [IO.File]::Move($temporary,$reportPath,$true)
}
$beforePresets = $null
try {
    Save-Report
    $orchestratorRoot = Assert-InstallerPath (Join-Path $Project 'work\v0.3.1\orchestrator-private') $Project
    $attempts = @(Get-ChildItem -LiteralPath $orchestratorRoot -Directory -Force | Sort-Object Name -Descending)
    Require ($attempts.Count -gt 0 -and $attempts.Count -lt 1000) 'No bounded latest orchestration inventory'
    $latest = $attempts[0]; [void](Assert-InstallerPath $latest.FullName $orchestratorRoot)
    $orchestrationPath = Assert-InstallerPath (Join-Path $latest.FullName 'orchestration.json') $latest.FullName
    $orchestration = Read-Json $orchestrationPath
    Require ($orchestration.schema -ceq 'openduck-host-hotfix-orchestration-v1' -and $orchestration.version -ceq '0.3.1' -and $orchestration.complete -eq $true -and $orchestration.productionInstallPerformed -eq $false -and $orchestration.dawLaunched -eq $false) 'Latest orchestration is incomplete, failed or wrong; no fallback to an older PASS'
    Require ([IO.Path]::GetFullPath([string]$orchestration.project) -ieq $Project) 'Orchestration project mismatch'
    Assert-HashRecord (Join-Path $Project 'tools\run_host_hotfix031.ps1') $orchestration.scriptSHA256
    $stageNames = @('01-stop-owned-stale-trial','02-rebuild-18-targets-and-version-guard','03-full-19-gate-verification','04-isolated-installer-qa','05-compile-official-installer','06-bind-installer-release-evidence')
    $stageExecutables=@('', 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',$Python,$PowerShell,$PowerShell,$Python)
    Require (@($orchestration.stages).Count -eq 6) 'Exactly six complete orchestration stages required'
    for ($index=0;$index -lt 6;$index++) {
        $stage = $orchestration.stages[$index]
        Require ($stage.name -ceq $stageNames[$index] -and $stage.passed -eq $true -and (Evidence-Time $stage.finishedUTC) -ge (Evidence-Time $stage.startedUTC)) 'Missing, reordered or failed orchestration stage'
        if ($index -gt 0) { Require ($stage.result.exitCode -eq 0 -and $stage.result.executable -ieq $stageExecutables[$index]) 'Orchestration process identity/exit mismatch'; Assert-HashRecord $stage.result.executable $stage.result.executableSHA256 }
    }
    Require ((Evidence-Time $orchestration.finishedUTC) -ge (Evidence-Time $orchestration.stages[5].finishedUTC)) 'Orchestration completion time precedes final stage'
    $proof = Read-Json $NonDawPath
    $contract=Assert-NonDawContract031 $proof
    $gateNames=$contract.gateNames
    $proofTime = Evidence-Time $proof.startedUTC
    Require ($proofTime -ge (Evidence-Time $orchestration.stages[2].startedUTC).AddSeconds(-1) -and $proofTime -le (Evidence-Time $orchestration.stages[2].finishedUTC)) 'Current non-DAW result is not from the latest orchestration'
    Assert-HashRecord $SourceBinary $proof.releaseBinarySHA256
    Assert-HashRecord (Join-Path $SourceContents 'Resources\moduleinfo.json') $proof.bundleMetadataSHA256
    foreach ($name in $gateNames) {
        $check = $proof.checks.$name
        Require ($check.passed -eq $true -and $check.exitCode -eq 0) "Non-DAW check failed: $name"
        Assert-HashRecord (Assert-InstallerPath (Join-Path $Project $check.log) $Project) $check.logSHA256
        Assert-HashRecord (Assert-InstallerPath $check.command[0] $Project) $check.executableSHA256
    }
    foreach ($name in @('extensive-global','extensive-local')) {
        $variant=$proof.checks.steinberg.variants.$name
        Require ($variant.passed -eq $true -and $variant.exitCode -eq 0 -and $variant.count -eq 537) 'Both extensive Steinberg lifetimes must pass537 tests'
        Assert-HashRecord (Assert-InstallerPath (Join-Path $Project $variant.log) $Project) $variant.logSHA256
    }
    Assert-CurrentNonDawFingerprints031 $proof
    $production = Read-Json $ProductionManifest
    $binding = Validate-InstallerRelease
    Require ($production.version -ceq '0.3.1' -and $production.complete -eq $true -and $production.compileOnly -eq $true -and $production.productionInstallPerformed -eq $false) 'Only a current compile-only official Setup manifest is accepted'
    Require ($production.setup.relative -ceq 'releases/OpenDuck-Pro-X-0.3.1-Windows-x64-Setup.exe' -and $production.setup.productVersion -ceq '0.3.1') 'Wrong official Setup identity'
    Assert-HashRecord $Setup $production.setup.sha256
    Require ((Get-Item -LiteralPath $Setup).Length -eq $production.setup.length -and (Get-Item -LiteralPath $Setup).VersionInfo.ProductVersion.Trim() -ceq '0.3.1') 'Official Setup size or PE ProductVersion mismatch'
    Require ((Get-Item -LiteralPath $SourceBinary).VersionInfo.ProductVersion.Trim() -ceq '0.3.1') 'Final DLL PE ProductVersion is not0.3.1'
    $boundProof = Read-Json (Assert-InstallerPath $orchestration.stages[5].result.proof $latest.FullName)
    Require ($boundProof.passed -eq $true -and (ConvertTo-InstallerCanonicalJson $boundProof) -ceq (ConvertTo-InstallerCanonicalJson $binding)) 'Latest stage6 binding differs from a fresh validate() result'
    $registeredBefore = Registry-Product
    if ($registeredBefore -and $registeredBefore.installLocation) { Require ($registeredBefore.installLocation.TrimEnd('\') -ieq $Bundle) 'Existing official install location differs from the fixed common-VST3 target' }
    $beforePresets = Get-InstallerTreeSnapshot $Presets
    [IO.File]::WriteAllText((Join-Path $attempt 'personal-presets-before.json'),($beforePresets | ConvertTo-Json -Depth 32),$Utf8)
    $report['orchestration']=[ordered]@{path=$orchestrationPath;sha256=(Get-InstallerFileHash $orchestrationPath)}
    $report['nonDAW']=[ordered]@{path=$NonDawPath;sha256=(Get-InstallerFileHash $NonDawPath);checks=19;binarySHA256=$proof.releaseBinarySHA256}
    $report['installerBinding']=$binding;$report['registryBefore']=$registeredBefore;$report['baselineHelperSHA256']=Get-InstallerFileHash $BaselineHelper
    Save-Report
    Assert-LiveClosed
    $latestNow=@(Get-ChildItem -LiteralPath $orchestratorRoot -Directory -Force|Sort-Object Name -Descending)[0]
    Require ($latestNow.FullName -ieq $latest.FullName) 'A newer orchestration appeared; installation was not started'
    Assert-HashRecord $orchestrationPath $report.orchestration.sha256
    Assert-HashRecord $NonDawPath $report.nonDAW.sha256
    Require (-not (Test-Path -LiteralPath $QaRoot) -and -not (Test-Path -LiteralPath $QaSet)) 'QA target appeared during preflight; no installation started'
    Assert-HashRecord $Setup $binding.productionSetupSHA256
    [void](Assert-NonDawContract031 $proof)
    Assert-CurrentNonDawFingerprints031 $proof
    $report['productionInstallAttempted']=$true;Save-Report
    # The only application launched by this wrapper is the exact official Setup.
    # No RunAs/UAC automation, DAW, silent uninstaller or direct bundle overwrite.
    $setupArguments='/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOAUDIO=1 /LOG="' + $installLog + '"'
    $report['setupArguments']=$setupArguments;Save-Report
    $process=Start-Process -FilePath $Setup -ArgumentList $setupArguments -WorkingDirectory $Project -WindowStyle Hidden -PassThru
    $report['setupPid']=$process.Id;Save-Report
    Require ($process.WaitForExit(180000)) "Official Setup exceeded180s; PID=$($process.Id) is not force-stopped; inspect its private log before further work"
    $process.WaitForExit();$process.Refresh();$report['setupExitCode']=$process.ExitCode
    if ([IO.File]::Exists($installLog)) { $report['setupLogSHA256']=Get-InstallerFileHash $installLog }
    Require ($process.ExitCode -eq 0) 'Official Setup did not return exit0'
    Assert-LiveClosed
    Assert-HashRecord $InstalledBinary $proof.releaseBinarySHA256
    Require ((Get-Item -LiteralPath $InstalledBinary).VersionInfo.ProductVersion.Trim() -ceq '0.3.1') 'Installed DLL PE version mismatch'
    $qa = Read-Json (Join-Path $Project 'build\installer-qa-results-v0.3.1.json')
    [void](Assert-InstallerPayloadMatches $qa.payloadManifest $InstalledContents 'Official production install')
    $moduleInfo=Join-Path $InstalledContents 'Resources\moduleinfo.json'
    Assert-HashRecord $moduleInfo $proof.bundleMetadataSHA256
    $versions=@([regex]::Matches([IO.File]::ReadAllText($moduleInfo),'"Version"\s*:\s*"([^"]+)"')|ForEach-Object{$_.Groups[1].Value})
    Require ($versions.Count -gt 0 -and @($versions|Where-Object{$_ -cne '0.3.1'}).Count -eq 0) 'Installed moduleinfo version mismatch'
    $documents=@()
    foreach ($name in @('LICENSE.txt','RELEASE-NOTES.txt')) {
        $expected=Get-InstallerFileHash (Join-Path $Project ('installer\'+$name))
        Assert-HashRecord (Join-Path $InstalledDocuments $name) $expected
        $documents += [ordered]@{name=$name;sha256=$expected}
    }
    $registered = Registry-Product
    Require ($registered -and $registered.displayName -ceq 'OpenDuck Pro X' -and $registered.displayVersion -ceq '0.3.1' -and $registered.installLocation.TrimEnd('\') -ieq $Bundle) 'HKLM64 official uninstall metadata does not identify installed0.3.1 at the common target'
    $afterPresets = Get-InstallerTreeSnapshot $Presets
    [IO.File]::WriteAllText((Join-Path $attempt 'personal-presets-after.json'),($afterPresets | ConvertTo-Json -Depth 32),$Utf8)
    Assert-InstallerTreeEqual $beforePresets $afterPresets 'Current-user personal presets'
    Assert-HashRecord $SourceBinary $proof.releaseBinarySHA256
    Assert-HashRecord $Setup $binding.productionSetupSHA256
    Assert-HashRecord $BaselineHelper $report.baselineHelperSHA256
    $report['installedVerified']=$true;$report['personalPresetsUnchanged']=$true;$report['registryAfter']=$registered;$report['managedDocuments']=$documents
    Save-Report
    Assert-LiveClosed
    $baselineLines = & $PowerShell -NoProfile -NonInteractive -File $BaselineHelper -Mode Baseline -ProjectRoot $Project -PreferencesRoot $Preferences -OriginalSet $OriginalSet -QaSet $QaSet -ProductionBundle $Bundle 2>&1
    $baselineExit=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $attempt 'ableton-baseline.stdout.log'),($baselineLines -join [Environment]::NewLine),$Utf8)
    Require ($baselineExit -eq 0) 'Installed plugin verified, but file-only QA031 baseline failed; no DAW launched'
    $baselinePath=Join-Path $QaRoot 'baseline.json';$baseline=Read-Json $baselinePath
    Require ($baseline.version -ceq '0.3.1' -and $baseline.baselineComplete -eq $true -and $baseline.targetBinarySHA256 -ceq $proof.releaseBinarySHA256 -and $baseline.abletonInitiallyRunning -eq $false) 'New QA031 baseline does not match the final installed DLL and closed Live state'
    Assert-LiveClosed
    $report['qaBaseline']=[ordered]@{path=$baselinePath;sha256=(Get-InstallerFileHash $baselinePath)}
    $report['complete']=$true
    $report['productionInstallPerformed']=$true
    $report['nextStage']='Coordinator may now open stable Live12.4.15 through UI, preserve/recover personal work via Save As and execute the031 QA plan. This wrapper never launches Live.'
}
catch { $report['error']=$_.Exception.Message;throw }
finally {
    $finalPresetFailure=$null
    if ($null -ne $beforePresets -and $report.productionInstallAttempted) {
        try { $finalPresets=Get-InstallerTreeSnapshot $Presets;$report['finalPresetSnapshot']=$finalPresets;Assert-InstallerTreeEqual $beforePresets $finalPresets 'Final personal preset check';$report['finalPersonalPresetsUnchanged']=$true }
        catch { $report['finalPersonalPresetsUnchanged']=$false;$report['finalPresetCheckError']=$_.Exception.Message;$report['complete']=$false;$finalPresetFailure=$_.Exception.Message }
    }
    $report['finishedUTC']=[DateTime]::UtcNow.ToString('O');Save-Report
    Write-Output "Private production installation report: $reportPath"
    if ($finalPresetFailure) { throw ('Final personal preset verification failed: ' + $finalPresetFailure) }
}
