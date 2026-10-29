param(
    [Parameter(Mandatory = $true)][string]$InnoCompiler,
    [string]$PreviousQAInstaller = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'installer_evidence_v031.ps1')
$verificationTools = [ordered]@{'tools/verify_installer_v031.ps1'=(Get-InstallerFileHash $PSCommandPath);
    'tools/installer_evidence_v031.ps1'=(Get-InstallerFileHash (Join-Path $PSScriptRoot 'installer_evidence_v031.ps1'))}
$version = '0.3.1'
$root = [System.IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
# The authentic 0.3.0 QA setup was compiled for the original v0.2.0 QA target.
# Preserve its immutable guard and reuse ONLY that isolated destination.
$qaRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'work\installer-qa-v0.2.0'))
$target = Join-Path $qaRoot 'OpenDuck Pro X.vst3'
$presetDir = Join-Path $qaRoot 'Presets'
$qaBuild = Join-Path $root "work\installer-qa-build-v$version"
$setup = Join-Path $qaBuild 'OpenDuck-Installer-QA.exe'
$logRoot = Join-Path $root 'build'
$qaRegistry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6C36316E-2BA3-4530-BA65-4FDC729F08CA}_is1'
$productionRegistry = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A2C52811-2937-42FA-A186-0100469D28E5}_is1'

function Assert-QaPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -ne $qaRoot -and -not $full.StartsWith($qaRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "QA path escaped its project directory: $full"
    }
    $ancestor = $full
    while ($ancestor.Length -ge $qaRoot.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "QA path contains a reparse point: $ancestor"
            }
        }
        $parent = Split-Path $ancestor -Parent
        if (-not $parent -or $parent -eq $ancestor) { break }
        $ancestor = $parent
    }
    return $full
}
foreach ($path in @($qaRoot, $target, $presetDir)) { [void](Assert-QaPath $path) }
foreach ($path in @($root,$qaRoot,$target,$presetDir,$qaBuild,$logRoot)) { [void](Assert-InstallerPath $path) }
# A failed rerun must never leave an older PASS eligible for the current inputs.
$attempt = Archive-InstallerAttempt $root 'qa' @('build/installer-qa-results-v0.3.1.json',
    'build/qa-v0.3.1-install-legacy.log','build/qa-v0.3.1-upgrade.log',
    'build/qa-v0.3.1-reinstall.log','build/qa-v0.3.1-uninstall.log',
    'work/installer-qa-build-v0.3.1/OpenDuck-Installer-QA.exe')
$resultPath = Join-Path $logRoot 'installer-qa-results-v0.3.1.json'
if (-not [Environment]::Is64BitProcess) { throw 'Installer QA requires 64-bit PowerShell so the production uninstall metadata is observed in its actual registry view' }
function File-HashOrNull([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash }
    return $null
}

# The QA AppId is deliberately different from the production AppId. Refuse to
# reuse another QA location; this script never removes an existing installation
# outside the exact versioned project directory above.
if (Test-Path -LiteralPath $qaRegistry) {
    $existing = (Get-ItemProperty -LiteralPath $qaRegistry).InstallLocation
    if ([System.IO.Path]::GetFullPath($existing).TrimEnd('\') -ne $target.TrimEnd('\')) {
        throw "A different QA installation is registered at '$existing'; refusing to modify it."
    }
}
$commonFiles = [Environment]::GetEnvironmentVariable('CommonProgramW6432')
if (-not $commonFiles) { $commonFiles = [Environment]::GetFolderPath('CommonProgramFiles') }
$productionBinary = Join-Path $commonFiles 'VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3'
$productionBundle = Join-Path $commonFiles 'VST3\OpenDuck Pro X.vst3'
$programFiles64 = [Environment]::GetEnvironmentVariable('ProgramW6432')
if (-not $programFiles64) { $programFiles64 = [Environment]::GetFolderPath('ProgramFiles') }
$productionAuxiliary = Join-Path $programFiles64 'OpenDuck Pro X'
function Production-Snapshot {
    return [ordered]@{bundle=(Get-InstallerTreeSnapshot $productionBundle);
        auxiliary=(Get-InstallerTreeSnapshot $productionAuxiliary);
        uninstallRegistry=(Get-InstallerRegistrySnapshot $productionRegistry)}
}
$productionBefore = Production-Snapshot
function Assert-ProductionUnchanged {
    Assert-InstallerTreeEqual $productionBefore (Production-Snapshot) 'Production bundle/auxiliary files/uninstall metadata'
}

if (-not $PreviousQAInstaller) {
    $PreviousQAInstaller = Join-Path $root 'work\installer-qa-build-v0.3.0\OpenDuck-Installer-QA.exe'
}
if (-not (Test-Path -LiteralPath $PreviousQAInstaller -PathType Leaf)) {
    throw "The archived 0.3.0 QA installer is required for a genuine upgrade test: $PreviousQAInstaller"
}
$previousVersion = (Get-Item -LiteralPath $PreviousQAInstaller).VersionInfo.ProductVersion
if ($previousVersion.Trim() -ne '0.3.0') {
    throw "Expected archived 0.3.0 QA installer, got '$previousVersion'."
}
if ((File-HashOrNull $PreviousQAInstaller) -ne '1660ADC9E23FEA2FF044A2695B6DE8BC5093DCDCF20F4CEDB02D8930773AECA5') {
    throw 'The previous QA installer differs from the preserved 0.3.0 release evidence.'
}
if (-not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw "Inno compiler not found: $InnoCompiler" }
$InnoCompiler = Assert-InstallerPath $InnoCompiler
$compilerSHA256 = Get-InstallerFileHash $InnoCompiler
$builtVersion = (Get-Item -LiteralPath (Join-Path $root 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3')).VersionInfo.ProductVersion
if (-not $builtVersion -or $builtVersion.Trim() -ne $version) { throw 'QA requires the final 0.3.1 VST3 ProductVersion' }
$inputManifest = Get-InstallerInputManifest $root
$compilerArguments = @('/Qp','/DProductVersion=0.3.1','/DQA','/DQAUpgradeFrom030','/FOpenDuck-Installer-QA',"/O$qaBuild","$root\installer\OpenDuck.iss")
& $InnoCompiler @compilerArguments
if ($LASTEXITCODE -ne 0) { throw 'QA installer compilation failed' }
Assert-InstallerInputsEqual $inputManifest (Get-InstallerInputManifest $root)
$qaSetupVersion = (Get-Item -LiteralPath $setup).VersionInfo.ProductVersion
if (-not $qaSetupVersion -or $qaSetupVersion.Trim() -ne $version) { throw 'Compiled QA Setup has the wrong ProductVersion' }
$qaSetupSHA256 = Get-InstallerFileHash $setup

New-Item -ItemType Directory -Path $presetDir -Force | Out-Null
$existingPresets = @(Get-ChildItem -LiteralPath $presetDir -File -Recurse -Force | ForEach-Object {
    $safe = Assert-QaPath $_.FullName
    [pscustomobject]@{Path=$safe; SHA256=(File-HashOrNull $safe)}
})
$suffix = [guid]::NewGuid().ToString()
$legacyPreset = Join-Path $presetDir "Legacy-InstallerQA-$suffix.odpx"
$modernPreset = Join-Path $presetDir "Modern-InstallerQA-$suffix.odpx"
$legacyState = Join-Path $root 'tests\fixtures\legacy-v0.1.0-init.odpx'
$modernState = Join-Path $root 'build\visual-qa-v0.3.1\QA-StateRoundtrip.odpx'
foreach ($path in @($legacyState, $modernState)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "State fixture is missing: $path" }
}
Copy-Item -LiteralPath $legacyState -Destination (Assert-QaPath $legacyPreset)
Copy-Item -LiteralPath $modernState -Destination (Assert-QaPath $modernPreset)
$legacyHash = File-HashOrNull $legacyPreset
$modernHash = File-HashOrNull $modernPreset

function Assert-PresetsPreserved {
    if ((File-HashOrNull $legacyPreset) -ne $legacyHash -or (File-HashOrNull $modernPreset) -ne $modernHash) {
        throw 'Installer modified or deleted a personal state/preset fixture'
    }
    foreach ($preset in $existingPresets) {
        if ((File-HashOrNull (Assert-QaPath $preset.Path)) -ne $preset.SHA256) {
            throw "Installer changed a previously existing QA preset: $($preset.Path)"
        }
    }
}
function Run-Setup([string]$Executable, [string]$LogName) {
    [void](Assert-QaPath $target)
    Assert-InstallerInputsEqual $inputManifest (Get-InstallerInputManifest $root)
    if ((Get-InstallerFileHash $setup) -cne $qaSetupSHA256) { throw 'Compiled QA setup changed during verification' }
    $log = Join-Path $logRoot $LogName
    $arguments = '/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOAUDIO=1 /DIR="' + $target + '" /LOG="' + $log + '"'
    $job = Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($job.ExitCode -ne 0) { throw "QA setup returned exit $($job.ExitCode): $Executable" }
    if (-not (Test-Path -LiteralPath $qaRegistry)) { throw 'QA uninstall registration is missing' }
    $registered = (Get-ItemProperty -LiteralPath $qaRegistry).InstallLocation
    if ([System.IO.Path]::GetFullPath($registered).TrimEnd('\') -ne $target.TrimEnd('\')) {
        throw "QA installer registered an unexpected target: $registered"
    }
    Assert-PresetsPreserved
    Assert-ProductionUnchanged
}

# Exercise a real previous-version upgrade, not merely two runs of the same EXE.
Run-Setup $PreviousQAInstaller 'qa-v0.3.1-install-previous.log'
$binary = Join-Path $target 'Contents\x86_64-win\OpenDuck Pro X.vst3'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Archived QA installer did not install its binary' }
$installedLegacyHash = File-HashOrNull $binary
if ($installedLegacyHash -ne 'A22BE99E689D64304FCC525C9C4EEAE9B793022836BCACA96CF125CCC636F271') {
    throw 'The previous setup did not install the authentic preserved 0.3.0 plugin binary.'
}
Run-Setup $setup 'qa-v0.3.1-upgrade.log'
$built = Join-Path $root 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3'
$builtHash = File-HashOrNull $built
if (-not $builtHash -or (File-HashOrNull $binary) -ne $builtHash) { throw 'Installed QA binary does not match the Release build' }
$builtContents = Split-Path (Split-Path $built -Parent) -Parent
$expectedPayload = Get-InstallerPayloadManifest $builtContents
$upgradePayload = Assert-InstallerPayloadMatches $expectedPayload (Join-Path $target 'Contents') 'Upgrade'
$payloadPaths = @($expectedPayload.files | ForEach-Object { $_.relative })
function Assert-DocumentsMatch {
    foreach ($document in @('LICENSE.txt','RELEASE-NOTES.txt')) {
        if ((Get-InstallerFileHash (Join-Path $root ('installer\'+$document))) -cne
            (Get-InstallerFileHash (Join-Path $target ('Documentation\'+$document)))) { throw "Installed document mismatch: $document" }
    }
}
Assert-DocumentsMatch
if ($installedLegacyHash -eq $builtHash) { throw 'Upgrade reference is not a different previous-version binary' }
$registeredVersion = (Get-ItemProperty -LiteralPath $qaRegistry).DisplayVersion
if ($registeredVersion -ne $version) { throw "QA registration has the wrong version: $registeredVersion" }
Run-Setup $setup 'qa-v0.3.1-reinstall.log'
if ((File-HashOrNull $binary) -ne $builtHash) { throw 'QA reinstall changed the binary' }
$reinstallPayload = Assert-InstallerPayloadMatches $expectedPayload (Join-Path $target 'Contents') 'Reinstall'
Assert-DocumentsMatch

$uninstall = Assert-QaPath (Join-Path $target 'Uninstall\unins000.exe')
if (-not (Test-Path -LiteralPath $uninstall -PathType Leaf)) { throw 'QA uninstaller is missing' }
$uninstallArguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + (Join-Path $logRoot 'qa-v0.3.1-uninstall.log') + '"'
$job = Start-Process -FilePath $uninstall -ArgumentList $uninstallArguments -WindowStyle Hidden -Wait -PassThru
if ($job.ExitCode -ne 0) { throw "QA uninstall returned exit $($job.ExitCode)" }
if (Test-Path -LiteralPath $binary) { throw 'QA uninstall left its binary behind' }
foreach ($relative in $payloadPaths) {
    if (Test-Path -LiteralPath (Join-Path (Join-Path $target 'Contents') $relative)) {
        throw "QA uninstall left a managed payload file behind: $relative"
    }
}
foreach ($document in @('LICENSE.txt', 'RELEASE-NOTES.txt')) {
    if (Test-Path -LiteralPath (Join-Path (Join-Path $target 'Documentation') $document)) {
        throw "QA uninstall left a managed document behind: $document"
    }
}
if (Test-Path -LiteralPath $qaRegistry) { throw 'QA uninstall left its registration behind' }
Assert-PresetsPreserved
Assert-ProductionUnchanged
$productionAfter = Production-Snapshot
Assert-InstallerInputsEqual $inputManifest (Get-InstallerInputManifest $root)
if ((Get-InstallerFileHash $setup) -cne $qaSetupSHA256 -or
    (Get-InstallerFileHash $InnoCompiler) -cne $compilerSHA256) { throw 'QA setup or compiler changed during verification' }
if ((File-HashOrNull $PreviousQAInstaller) -ne '1660ADC9E23FEA2FF044A2695B6DE8BC5093DCDCF20F4CEDB02D8930773AECA5') { throw 'Previous setup changed during verification' }
foreach ($relative in $verificationTools.Keys) {
    if ((Get-InstallerFileHash (Join-Path $root $relative)) -cne $verificationTools[$relative]) { throw 'Installer verification tool changed during QA' }
}

[ordered]@{
    evidenceSchema = 'openduck-installer-qa-v2'
    version = $version
    attemptId = $attempt.id
    previousVersion = $previousVersion.Trim()
    installPrevious = $true
    upgradeFrom030 = $true
    reinstall = $true
    uninstall = $true
    legacyPresetPreserved = $true
    modernPresetPreserved = $true
    binaryMatches = $true
    allPayloadFilesMatch = $true
    reinstallPayloadMatches = $true
    managedDocumentsMatch = $true
    allManagedPayloadRemoved = $true
    payloadFileCount = $payloadPaths.Count
    productionUntouched = $true
    productionSnapshotSchema = 1
    productionBefore = $productionBefore
    productionAfter = $productionAfter
    productionBeforeSHA256 = (Get-InstallerValueHash $productionBefore)
    productionAfterSHA256 = (Get-InstallerValueHash $productionAfter)
    payloadManifest = $expectedPayload
    upgradePayloadSHA256 = (Get-InstallerValueHash $upgradePayload)
    reinstallPayloadSHA256 = (Get-InstallerValueHash $reinstallPayload)
    inputManifest = $inputManifest
    inputSHA256 = $inputManifest.inputSHA256
    compiler = [ordered]@{path=$InnoCompiler; sha256=$compilerSHA256; arguments=$compilerArguments}
    verificationToolsSHA256 = $verificationTools
    target = $target
    presetDirectory = $presetDir
    builtProductVersion = $builtVersion.Trim()
    builtSHA256 = $builtHash
    legacyInstalledSHA256 = $installedLegacyHash
    previousInstalledSHA256 = $installedLegacyHash
    previousSetupSHA256 = (File-HashOrNull $PreviousQAInstaller)
    qaSetupRelative = 'work/installer-qa-build-v0.3.1/OpenDuck-Installer-QA.exe'
    qaSetupProductVersion = $qaSetupVersion.Trim()
    setupSHA256 = $qaSetupSHA256
    timestamp = (Get-Date).ToString('o')
} | ConvertTo-Json -Depth 32 | Set-Content -Encoding UTF8 -LiteralPath $resultPath
Write-Output 'PASS isolated 0.3.0 install -> 0.3.1 upgrade -> reinstall -> uninstall; both preset schemas preserved; production untouched'
