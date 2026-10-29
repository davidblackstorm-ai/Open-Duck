[CmdletBinding()]
param()
# REVIEWABLE ORCHESTRATOR. This file has not been executed by its author.
# Stage7 production installation is deliberately separate, as agreed with the
# coordinator. No DAW, UAC elevation, source edit or arbitrary process cleanup.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectPath = 'E:\Codex\Lavori\OpenDuck Pro X'
$PythonPath = 'C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$CMakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$PowerShellPath = 'C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
$InnoPath = 'E:\Codex\Lavori\Techno-Lowend-Nexus-0.7.0\work\tools\InnoSetup\ISCC.exe'
$ExpectedInnoSHA256 = 'd06ebd38f38e3cee60a3c50cc45bd449d77e0bc6a5cabc607ea9886808e4de1a'
$RunnerPath = Join-Path $ProjectPath 'tools\verify_revision_v031.py'
$BundlePath = Join-Path $ProjectPath 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3'
$BinaryPath = Join-Path $BundlePath 'Contents\x86_64-win\OpenDuck Pro X.vst3'
$NativeTargets = @('OpenDuckTests','Revision030DspTests','Revision030QualityTests','Revision030QualityAudit',
    'Revision030UiTests','Revision030AiTests','Revision030AssistBridgeTests','AssistModelParity','OpenDuckHostContract',
    'Revision031LifecycleTests','Revision031Vst3LifecycleTests','Revision031StateCommitTests','Revision031AssistExceptionTests',
    'Revision031WaveformCaptureTests','Revision031ScopeFreezeTests','Revision031ScopeEdgeTests','Revision031WaveformUiTests')
$BuildTargets = @('OpenDuckProX_VST3') + $NativeTargets

function Assert-LocalPath([string]$Path) {
    if ($Path -notmatch '^[A-Za-z]:\\' -or $Path.Substring(2).Contains(':') -or $Path -match '(?:^|\\)\.\.(?:\\|$)') { throw "Nonlocal/ambiguous path: $Path" }
    $full = [IO.Path]::GetFullPath($Path)
    $itemPath = $full
    while ($itemPath) {
        if (Test-Path -LiteralPath $itemPath) {
            $item = Get-Item -LiteralPath $itemPath -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path refused: $itemPath" }
        }
        $parent = [IO.Directory]::GetParent($itemPath)
        if (-not $parent) { break }; $itemPath = $parent.FullName
    }
    return $full
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Same-Path([string]$First,[string]$Second) {
    if (-not $First -or -not $Second) { return $false }
    return [IO.Path]::GetFullPath($First).TrimEnd('\') -ieq [IO.Path]::GetFullPath($Second).TrimEnd('\')
}
foreach ($path in @($ProjectPath,$PythonPath,$CMakePath,$PowerShellPath,$InnoPath,$RunnerPath)) { [void](Assert-LocalPath $path); if (-not (Test-Path -LiteralPath $path)) { throw "Required path missing: $path" } }
if ((Hash $InnoPath) -cne $ExpectedInnoSHA256) { throw 'The approved Inno compiler changed' }

$AttemptPath = Join-Path $ProjectPath ('work\v0.3.1\orchestrator-private\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
[void](Assert-LocalPath $AttemptPath)
New-Item -ItemType Directory -Path $AttemptPath -ErrorAction Stop | Out-Null
$ReportPath = Join-Path $AttemptPath 'orchestration.json'
$Report = [ordered]@{schema='openduck-host-hotfix-orchestration-v1';version='0.3.1';startedUTC=[DateTime]::UtcNow.ToString('O');
    complete=$false;productionInstallPerformed=$false;productionInstallDeferred=$true;dawLaunched=$false;
    project=$ProjectPath;stages=@();scriptSHA256=(Hash $PSCommandPath)}
function Save-Report {
    $temporary = $ReportPath + '.tmp'
    [IO.File]::WriteAllText($temporary,($Report | ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary,$ReportPath,$true)
}
function Frozen-Inputs {
    $result = [ordered]@{}
    foreach ($directory in @('Source','tests','docs')) {
        $base = Join-Path $ProjectPath $directory
        foreach ($file in @(Get-ChildItem -LiteralPath $base -Recurse -File -Force | Sort-Object FullName)) {
            [void](Assert-LocalPath $file.FullName)
            $result[$file.FullName.Substring($ProjectPath.Length+1)] = Hash $file.FullName
        }
    }
    foreach ($relative in @('CMakeLists.txt','cmake\OpenDuckWindowsResources.cmake','work\hotfix031\waveform\SCOPE-EXACT-TRANSFORMS.json')) {
        $file=Join-Path $ProjectPath $relative;[void](Assert-LocalPath $file);$result[$relative]=Hash $file
    }
    return ($result | ConvertTo-Json -Compress -Depth 10)
}
$FrozenBefore = Frozen-Inputs
function Assert-Frozen { if ((Frozen-Inputs) -cne $FrozenBefore) { throw 'Source/tests/docs/NN changed; orchestration stopped' } }
function Quote-Native([string]$Value) {
    if ($Value.Contains("`r") -or $Value.Contains("`n")) { throw 'Multiline native argument refused' }
    $text = [Text.StringBuilder]::new();[void]$text.Append('"');$slashes=0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { ++$slashes;continue }
        if ($character -eq '"') { [void]$text.Append(('\' * (2*$slashes+1)));[void]$text.Append('"') }
        else { [void]$text.Append(('\' * $slashes));[void]$text.Append($character) };$slashes=0
    }
    [void]$text.Append(('\' * (2*$slashes)));[void]$text.Append('"');return $text.ToString()
}
function Invoke-OwnedCommand([string]$Label,[string]$Executable,[string[]]$Arguments,[int]$TimeoutSeconds) {
    [void](Assert-LocalPath $Executable)
    $stdout=Join-Path $AttemptPath ($Label+'.stdout.log');$stderr=Join-Path $AttemptPath ($Label+'.stderr.log')
    $process=Start-Process -FilePath $Executable -ArgumentList (($Arguments | ForEach-Object { Quote-Native $_ }) -join ' ') -WorkingDirectory $ProjectPath -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $identity=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if (-not $process.WaitForExit($TimeoutSeconds*1000)) {
        $current=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        if ($identity -and $current -and $current.CreationDate -eq $identity.CreationDate -and (Same-Path $current.ExecutablePath $Executable)) { Stop-Process -Id $process.Id -Force }
        throw "Owned stage timed out: $Label; inspect private logs and owned descendants before retry"
    }
    $process.WaitForExit();$process.Refresh()
    if ($process.ExitCode -ne 0) { throw "Stage command failed: $Label exit=$($process.ExitCode); $stdout; $stderr" }
    return [ordered]@{pid=$process.Id;exitCode=$process.ExitCode;stdout=$stdout;stderr=$stderr;executable=$Executable;executableSHA256=(Hash $Executable)}
}
function Stage([string]$Name,[scriptblock]$Body) {
    $entry=[ordered]@{name=$Name;startedUTC=[DateTime]::UtcNow.ToString('O');passed=$false}
    $Report.stages += $entry;Save-Report
    try { $entry['result']=& $Body;Assert-Frozen;$entry.passed=$true }
    catch { $entry['error']=$_.Exception.Message;throw }
    finally { $entry['finishedUTC']=[DateTime]::UtcNow.ToString('O');Save-Report }
}

Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class OpenDuckHotfixArguments031 {
 [DllImport("shell32.dll", SetLastError=true)] static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string text,out int count);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
 public static string[] Parse(string text){int count;IntPtr memory=CommandLineToArgvW(text,out count);if(memory==IntPtr.Zero)throw new InvalidOperationException("Command line decode failed");try{var result=new string[count];for(int i=0;i<count;i++)result[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(memory,i*IntPtr.Size));return result;}finally{LocalFree(memory);}}
}
'@
function Is-ExactRunner($Process) {
    if (-not (Same-Path $Process.ExecutablePath $PythonPath) -or -not $Process.CommandLine) { return $false }
    $arguments=[OpenDuckHotfixArguments031]::Parse($Process.CommandLine)
    $scriptIndex=1;if ($arguments.Count -gt 1 -and $arguments[1] -eq '-u') { $scriptIndex=2 }
    if ($arguments.Count -le $scriptIndex -or -not (Same-Path $arguments[$scriptIndex] $RunnerPath)) { return $false }
    $rootArguments=@(for ($index=$scriptIndex+1;$index -lt $arguments.Count-1;$index++) { if ($arguments[$index] -eq '--root') { $arguments[$index+1] } })
    return $rootArguments.Count -eq 1 -and (Same-Path $rootArguments[0] $ProjectPath)
}
function Stop-VerifiedProcess($Snapshot) {
    $current=Get-CimInstance Win32_Process -Filter "ProcessId=$($Snapshot.ProcessId)"
    if (-not $current) { return }
    if ($current.CreationDate -ne $Snapshot.CreationDate -or -not (Same-Path $current.ExecutablePath $Snapshot.ExecutablePath)) { throw 'PID identity changed; refusing termination' }
    Stop-Process -Id ([int]$current.ProcessId) -Force -ErrorAction Stop
}
$oldNoReuse=$env:MSBUILDDISABLENODEREUSE
$oldNoBytecode=$env:PYTHONDONTWRITEBYTECODE
try {
    $env:MSBUILDDISABLENODEREUSE='1';$env:PYTHONDONTWRITEBYTECODE='1'
    Stage '01-stop-owned-stale-trial' {
        $all=@(Get-CimInstance Win32_Process)
        $runners=@($all | Where-Object { Is-ExactRunner $_ })
        if ($runners.Count -gt 1) { throw 'Multiple exact runners exist; no process was stopped' }
        $stopped=@()
        if ($runners.Count -eq 1) {
            $runner=$runners[0];$created=$runner.CreationDate.ToUniversalTime()
            if ($created -lt [DateTime]::UtcNow.AddHours(-6) -or $created -gt [DateTime]::UtcNow.AddSeconds(5)) { throw 'Runner creation time is not fresh' }
            $allowed=@($NativeTargets | ForEach-Object { Join-Path $ProjectPath "build\$($_)_artefacts\Release\$_.exe" })
            $allowed += @((Join-Path $ProjectPath 'build\validator\bin\Release\validator.exe'),(Join-Path $ProjectPath 'tools\pluginval\pluginval.exe'),'C:\Windows\System32\conhost.exe')
            $tree=@($runner);$known=@{([int]$runner.ProcessId)=$true}
            do { $added=$false;foreach ($candidate in $all) { if (-not $known.ContainsKey([int]$candidate.ProcessId) -and $known.ContainsKey([int]$candidate.ParentProcessId)) { $tree += $candidate;$known[[int]$candidate.ProcessId]=$true;$added=$true } } } while ($added)
            foreach ($child in @($tree | Where-Object ProcessId -ne $runner.ProcessId)) {
                if (-not $child.ExecutablePath -or -not ($allowed | Where-Object { Same-Path $_ $child.ExecutablePath }) -or $child.Name -match '(?i)Ableton|Live' -or $child.CreationDate.ToUniversalTime() -lt $created) { throw "Unknown/unfresh descendant; no termination: PID=$($child.ProcessId)" }
                [void](Assert-LocalPath $child.ExecutablePath)
            }
            # Stop the verified parent first so it cannot advance to another suite.
            Stop-VerifiedProcess $runner;$stopped += [int]$runner.ProcessId
            foreach ($child in @($tree | Where-Object ProcessId -ne $runner.ProcessId)) { Stop-VerifiedProcess $child;$stopped += [int]$child.ProcessId }
            # Fail closed if a child escaped the captured cohort. Never broaden stop.
            $escaped=@(Get-CimInstance Win32_Process | Where-Object { $known.ContainsKey([int]$_.ParentProcessId) -and -not $known.ContainsKey([int]$_.ProcessId) -and $_.CreationDate.ToUniversalTime() -ge $created })
            if ($escaped.Count) { throw 'A new descendant appeared during stop; inspect privately before any retry' }
        }
        $archive=Join-Path $AttemptPath 'stale-trial-before-rebuild';New-Item -ItemType Directory -Path $archive | Out-Null
        foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $ProjectPath 'build') -File | Where-Object { $_.Name -match 'v0\.3\.1\.(log|json)$' })) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $archive $file.Name) }
        return [ordered]@{exactRunnerCount=$runners.Count;stoppedPids=$stopped;staleTrialArchived=$archive;reason='Previous e047 trial used stale Windows PE resource; no final certification retained'}
    }
    Stage '02-rebuild-18-targets-and-version-guard' {
        $arguments=@('--build',(Join-Path $ProjectPath 'build'),'--config','Release','--target')+$BuildTargets+@('--parallel','2','--','/nr:false')
        $result=Invoke-OwnedCommand '02-build' $CMakePath $arguments 3600
        $version=(Get-Item -LiteralPath $BinaryPath).VersionInfo.ProductVersion
        if (-not $version -or $version.Trim() -cne '0.3.1') { throw "PE ProductVersion is not0.3.1: $version" }
        $metadata=Join-Path $BundlePath 'Contents\Resources\moduleinfo.json'
        $versions=@([regex]::Matches([IO.File]::ReadAllText($metadata),'"Version"\s*:\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
        if (-not $versions.Count -or @($versions | Where-Object { $_ -cne '0.3.1' }).Count) { throw 'Module metadata version is not0.3.1' }
        $result['binarySHA256']=Hash $BinaryPath;$result['peProductVersion']=$version.Trim();return $result
    }
    Stage '03-full-19-gate-verification' {
        $result=Invoke-OwnedCommand '03-full-verifier' $PythonPath @($RunnerPath,'--root',$ProjectPath,'--timeout','1200') 10800
        $proof=Get-Content -LiteralPath (Join-Path $ProjectPath 'build\non-daw-v0.3.1.json') -Raw | ConvertFrom-Json
        if ($proof.complete -ne $true -or $proof.version -cne '0.3.1' -or $proof.releaseBinarySHA256 -cne (Hash $BinaryPath) -or @($proof.checks.PSObject.Properties).Count -ne 19 -or @($proof.checks.PSObject.Properties | Where-Object { $_.Value.passed -ne $true }).Count) { throw 'The complete current19-gate manifest is not valid' }
        $expectedCounts=[ordered]@{legacy=76;dsp=51;quality=16;audio=35;ui=43;ai=76;bridge=179;parity=15;host=11;lifecycle=41;vst3_lifecycle=82;state_commit=7;assist_exception=6;waveform_capture=108;scope_freeze=18;scope_edge=21;waveform_ui=109}
        if (@(Compare-Object -CaseSensitive -ReferenceObject (@($expectedCounts.Keys)+@('steinberg','pluginval')) -DifferenceObject @($proof.checks.PSObject.Properties.Name)).Count) { throw 'Exact nineteen mandatory gate names do not match' }
        foreach ($name in $expectedCounts.Keys) {
            if ($null -eq $expectedCounts[$name] -or -not $proof.checks.PSObject.Properties[$name] -or $proof.checks.$name.count -ne $expectedCounts[$name] -or $proof.checks.$name.exitCode -ne 0) { throw "Native strict count/exit mismatch: $name" }
        }
        if ($proof.checks.steinberg.count -ne 47 -or $proof.checks.steinberg.exitCode -ne 0 -or $null -ne $proof.checks.pluginval.count -or $proof.checks.pluginval.exitCode -ne 0) { throw 'Steinberg/pluginval summary count/exit mismatch' }
        if ($proof.pairedAudioEquivalence.passed -ne $true) { throw 'Actual paired audio equivalence is missing' };return $result
    }
    Stage '04-isolated-installer-qa' { Invoke-OwnedCommand '04-installer-qa' $PowerShellPath @('-NoProfile','-NonInteractive','-File',(Join-Path $ProjectPath 'tools\verify_installer_v031.ps1'),'-InnoCompiler',$InnoPath) 1800 }
    Stage '05-compile-official-installer' { Invoke-OwnedCommand '05-compile-installer' $PowerShellPath @('-NoProfile','-NonInteractive','-File',(Join-Path $ProjectPath 'tools\compile_installer_v031.ps1'),'-InnoCompiler',$InnoPath) 900 }
    Stage '06-bind-installer-release-evidence' {
        $output=Join-Path $AttemptPath 'installer-release-gate.json'
        $code='import json,sys;from pathlib import Path;r=Path(sys.argv[1]);sys.path.insert(0,str(r/"tools"));import installer_release_evidence_v031 as g;result=g.validate(r);Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8");print(json.dumps(result))'
        $result=Invoke-OwnedCommand '06-installer-gate' $PythonPath @('-c',$code,$ProjectPath,$output) 120
        $gate=Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        if ($gate.passed -ne $true -or $gate.productionInstallPerformed -ne $false) { throw 'Installer release binding failed' };$result['proof']=$output;return $result
    }
    $Report.complete=$true
    $Report['nextStage']='Production installation remains a separate explicitly authorized stage; refuse any open Live, preserve personal preset hashes, use official Setup only and never request UAC UI.'
}
catch { $Report['error']=$_.Exception.Message;throw }
finally {
    $Report['finishedUTC']=[DateTime]::UtcNow.ToString('O');Save-Report
    $env:MSBUILDDISABLENODEREUSE=$oldNoReuse;$env:PYTHONDONTWRITEBYTECODE=$oldNoBytecode
    Write-Output "Private orchestration report: $ReportPath"
}
