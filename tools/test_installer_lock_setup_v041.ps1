[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InnoCompiler)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'installer_evidence_v041.ps1')
$taskRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskProof=Join-Path $taskRoot 'work\revision041\InstallerSetupLock-R17'
$taskQaRoot=Join-Path $taskRoot 'work\installer-qa-v0.4.1'
$taskTarget=Join-Path $taskQaRoot 'OpenDuck Pro X.vst3'
foreach($taskPath in @($taskProof,$taskQaRoot,$taskTarget)){[void](Assert-InstallerPath $taskPath $taskRoot)}
if((Test-Path -LiteralPath $taskTarget) -and (Get-InstallerTreeSnapshot $taskTarget).files.Count -ne 0){throw 'The focused lock test requires an empty, isolated QA target; existing files are preserved'}
$taskQaRegistry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6C36316E-2BA3-4530-BA65-4FDC729F08CA}_is1'
if(Test-Path -LiteralPath $taskQaRegistry){throw 'Another QA installation is registered; refusing to alter it'}
New-Item -ItemType Directory -Path $taskProof -Force | Out-Null
if(Test-Path -LiteralPath (Join-Path $taskProof 'results.json')){Copy-Item -LiteralPath (Join-Path $taskProof 'results.json') -Destination (Join-Path $taskProof ('results-history-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.json'))}
$taskInputs=Get-InstallerInputManifest $taskRoot
& $InnoCompiler '/Qp' '/DQA' '/DProductVersion=0.4.1' '/FOpenDuck-R17-Lock-QA' (('/O')+$taskProof) (Join-Path $taskRoot 'installer\OpenDuck.iss') *> (Join-Path $taskProof 'compile.log')
if($LASTEXITCODE -ne 0){throw 'Focused QA setup compilation failed'}
$taskSetup=Join-Path $taskProof 'OpenDuck-R17-Lock-QA.exe'
Assert-InstallerInputsEqual $taskInputs (Get-InstallerInputManifest $taskRoot)
$taskContents=Join-Path $taskTarget 'Contents'
$taskDll=Join-Path $taskContents 'x86_64-win\OpenDuck Pro X.vst3'
$taskInfo=Join-Path $taskContents 'Resources\moduleinfo.json'
New-Item -ItemType Directory -Path (Split-Path $taskDll -Parent),(Split-Path $taskInfo -Parent) -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3') -Destination $taskDll
[IO.File]::WriteAllText($taskInfo,'{"sentinel":"must remain unchanged while the DLL is mapped"}')
$taskBefore=Get-InstallerTreeSnapshot $taskTarget
$taskProduction='C:\Program Files\Common Files\VST3\OpenDuck Pro X.vst3'
$taskProductionBefore=Get-InstallerTreeSnapshot $taskProduction
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OpenDuckR17SetupLock {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern IntPtr LoadLibraryExW(string path,IntPtr file,uint flags);
 [DllImport("kernel32.dll",SetLastError=true)]
 [return:MarshalAs(UnmanagedType.Bool)] public static extern bool FreeLibrary(IntPtr module);
}
'@
$taskModule=[OpenDuckR17SetupLock]::LoadLibraryExW($taskDll,[IntPtr]::Zero,1)
if($taskModule -eq [IntPtr]::Zero){throw 'Could not map the isolated QA image'}
function Invoke-FocusedSetup([string]$LogName){
 $taskLog=Join-Path $taskProof $LogName
 $taskArguments='/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOAUDIO=1 /DIR="'+$taskTarget+'" /LOG="'+$taskLog+'"'
 $taskJob=Start-Process -FilePath $taskSetup -ArgumentList $taskArguments -WindowStyle Hidden -Wait -PassThru
 return $taskJob.ExitCode
}
try {
 $taskBlockedExit=Invoke-FocusedSetup 'locked.log'
 if($taskBlockedExit -ne 7){throw "Expected preparing-stage exit7, got $taskBlockedExit"}
 Assert-InstallerTreeEqual $taskBefore (Get-InstallerTreeSnapshot $taskTarget) 'Locked target including moduleinfo sentinel'
 if(Test-Path -LiteralPath $taskQaRegistry){throw 'Blocked setup registered an uninstall entry'}
 $taskLog=Get-Content -LiteralPath (Join-Path $taskProof 'locked.log') -Raw
 if($taskLog -notmatch 'ODX_PREFLIGHT_HELPER_EXIT=32' -or $taskLog -notmatch ('PROCESS_PID='+$PID+'\b')){throw 'Blocked setup log lacks lock code and actual holder PID'}
} finally {
 if(![OpenDuckR17SetupLock]::FreeLibrary($taskModule)){throw 'QA mapped-image release failed'}
}
$taskClearExit=Invoke-FocusedSetup 'clear.log'
if($taskClearExit -ne 0){throw "Unlocked focused QA setup failed: $taskClearExit"}
$taskBuiltContents=Join-Path $taskRoot 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents'
$taskPayload=Get-InstallerPayloadManifest $taskBuiltContents
[void](Assert-InstallerPayloadMatches $taskPayload $taskContents 'Unlocked focused QA')
$taskUninstall=Join-Path $taskTarget 'Uninstall\unins000.exe'
$taskJob=Start-Process -FilePath $taskUninstall -ArgumentList ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+(Join-Path $taskProof 'uninstall.log')+'"') -WindowStyle Hidden -Wait -PassThru
if($taskJob.ExitCode -ne 0 -or (Test-Path -LiteralPath $taskDll) -or (Test-Path -LiteralPath $taskInfo) -or (Test-Path -LiteralPath $taskQaRegistry)){throw 'Focused QA uninstall failed or left managed state'}
$taskProductionAfter=Get-InstallerTreeSnapshot $taskProduction
Assert-InstallerTreeEqual $taskProductionBefore $taskProductionAfter 'Production bundle'
Assert-InstallerInputsEqual $taskInputs (Get-InstallerInputManifest $taskRoot)
$taskReceipt=[ordered]@{schema='openduck-focused-setup-lock-test-R17-v1';passed=8;failed=0;
 blockedSetupExitCode=$taskBlockedExit;unlockedSetupExitCode=$taskClearExit;uninstallExitCode=$taskJob.ExitCode;
 lockedTargetUnchanged=$true;moduleInfoSentinelPreserved=$true;blockedUninstallRegistrationAbsent=$true;
 holderPID=$PID;holderSurvived=$true;payloadMatches=$true;productionUntouched=$true;
 productionBeforeSHA256=(Get-InstallerValueHash $taskProductionBefore);productionAfterSHA256=(Get-InstallerValueHash $taskProductionAfter);
 setupSHA256=(Get-InstallerFileHash $taskSetup);inputSHA256=$taskInputs.inputSHA256;
 helperSHA256=(Get-InstallerFileHash (Join-Path $taskRoot 'build\installer-preflight\OpenDuck-Installer-Preflight.exe'));
 productionSetupExecuted=$false;dawControlled=$false;createdUTC=[DateTime]::UtcNow.ToString('O')}
$taskReceipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskProof 'results.json') -Encoding utf8
$taskReceipt | ConvertTo-Json -Depth 8
Write-Output 'RESULT 8 passed, 0 failed'
