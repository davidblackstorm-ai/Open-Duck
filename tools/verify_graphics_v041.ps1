[CmdletBinding()]
param([string]$CMakePath='', [switch]$SkipBuild, [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Join-Path $projectRoot 'build'
if(!$OutputDirectory){$OutputDirectory=Join-Path $buildRoot 'verification-v0.4.1\Graphics'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$reportPath=Join-Path $OutputDirectory 'graphics-verification.json'
$checks=[Collections.Generic.List[object]]::new()
$started=[DateTime]::UtcNow.ToString('O')
try {
    if(!$SkipBuild){
        if(!$CMakePath){
            $command=Get-Command cmake.exe -ErrorAction SilentlyContinue
            if($command){$CMakePath=$command.Source}
            else {$CMakePath=Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
        }
        if(!(Test-Path -LiteralPath $CMakePath)){throw 'CMake executable was not found.'}
        & $CMakePath -S $projectRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64
        if($LASTEXITCODE -ne 0){throw 'Graphics verification configuration failed.'}
        & $CMakePath --build $buildRoot --config Release --target Revision041RegressionTests Revision041CurveToolsTests -j 4
        if($LASTEXITCODE -ne 0){throw 'Graphics verification compilation failed.'}
    }
    $runs=@(
        @{name='ui';exe=(Join-Path $buildRoot 'Revision041RegressionTests_artefacts\Release\Revision041RegressionTests.exe');arguments=@('ui')},
        @{name='curve-tools';exe=(Join-Path $buildRoot 'Revision041CurveToolsTests_artefacts\Release\Revision041CurveToolsTests.exe');arguments=@($projectRoot,$OutputDirectory)}
    )
    Push-Location $projectRoot
    try {
        foreach($run in $runs){
            $log=Join-Path $OutputDirectory ($run.name+'.log')
            $arguments=$run.arguments
            & $run.exe @arguments *> $log
            $code=$LASTEXITCODE
            $body=Get-Content -LiteralPath $log -Raw
            $result=[regex]::Match($body,'(?m)^RESULT[^\r\n]*?(\d+) passed, 0 failed')
            if($code -ne 0 -or !$result.Success -or $body -match '(?m)^FAIL '){
                Get-Content -LiteralPath $log -Tail 35
                throw "Graphics invariant failed in $($run.name); inspect $log and repair the identified implementation or test defect before rerunning."
            }
            $checks.Add([ordered]@{name=$run.name;passed=[int]$result.Groups[1].Value;failed=0;log=$log;sha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash})
        }
    } finally {Pop-Location}
    [ordered]@{complete=$true;repairRequired=$false;startedUTC=$started;finishedUTC=[DateTime]::UtcNow.ToString('O');checks=@($checks)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Output 'PASS graphics invariants, frozen rotary byte/pixel baseline, native controls and audio behavior. No installation or Ableton changes.'
} catch {
    [ordered]@{complete=$false;repairRequired=$true;startedUTC=$started;finishedUTC=[DateTime]::UtcNow.ToString('O');error=$_.Exception.Message;checks=@($checks)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
}
