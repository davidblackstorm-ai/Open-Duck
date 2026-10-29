[CmdletBinding()]
param([string]$CMakePath='', [switch]$WithStress)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(!$CMakePath){
    $cmakeCommand=Get-Command cmake.exe -ErrorAction SilentlyContinue
    if($cmakeCommand){$CMakePath=$cmakeCommand.Source}
    else {$CMakePath=Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
}
if(!(Test-Path -LiteralPath $CMakePath)){throw 'Specify -CMakePath with the installed CMake executable.'}
$buildRoot=Join-Path $projectRoot 'build'
$evidenceRoot=Join-Path $buildRoot 'verification-v0.4.0'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
& $CMakePath -S $projectRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64
if($LASTEXITCODE -ne 0){throw 'CMake configuration failed'}
& $CMakePath --build $buildRoot --config Release --target OpenDuckProX_VST3 Revision040Tests Revision040RegressionTests Revision040Vst3LifecycleTests OpenDuckHostContract Revision031WaveformCaptureTests -j 4
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
$bundle=Join-Path $buildRoot 'OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3'
$results=[Collections.Generic.List[object]]::new()
function Invoke-RevisionCheck([string]$Name,[string]$Executable,[string[]]$Arguments){
    $log=Join-Path $evidenceRoot ($Name+'.log')
    & $Executable @Arguments *> $log
    $code=$LASTEXITCODE
    if($code -ne 0){Get-Content -LiteralPath $log -Tail 25;throw "$Name failed (exit $code)"}
    $body=Get-Content -LiteralPath $log -Raw
    if($body -match '(?m)^FAIL ' -or $body -notmatch '(?m)(?:RESULT|Result:).*?([1-9][0-9]*) passed, 0 failed'){
        throw "$Name did not emit a successful positive result record: $log"
    }
    $results.Add([ordered]@{name=$Name;exitCode=$code;log=$log;sha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash})
    Write-Output "PASS $Name"
}
Push-Location $projectRoot
try {
    Invoke-RevisionCheck 'core' (Join-Path $buildRoot 'Revision040Tests_artefacts\Release\Revision040Tests.exe') @((Join-Path $evidenceRoot 'Preview'))
    $regression=Join-Path $buildRoot 'Revision040RegressionTests_artefacts\Release\Revision040RegressionTests.exe'
    Invoke-RevisionCheck 'ui' $regression @('ui')
    Invoke-RevisionCheck 'ai5' $regression @('ai')
    Invoke-RevisionCheck 'ai10' $regression @('ai10')
    if($WithStress){Invoke-RevisionCheck 'stress-4x120s' $regression @('stress')}
    Invoke-RevisionCheck 'actual-vst3-lifecycle-and-automation' (Join-Path $buildRoot 'Revision040Vst3LifecycleTests_artefacts\Release\Revision040Vst3LifecycleTests.exe') @($bundle,(Join-Path $projectRoot 'tests\fixtures'),'0.4.0')
    Invoke-RevisionCheck 'actual-vst3-host' (Join-Path $buildRoot 'OpenDuckHostContract_artefacts\Release\OpenDuckHostContract.exe') @($bundle)
    Invoke-RevisionCheck 'real-waveform-capture' (Join-Path $buildRoot 'Revision031WaveformCaptureTests_artefacts\Release\Revision031WaveformCaptureTests.exe') @()
} finally {Pop-Location}
$binary=Join-Path $bundle 'Contents\x86_64-win\OpenDuck Pro X.vst3'
[ordered]@{version='0.4.0';builtSHA256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash;checks=@($results);stressIncluded=[bool]$WithStress;installed=$false;abletonLaunched=$false} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'results.json') -Encoding utf8
Write-Output 'PASS requested native 0.4.0 checks. This script does not install or launch Ableton.'
