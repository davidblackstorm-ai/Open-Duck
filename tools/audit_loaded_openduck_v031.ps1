[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][int]$LiveProcessId,
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$ExpectedHostExecutable,
    [Parameter(Mandatory=$true)][ValidatePattern('^\d{16,20}$')][string]$ExpectedStartTimeUtcTicks,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedReleaseBinarySHA256,
    [string]$ExpectedModulePath = '',
    [string]$Output = ''
)
# OS module metadata/on-disk hashes only. Never launches/stops Live, reads a
# process dump or arbitrary memory, edits ALS/prefs, or installs/copies a VST.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$releaseHash=$ExpectedReleaseBinarySHA256.ToLowerInvariant()
function Safe-Path([string]$Value) {
    if ($Value -notmatch '^[A-Za-z]:[\\/]' -or $Value.Substring(2) -match ':' -or $Value -match '(^|[\\/])\.{1,2}([\\/]|$)') { throw 'Absolute local path without traversal/ADS required' }
    $full=[IO.Path]::GetFullPath($Value).TrimEnd('\','/')
    if ($full.Length -le 3) { throw 'Drive root forbidden' }
    $walk=$full
    while ($walk) {
        $entry=Get-Item -LiteralPath $walk -Force -ErrorAction SilentlyContinue
        if ($null -ne $entry -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse path forbidden: $walk" }
        $parent=[IO.Path]::GetDirectoryName($walk)
        if (-not $parent -or $parent -eq $walk) { break }
        $walk=$parent
    }
    return $full
}
function Hash([string]$Path) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant() }
function Same-Path([string]$First,[string]$Second) { return $First.Equals($Second,[StringComparison]::OrdinalIgnoreCase) }
function Project-File([string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.{1,2}([\\/]|$)|:') { throw 'Invalid project-relative evidence path' }
    $candidate=Safe-Path (Join-Path $project $Relative)
    if (-not $candidate.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or -not [IO.File]::Exists($candidate)) { throw 'Project evidence missing or escaped boundary' }
    return $candidate
}
function Assert-Identity($Process,[string]$Executable) {
    $Process.Refresh()
    if ($Process.HasExited -or $Process.ProcessName -notlike 'Ableton Live*' -or $Process.ProcessName -like '*Beta*') { throw 'Selected stable Live process is unavailable' }
    $path=Safe-Path $Process.Path
    $ticks=$Process.StartTime.ToUniversalTime().Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
    if (-not (Same-Path $path $Executable) -or $ticks -cne $ExpectedStartTimeUtcTicks) { throw 'Live PID/executable/start-time identity mismatch' }
}
function Assert-FinalNativeProof($Native,[string]$ReleaseHash) {
    $requiredChecks=@('legacy','dsp','quality','audio','ui','ai','bridge','parity','host','lifecycle','vst3_lifecycle','state_commit','assist_exception','steinberg','pluginval','waveform_capture','scope_freeze','scope_edge','waveform_ui')
    $counts=[ordered]@{legacy=76;dsp=51;quality=16;audio=35;ui=43;ai=76;bridge=179;parity=15;host=11;
        lifecycle=41;vst3_lifecycle=82;state_commit=7;assist_exception=6;
        waveform_capture=108;scope_freeze=18;scope_edge=21;waveform_ui=109}
    if (@($counts.Values | Where-Object {$null -eq $_}).Count) { throw 'Final waveform UI count has not been approved; this auditor cannot certify a release yet' }
    if ($Native.version -cne '0.3.1' -or $Native.complete -ne $true -or $Native.releaseBinarySHA256 -cne $ReleaseHash) { throw 'Complete final non-DAW031 report for the explicitly expected DLL required' }
    $names=@($Native.checks.PSObject.Properties.Name | Sort-Object)
    if (($names -join '|') -cne (($requiredChecks | Sort-Object) -join '|')) { throw 'Exactly nineteen final native gates required' }
    if ((@($Native.expectedNativeCounts.PSObject.Properties.Name|Sort-Object) -join '|') -cne (($counts.Keys|Sort-Object) -join '|')) { throw 'Exactly seventeen native count declarations required' }
    foreach($name in $requiredChecks) {
        $check=$Native.checks.$name
        if ($check.passed -ne $true -or $check.exitCode -ne 0) { throw "Native gate unresolved: $name" }
        if ($counts.Contains($name) -and (-not ($check.count -is [int] -or $check.count -is [long]) -or -not ($Native.expectedNativeCounts.$name -is [int] -or $Native.expectedNativeCounts.$name -is [long]) -or $check.count -ne $counts[$name] -or $Native.expectedNativeCounts.$name -ne $counts[$name])) { throw "Native count differs from approved regression count: $name" }
        if ($name -eq 'steinberg' -and (-not ($check.count -is [int] -or $check.count -is [long]) -or $check.count -ne 47)) { throw 'Base Steinberg47 count required' }
    }
    $extras=@('lifecycle','vst3_lifecycle','state_commit','assist_exception','waveform_capture','scope_freeze','scope_edge','waveform_ui')
    if ((@($Native.expectedExtraCounts.PSObject.Properties.Name|Sort-Object) -join '|') -cne (($extras|Sort-Object) -join '|')) { throw 'Exactly eight approved lifecycle/waveform count declarations required' }
    foreach($name in $extras) { if(-not ($Native.expectedExtraCounts.$name -is [int] -or $Native.expectedExtraCounts.$name -is [long]) -or $Native.expectedExtraCounts.$name -ne $counts[$name]) { throw "Extra regression count changed: $name" } }
    if ($Native.pairedAudioEquivalence.passed -ne $true) { throw 'Actual paired-audio gate is not complete' }
}
function Assert-ReleaseFileEvidence([string]$ReleaseHash,[string]$ExpectedBinary,[string]$Selected,[string]$NativePath,[string]$BaselinePath,[string]$BaselineDigestPath) {
    if ($ReleaseHash -cnotmatch '^[0-9a-f]{64}$' -or (Hash $ExpectedBinary) -cne $ReleaseHash -or (Hash $Selected) -cne $ReleaseHash) { throw 'Build/selected DLL differs from the explicit expected release hash' }
    $nativeHash=Hash $NativePath
    $native=[IO.File]::ReadAllText($NativePath) | ConvertFrom-Json
    Assert-FinalNativeProof $native $ReleaseHash
    foreach($property in $native.checks.PSObject.Properties) {
        $name=$property.Name;$check=$property.Value
        if ((Hash (Project-File $check.log)) -cne $check.logSHA256) { throw "Native log changed: $name" }
        $exe=Safe-Path ([string]$check.command[0])
        if (-not $exe.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or (Hash $exe) -cne $check.executableSHA256) { throw "Native executable changed: $name" }
    }
    $baselineHash=Hash $BaselinePath
    if ($baselineHash -cne [IO.File]::ReadAllText($BaselineDigestPath).Trim()) { throw 'Baseline integrity check failed' }
    $baseline=[IO.File]::ReadAllText($BaselinePath) | ConvertFrom-Json
    if ($baseline.version -cne '0.3.1' -or $baseline.baselineComplete -ne $true -or $baseline.targetBinarySHA256 -cne $ReleaseHash -or -not (Same-Path ([string]$baseline.targetBinary) $ExpectedBinary)) { throw 'Final target differs from the031 baseline' }
    if ($baseline.PSObject.Properties['fixtureOnly'] -and $baseline.fixtureOnly -ne $false) { throw 'Synthetic baselines cannot certify an actual Live module' }
    return [ordered]@{native=$native;nativeHash=$nativeHash;baseline=$baseline;baselineHash=$baselineHash}
}
try {
    if ($LiveProcessId -le 0) { throw 'Positive explicit Live process ID required' }
    $project=Safe-Path $ProjectRoot
    $expected=Project-File 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3'
    if ((Hash $expected) -cne $releaseHash -or [Diagnostics.FileVersionInfo]::GetVersionInfo($expected).ProductVersion -cne '0.3.1') { throw 'Final DLL hash/ProductVersion does not identify the approved031 build' }
    $selected=if($ExpectedModulePath){ Safe-Path $ExpectedModulePath }else{ $expected }
    if (-not [IO.File]::Exists($selected) -or (Hash $selected) -cne $releaseHash) { throw 'Selected loaded-module path does not contain the approved031 binary' }
    $nativePath=Project-File 'build\non-daw-v0.3.1.json'
    $qaRoot=Safe-Path (Join-Path $project 'work\Ableton-QA-v0.3.1')
    $baselinePath=Project-File 'work\Ableton-QA-v0.3.1\baseline.json'
    $digestPath=Project-File 'work\Ableton-QA-v0.3.1\baseline.json.sha256'
    $evidence=Assert-ReleaseFileEvidence $releaseHash $expected $selected $nativePath $baselinePath $digestPath
    $native=$evidence.native;$nativeHash=$evidence.nativeHash;$baseline=$evidence.baseline;$baselineHash=$evidence.baselineHash
    $hostExecutable=Safe-Path $ExpectedHostExecutable
    $hostVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($hostExecutable).ProductVersion
    if ($hostVersion -notmatch '^12\.4\.15(?:\.0)?$') { throw 'Only the explicitly selected stable Live12.4.15 is in scope' }
    $live=Get-Process -Id $LiveProcessId
    Assert-Identity $live $hostExecutable
    $records=@()
    foreach($module in @($live.Modules | Where-Object { [IO.Path]::GetFileName($_.FileName) -ieq 'OpenDuck Pro X.vst3' })) {
        $path=Safe-Path $module.FileName
        $hash=Hash $path
        $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($path).ProductVersion
        $records += [ordered]@{path=$path;sha256=$hash;productVersion=$version;
            expectedProjectPath=(Same-Path $path $expected);expectedSelectedPath=(Same-Path $path $selected);
            matchesTargetHash=($hash -ceq $releaseHash)}
    }
    Assert-Identity (Get-Process -Id $LiveProcessId) $hostExecutable
    if ((Hash $expected) -cne $releaseHash -or (Hash $selected) -cne $releaseHash -or (Hash $nativePath) -cne $nativeHash -or (Hash $baselinePath) -cne $baselineHash) { throw 'Evidence/module changed during enumeration' }
    $allFinal=$records.Count -gt 0 -and @($records | Where-Object {-not $_.matchesTargetHash -or $_.productVersion -cne '0.3.1'}).Count -eq 0
    $allSelected=$records.Count -gt 0 -and @($records | Where-Object {-not $_.expectedSelectedPath}).Count -eq 0
    $allProject=$records.Count -gt 0 -and @($records | Where-Object {-not $_.expectedProjectPath}).Count -eq 0
    $report=[ordered]@{schema='openduck-loaded-module031-v1';version='0.3.1';atUTC=[DateTime]::UtcNow.ToString('O');
        processId=$LiveProcessId;processName=$live.ProcessName;processStartTimeUtcTicks=$ExpectedStartTimeUtcTicks;
        processStartTimeUtc=$live.StartTime.ToUniversalTime().ToString('O');hostExecutable=$hostExecutable;hostProductVersion=$hostVersion;
        expectedBinary=$expected;expectedLoadedBinary=$selected;expectedSHA256=$releaseHash;baselineSHA256=$baselineHash;
        nonDAWReportSHA256=$nativeHash;auditToolSHA256=(Hash $PSCommandPath);loadedOpenDuckModules=$records;
        checks=[ordered]@{openDuckModuleFound=($records.Count -gt 0);allLoadedOpenDuckModulesAreFinalProjectBinary=($allFinal -and $allProject);
            allLoadedOpenDuckModulesMatchFinalBinary=$allFinal;allLoadedOpenDuckModulesMatchExpectedSelectedPath=$allSelected;
            stableHostIdentityMatched=$true;finalNineteenNativeGatesMatched=$true};
        proofPassed=($allFinal -and $allSelected);
        scope='Read-only OS loader metadata/on-disk hashes, explicit PID/executable/start identity; no DAW/UI launch, ALS/prefs change, dump, arbitrary process-memory reading or installation.'}
    $rendered=($report | ConvertTo-Json -Depth 12)+[Environment]::NewLine
    if($Output) {
        $out=Safe-Path $Output
        if ([IO.Path]::GetDirectoryName($out) -ine $qaRoot -or [IO.Path]::GetFileName($out) -cne 'LOADED-MODULES.json') { throw 'Use only the new LOADED-MODULES.json directly inside031 QA root' }
        $stream=[IO.File]::Open($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {$bytes=[Text.UTF8Encoding]::new($false).GetBytes($rendered);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    }
    $rendered
    if($report.proofPassed){exit 0}else{exit 2}
}catch{
    [Console]::Error.WriteLine('Loaded-module031 evidence failed: '+$_.Exception.Message)
    exit 1
}
