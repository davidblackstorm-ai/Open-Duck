# Synthetic file/provider tests only. No installer, DAW, real registry or real
# production tree is accessed. Every mutable file is under this C: fixture.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'installer_evidence_v030.ps1')
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\qa-tools-proof'))
$proof = Join-Path $fixtureParent ('installer-evidence-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($proof)
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name) { if (-not $Condition) { throw $Name }; $checks.Add($Name) }
function Refuses([scriptblock]$Action,[string]$Name) {
    $rejected=$false
    try { & $Action | Out-Null } catch { $rejected=$true }
    Check $rejected $Name
}
function Put([string]$Path,[string]$Content) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path,$Content,[Text.UTF8Encoding]::new($false))
}
$project=Join-Path $proof 'project'
foreach($name in @('OpenDuck.iss','LICENSE.txt','RELEASE-NOTES.txt','WizardPanel.bmp','WizardSmall.bmp','OriginalTechno.wav')) {
    Put (Join-Path $project ('installer\'+$name)) ('SYNTHETIC INPUT '+$name)
}
$buildContents=Join-Path $project 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents'
$builtDll=Join-Path $buildContents 'x86_64-win\OpenDuck Pro X.vst3'
$builtResource=Join-Path $buildContents 'Resources\moduleinfo.json'
Put $builtDll 'DUMMY NONEXECUTABLE DLL BYTES'
Put $builtResource '{"fixtureOnly":true,"version":"0.3.0"}'

$inputs=Get-InstallerInputManifest $project
Check ($inputs.files.Count -eq 8) 'Input manifest fingerprints six actual compiler inputs plus both VST3 payload files'
Assert-InstallerInputsEqual $inputs (Get-InstallerInputManifest $project)
Check $true 'Unchanged input manifest is deterministic'
$notes=Join-Path $project 'installer\RELEASE-NOTES.txt'
$notesBefore=[IO.File]::ReadAllText($notes)
Put $notes 'CHANGED NOTES WITHOUT CHANGING DLL'
$changedInputs=Get-InstallerInputManifest $project
Refuses { Assert-InstallerInputsEqual $inputs $changedInputs } 'Changed notes invalidate an older QA/production input fingerprint even when DLL is unchanged'
Check ($inputs.inputSHA256 -cne $changedInputs.inputSHA256) 'Input SHA detects installer-only edits'
Put $notes $notesBefore
Assert-InstallerInputsEqual $inputs (Get-InstallerInputManifest $project)

$installed=Join-Path $project 'work\fake-qa\OpenDuck Pro X.vst3\Contents'
Put (Join-Path $installed 'x86_64-win\OpenDuck Pro X.vst3') ([IO.File]::ReadAllText($builtDll))
Put (Join-Path $installed 'Resources\moduleinfo.json') ([IO.File]::ReadAllText($builtResource))
$payload=Get-InstallerPayloadManifest $buildContents
$verified=Assert-InstallerPayloadMatches $payload $installed 'Reinstall'
Check ($verified.files.Count -eq 2) 'Full reinstall payload comparison accepts a matching DLL and resource'
Put (Join-Path $installed 'Resources\moduleinfo.json') 'CORRUPTED RESOURCE WITH IDENTICAL DLL'
Check ((Get-InstallerFileHash $builtDll) -ceq (Get-InstallerFileHash (Join-Path $installed 'x86_64-win\OpenDuck Pro X.vst3'))) 'Reinstall negative fixture leaves DLL unchanged'
Refuses { Assert-InstallerPayloadMatches $payload $installed 'Reinstall' } 'Reinstall rejects resource corruption previously missed by DLL-only check'
Put (Join-Path $installed 'Resources\moduleinfo.json') ([IO.File]::ReadAllText($builtResource))
Put (Join-Path $installed 'Resources\unexpected.json') 'EXTRA MANAGED PAYLOAD'
Refuses { Assert-InstallerPayloadMatches $payload $installed 'Upgrade' } 'Upgrade comparison rejects extra payload files'
Remove-Item -LiteralPath (Join-Path $installed 'Resources\unexpected.json')
[void][IO.Directory]::CreateDirectory((Join-Path $installed 'Unexpected-empty-directory'))
Refuses { Assert-InstallerPayloadMatches $payload $installed 'Reinstall' } 'Payload comparison detects changed empty directory sets'
Remove-Item -LiteralPath (Join-Path $installed 'Unexpected-empty-directory')
Remove-Item -LiteralPath (Join-Path $installed 'Resources\moduleinfo.json')
Refuses { Assert-InstallerPayloadMatches $payload $installed 'Reinstall' } 'Payload comparison rejects missing managed resources'
Put (Join-Path $installed 'Resources\moduleinfo.json') ([IO.File]::ReadAllText($builtResource))

$production=Join-Path $proof 'fake-production\OpenDuck Pro X.vst3'
Put (Join-Path $production 'Contents\x86_64-win\OpenDuck Pro X.vst3') 'IMMUTABLE FAKE PRODUCTION DLL'
$prodResource=Join-Path $production 'Contents\Resources\moduleinfo.json'
Put $prodResource 'IMMUTABLE FAKE PRODUCTION RESOURCE'
$before=Get-InstallerTreeSnapshot $production
Assert-InstallerTreeEqual $before (Get-InstallerTreeSnapshot $production) 'Fake production'
Check $true 'Unchanged complete fake production tree passes'
Put $prodResource 'ALTERED RESOURCE, DLL LEFT INTACT'
Refuses { Assert-InstallerTreeEqual $before (Get-InstallerTreeSnapshot $production) 'Fake production' } 'Production snapshot detects resource corruption without a DLL change'
Put $prodResource 'IMMUTABLE FAKE PRODUCTION RESOURCE'
Put (Join-Path $production 'new-file.txt') 'UNEXPECTED'
Refuses { Assert-InstallerTreeEqual $before (Get-InstallerTreeSnapshot $production) 'Fake production' } 'Production snapshot detects added files'
Remove-Item -LiteralPath (Join-Path $production 'new-file.txt')
Remove-Item -LiteralPath $prodResource
Refuses { Assert-InstallerTreeEqual $before (Get-InstallerTreeSnapshot $production) 'Fake production' } 'Production snapshot detects removed files'
Put $prodResource 'IMMUTABLE FAKE PRODUCTION RESOURCE'
$absent=Join-Path $proof 'initially-absent-product'
$absentBefore=Get-InstallerTreeSnapshot $absent
[void][IO.Directory]::CreateDirectory($absent)
Refuses { Assert-InstallerTreeEqual $absentBefore (Get-InstallerTreeSnapshot $absent) 'Fake absent production' } 'Initially absent production root cannot appear unnoticed'
$aux=Join-Path $proof 'fake-program-files\OpenDuck Pro X'
Put (Join-Path $aux 'LICENSE.txt') 'ORIGINAL LICENSE'
Put (Join-Path $aux 'unins000.exe') 'DUMMY NONEXECUTABLE UNINSTALLER'
$auxBefore=Get-InstallerTreeSnapshot $aux
Put (Join-Path $aux 'unins000.exe') 'CHANGED DUMMY UNINSTALLER'
Refuses { Assert-InstallerTreeEqual $auxBefore (Get-InstallerTreeSnapshot $aux) 'Auxiliary production' } 'Auxiliary production snapshot protects product documents and uninstaller'

$oldResult=Join-Path $project 'build\installer-qa-results-v0.3.0.json'
Put $oldResult '{"oldPass":true}'
Put (Join-Path $project 'build\qa-v0.3.0-upgrade.log') 'HISTORICAL INNO LOG'
$oldHash=Get-InstallerFileHash $oldResult
$archived=Archive-InstallerAttempt $project 'qa' @('build/installer-qa-results-v0.3.0.json','build/qa-v0.3.0-upgrade.log')
Check (-not [IO.File]::Exists($oldResult)) 'An old PASS is invalidated before a new installer attempt'
Check ((Get-InstallerFileHash (Join-Path $archived.archive 'build\installer-qa-results-v0.3.0.json')) -ceq $oldHash) 'Old PASS archived byte-identically instead of deleted'
Check ($archived.previousFiles.Count -eq 2) 'Prior result and Inno log retain their hashes in the attempt record'
Refuses { Archive-InstallerAttempt $project 'qa' @('..\outside.txt') } 'Evidence archive refuses parent traversal'
Refuses { Assert-InstallerPath ('relative\file.txt') } 'Relative evidence paths are rejected'
Refuses { Assert-InstallerPath ($project+'\test.txt:stream') } 'Alternate data streams are rejected'
Refuses { Assert-InstallerPath ($project+'\..\escape') } 'Evidence paths reject traversal before normalization'
$junctionTarget=Join-Path $proof 'fake-link-target'
[void][IO.Directory]::CreateDirectory($junctionTarget)
Put (Join-Path $junctionTarget 'do-not-follow.txt') 'SYNTHETIC LINK TARGET'
$junction=Join-Path $proof 'fake-production-junction'
New-Item -ItemType Junction -Path $junction -Target $junctionTarget | Out-Null
Refuses { Get-InstallerTreeSnapshot $junction } 'Production snapshots refuse junctions rather than following a redirected root'

# Test the actual registry serializer against an in-memory fake provider. It is
# not a second serializer and never calls the real registry provider.
class FakeInstallerKey {
    [hashtable]$Values
    [string[]]$Children
    FakeInstallerKey([hashtable]$Values,[string[]]$Children) { $this.Values=$Values; $this.Children=$Children }
    [string[]] GetValueNames() { return @($this.Values.Keys) }
    [Microsoft.Win32.RegistryValueKind] GetValueKind([string]$Name) { return [Microsoft.Win32.RegistryValueKind]$this.Values[$Name].kind }
    [object] GetValue([string]$Name,[object]$Default,[Microsoft.Win32.RegistryValueOptions]$Options) { return $this.Values[$Name].data }
    [string[]] GetSubKeyNames() { return $this.Children }
}
function Fake-RegistrySnapshot([hashtable]$RegistryFixtureMap,[string]$RegistryPath) {
    function Test-Path { [CmdletBinding()]param([string]$LiteralPath) return $RegistryFixtureMap.ContainsKey($LiteralPath) }
    function Get-Item { [CmdletBinding()]param([string]$LiteralPath) if(-not $RegistryFixtureMap.ContainsKey($LiteralPath)){throw 'Missing fake key'}; return $RegistryFixtureMap[$LiteralPath] }
    return Get-InstallerRegistrySnapshot $RegistryPath
}
$registryPath='HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A2C52811-2937-42FA-A186-0100469D28E5}_is1'
$fakeKeys=@{}
$rootValues=@{DisplayName=@{kind='String';data='OpenDuck Pro X'};
    UninstallString=@{kind='ExpandString';data='%ProgramFiles%\OpenDuck Pro X\unins000.exe'};
    UnknownBinary=@{kind='Binary';data=[byte[]]@(0,255,5)};
    UnknownMulti=@{kind='MultiString';data=[string[]]@('first','second')};
    UnknownDword=@{kind='DWord';data=[int]42};UnknownQword=@{kind='QWord';data=[long]9223372036854775807}}
$fakeKeys[$registryPath]=[FakeInstallerKey]::new($rootValues,@('Child'))
$fakeKeys[$registryPath+'\Child']=[FakeInstallerKey]::new(@{Extra=@{kind='String';data='child metadata'}},@())
$registryBefore=Fake-RegistrySnapshot $fakeKeys $registryPath
Check ($registryBefore.keys.Count -eq 2 -and $registryBefore.keys[0].values.Count -eq 6) 'Registry snapshot includes every value and child key'
$expand=@($registryBefore.keys[0].values | Where-Object {$_.name -eq 'UninstallString'})[0]
Check ($expand.kind -eq 'ExpandString' -and $expand.data -ceq '%ProgramFiles%\OpenDuck Pro X\unins000.exe') 'Registry snapshot preserves unexpanded string kind and bytes'
$binary=@($registryBefore.keys[0].values | Where-Object {$_.name -eq 'UnknownBinary'})[0]
Check ($binary.data -ceq 'AP8F') 'Registry binary values are captured exactly as base64'
$multi=@($registryBefore.keys[0].values | Where-Object {$_.name -eq 'UnknownMulti'})[0]
Check (($multi.data -join '|') -ceq 'first|second') 'Registry multi-string ordering retained'
$rootValues.UnknownDword.data=[int]43
Refuses { Assert-InstallerTreeEqual $registryBefore (Fake-RegistrySnapshot $fakeKeys $registryPath) 'Uninstall metadata' } 'Changing an additional uninstall value is detected'
$rootValues.UnknownDword.data=[int]42
$rootValues.UnknownDword.kind='QWord'
Refuses { Assert-InstallerTreeEqual $registryBefore (Fake-RegistrySnapshot $fakeKeys $registryPath) 'Uninstall metadata' } 'Registry value kind changes cannot hide behind equal numeric data'
$rootValues.UnknownDword.kind='DWord'
$fakeKeys[$registryPath+'\Child'].Values.Extra.data='ALTERED CHILD METADATA'
Refuses { Assert-InstallerTreeEqual $registryBefore (Fake-RegistrySnapshot $fakeKeys $registryPath) 'Uninstall metadata' } 'Uninstall subkey changes are detected'
$noRegistry=Fake-RegistrySnapshot @{} $registryPath
Check (-not $noRegistry.exists -and $noRegistry.keys.Count -eq 0) 'Initially absent uninstall registration is captured explicitly'
Refuses { Fake-RegistrySnapshot @{} 'HKLM:\Software\Unrelated' } 'Registry serializer refuses unrelated application keys'

$allInputs=@{input=$inputs; tree=$before; registry=$registryBefore; payload=$payload}
$allInputs | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath (Join-Path $proof 'synthetic-canonical-records.json') -Encoding utf8
[ordered]@{fixtureOnly=$true; productionInstallPerformed=$false; realRegistryAccessPerformed=$false;
    installerOrDawLaunched=$false; passed=$checks.Count; failed=0; checks=@($checks); proofRoot=$proof;
    helperSHA256=(Get-InstallerFileHash (Join-Path $PSScriptRoot 'installer_evidence_v030.ps1'))} |
    ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $proof 'FIXTURE-RESULTS.json') -Encoding utf8
Write-Output ("RESULT "+$checks.Count+" passed, 0 failed")
Write-Output $proof
