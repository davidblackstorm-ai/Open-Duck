[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskSource=Join-Path $taskRoot 'installer\InstallerPreflight.cpp'
$taskBuild=Join-Path $taskRoot 'build\installer-preflight'
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
$taskVs=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
if(!(Test-Path -LiteralPath $taskVs -PathType Leaf)){throw 'The installed MSVC x64 build environment is required for the check-only installer helper'}
$taskExe=Join-Path $taskBuild 'OpenDuck-Installer-Preflight.exe'
$taskObj=Join-Path $taskBuild 'InstallerPreflight.obj'
$taskCommand='@call "'+$taskVs+'" -arch=x64 -host_arch=x64 >nul'+[Environment]::NewLine+
    'cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 '+
    '/Fo"'+$taskObj+'" /Fe"'+$taskExe+'" "'+$taskSource+'" /link /INCREMENTAL:NO /Brepro Rstrtmgr.lib'
$taskCmdPath=Join-Path $taskBuild 'build-preflight.cmd'
[IO.File]::WriteAllText($taskCmdPath,$taskCommand+[Environment]::NewLine,[Text.Encoding]::ASCII)
& $env:ComSpec /d /c ('call "'+$taskCmdPath+'"')
if($LASTEXITCODE -ne 0){throw 'Check-only installer helper compilation failed'}
$taskRecord=[ordered]@{schema='openduck-installer-preflight-build-v1';architecture='x64';
    source='installer/InstallerPreflight.cpp';sourceSHA256=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash;
    executable='build/installer-preflight/OpenDuck-Installer-Preflight.exe';
    executableSHA256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash;
    buildScriptSHA256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;
    checkOnly=$true;hostShutdownApiUsed=$false;createdUTC=[DateTime]::UtcNow.ToString('O')}
$taskRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskBuild 'build-receipt.json') -Encoding utf8
$taskRecord | ConvertTo-Json -Depth 8
