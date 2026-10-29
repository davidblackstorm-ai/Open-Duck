[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InnoCompiler)

# Compiles only. Never starts Setup, a DAW or an uninstaller.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'installer_evidence_v040.ps1')
$root = Assert-InstallerPath ([IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)))
$InnoCompiler = Assert-InstallerPath $InnoCompiler
$setupRelative = 'releases/OpenDuck-Pro-X-0.4.0-Windows-x64-Setup.exe'
$manifestRelative = 'build/installer-production-v0.4.0.json'
$setup = Assert-InstallerPath (Join-Path $root $setupRelative) $root
$manifestPath = Assert-InstallerPath (Join-Path $root $manifestRelative) $root
$started = [DateTime]::UtcNow.ToString('O')
$attempt = Archive-InstallerAttempt $root 'production' @($manifestRelative,$setupRelative)
$builtVersion = (Get-Item -LiteralPath (Join-Path $root 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3')).VersionInfo.ProductVersion
if (-not $builtVersion -or $builtVersion.Trim() -ne '0.4.0') { throw 'Production Setup requires the final 0.4.0 VST3 ProductVersion' }
$inputs = Get-InstallerInputManifest $root
$compilationTools = [ordered]@{'tools/compile_installer_v040.ps1'=(Get-InstallerFileHash $PSCommandPath);
    'tools/installer_evidence_v040.ps1'=(Get-InstallerFileHash (Join-Path $PSScriptRoot 'installer_evidence_v040.ps1'))}
$compilerHash = Get-InstallerFileHash $InnoCompiler
$arguments = @('/Qp','/DProductVersion=0.4.0',('/O'+(Join-Path $root 'releases')),'/FOpenDuck-Pro-X-0.4.0-Windows-x64-Setup',(Join-Path $root 'installer\OpenDuck.iss'))
& $InnoCompiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Production installer compilation failed; old setup/manifest are archived and no current PASS is retained' }
Assert-InstallerInputsEqual $inputs (Get-InstallerInputManifest $root)
if ((Get-InstallerFileHash $InnoCompiler) -cne $compilerHash) { throw 'Compiler changed during compilation' }
foreach ($relative in $compilationTools.Keys) {
    if ((Get-InstallerFileHash (Join-Path $root $relative)) -cne $compilationTools[$relative]) { throw 'Installer compiler helper changed during compilation' }
}
$item = Get-Item -LiteralPath $setup -Force -ErrorAction Stop
$version = $item.VersionInfo.ProductVersion.Trim()
if ($version -ne '0.4.0') { throw "Compiled setup has unexpected product version: $version" }
$result = [ordered]@{schema='openduck-installer-production-v1'; version='0.4.0'; complete=$true;
    builtProductVersion=$builtVersion.Trim(); compileOnly=$true; productionInstallPerformed=$false; attemptId=$attempt.id; startedUTC=$started;
    finishedUTC=[DateTime]::UtcNow.ToString('O'); inputManifest=$inputs; inputSHA256=$inputs.inputSHA256;
    compiler=[ordered]@{path=$InnoCompiler; sha256=$compilerHash; arguments=$arguments};
    compilationToolsSHA256=$compilationTools;
    setup=[ordered]@{relative=$setupRelative; sha256=(Get-InstallerFileHash $setup); length=$item.Length; productVersion=$version}}
$utf8 = [Text.UTF8Encoding]::new($false)
$stream = [IO.File]::Open($manifestPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $bytes=$utf8.GetBytes(($result | ConvertTo-Json -Depth 32)+[Environment]::NewLine); $stream.Write($bytes,0,$bytes.Length) }
finally { $stream.Dispose() }
$result | ConvertTo-Json -Depth 32
