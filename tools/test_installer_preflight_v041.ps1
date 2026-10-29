[CmdletBinding()]
param(
    [string]$Preflight = '',
    [string]$OutputDirectory = ''
)

# Isolated Win32 image/file-lock tests. Does not install, start audio, operate a
# DAW, or ask Restart Manager to shut down/restart another process.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'installer_evidence_v041.ps1')
$projectRoot = Assert-InstallerPath ([IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)))
$workRoot = Assert-InstallerPath (Join-Path $projectRoot 'work\revision041') $projectRoot
if (-not $Preflight) { $Preflight = Join-Path $projectRoot 'build\installer-preflight\OpenDuck-Installer-Preflight.exe' }
$Preflight = Assert-InstallerPath $Preflight $projectRoot
if (-not (Test-Path -LiteralPath $Preflight -PathType Leaf)) { throw "Build preflight first: $Preflight" }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $workRoot ('InstallerPreflight-R17-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$OutputDirectory = Assert-InstallerPath $OutputDirectory $workRoot
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh evidence directory; prior evidence is preserved.' }
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$fixtureRoot = Assert-InstallerPath (Join-Path $OutputDirectory 'isolated-target') $OutputDirectory
[void][IO.Directory]::CreateDirectory($fixtureRoot)
$payload = Assert-InstallerPath (Join-Path $projectRoot 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3') $projectRoot
$expectedPayloadSHA = 'a2bef839de18d19e1f58d68356a95b043e6024a880653c5e6975d94e772278b0'
if ((Get-InstallerFileHash $payload) -cne $expectedPayloadSHA) { throw 'R17 must retain the R16 payload exactly.' }
$mapped = Assert-InstallerPath (Join-Path $fixtureRoot 'Mapped-R16-Payload.dll') $fixtureRoot
Copy-Item -LiteralPath $payload -Destination $mapped
$sentinel = Assert-InstallerPath (Join-Path $fixtureRoot 'resource-sentinel.txt') $fixtureRoot
[IO.File]::WriteAllText($sentinel, 'Existing resources and personal state must survive a refused upgrade.', [Text.UTF8Encoding]::new($false))
$baseline = Get-InstallerTreeSnapshot $fixtureRoot
$toolSHA = Get-InstallerFileHash $Preflight
$checks = [Collections.Generic.List[object]]::new()
$script:passed = 0
$script:failed = 0
$script:probeIndex = 0

function Check([bool]$Condition, [string]$Name) {
    $checks.Add([ordered]@{name=$Name;passed=$Condition})
    if ($Condition) { $script:passed++; Write-Output "PASS $Name" }
    else { $script:failed++; Write-Output "FAIL $Name" }
}

function Invoke-Probe([string[]]$Paths, [string]$Label) {
    $script:probeIndex++
    $report = Join-Path $OutputDirectory ('{0:D2}-{1}.txt' -f $script:probeIndex, $Label)
    $stdout = Join-Path $OutputDirectory ('{0:D2}-{1}-stdout.txt' -f $script:probeIndex, $Label)
    $stderr = Join-Path $OutputDirectory ('{0:D2}-{1}-stderr.txt' -f $script:probeIndex, $Label)
    $probeArgs = [Collections.Generic.List[string]]::new()
    foreach ($candidate in $Paths) { $probeArgs.Add('--path'); $probeArgs.Add($candidate) }
    $probeArgs.Add('--report'); $probeArgs.Add($report)
    & $Preflight @probeArgs 1> $stdout 2> $stderr
    $code = $LASTEXITCODE
    $text = if (Test-Path -LiteralPath $report -PathType Leaf) { [IO.File]::ReadAllText($report, [Text.Encoding]::UTF8) } else { '' }
    return [pscustomobject]@{exitCode=$code;report=$report;text=$text;stdout=$stdout;stderr=$stderr}
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OpenDuckPreflightFixture17 {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DeleteFileW(string path);
}
'@

$image = [IntPtr]::Zero
$handle = $null
$deleteError = $null
try {
    $clear = Invoke-Probe @($mapped, $sentinel) 'clear'
    Check ($clear.exitCode -eq 0 -and $clear.text.Contains('ODX_PREFLIGHT_STATUS=CLEAR')) 'Unlocked isolated payload and resource pass preflight'
    Assert-InstallerTreeEqual $baseline (Get-InstallerTreeSnapshot $fixtureRoot) 'Clear preflight fixture'
    Check $true 'Successful preflight leaves every existing file and directory unchanged'

    # DONT_RESOLVE_DLL_REFERENCES maps a PE image without DllMain/import loading:
    # no plugin entry point, host, device, player, or audio engine is invoked.
    $image = [OpenDuckPreflightFixture17]::LoadLibraryExW($mapped, [IntPtr]::Zero, 1)
    if ($image -eq [IntPtr]::Zero) { throw ('LoadLibraryEx fixture failed: ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
    $deleted = [OpenDuckPreflightFixture17]::DeleteFileW($mapped)
    $deleteError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Check ((-not $deleted) -and $deleteError -eq 5) 'Mapped PE fixture reproduces DeleteFile error 5 without touching production'
    if ($deleted) { throw 'Unexpected successful deletion invalidates the mapped-image fixture.' }
    $busy = Invoke-Probe @($mapped, $sentinel) 'mapped-image'
    Check ($busy.exitCode -eq 32 -and $busy.text.Contains('ODX_PREFLIGHT_STATUS=LOCKED')) 'Mapped image is blocked by non-destructive preflight before replacement'
    Check ($busy.text.Contains($mapped)) 'Busy report identifies the exact blocked file'
    Check ([regex]::IsMatch($busy.text, '(?m)^PROCESS_PID=' + [regex]::Escape([string]$PID) + '\r?$')) 'Restart Manager report identifies the actual image-owning fixture PID'
    Assert-InstallerTreeEqual $baseline (Get-InstallerTreeSnapshot $fixtureRoot) 'Mapped preflight fixture'
    Check $true 'Refused mapped-image upgrade preserves payload, resources, and directory set exactly'
    $again = Invoke-Probe @($mapped) 'mapped-repeat'
    Check ($again.exitCode -eq 32) 'Repeated mapped-file preflight remains blocked without terminating the owner'
    Check ([OpenDuckPreflightFixture17]::FreeLibrary($image)) 'Fixture owner explicitly unloads only its own mapped test image'
    $image = [IntPtr]::Zero
    $unlocked = Invoke-Probe @($mapped, $sentinel) 'after-unload'
    Check ($unlocked.exitCode -eq 0 -and $unlocked.text.Contains('ODX_PREFLIGHT_STATUS=CLEAR')) 'Same target passes after its owner voluntarily releases the image'
    Assert-InstallerTreeEqual $baseline (Get-InstallerTreeSnapshot $fixtureRoot) 'Unlocked preflight fixture'
    Check $true 'Unlock and recheck leave target bytes unchanged'

    $handle = [IO.File]::Open($sentinel, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $shared = Invoke-Probe @($sentinel) 'exclusive-file'
    Check ($shared.exitCode -eq 32) 'An ordinary exclusive file handle also blocks preflight'
    $handle.Dispose(); $handle = $null
    $released = Invoke-Probe @($sentinel) 'exclusive-released'
    Check ($released.exitCode -eq 0) 'Releasing an ordinary exclusive handle allows preflight'

    $absent = Join-Path $fixtureRoot 'not-installed\Contents\new-plugin.vst3'
    $fresh = Invoke-Probe @($absent) 'fresh-target'
    Check ($fresh.exitCode -eq 0 -and $fresh.text.Contains('FILE_STATE=ABSENT')) 'An initially absent payload target permits a fresh install'
    $invalid = Invoke-Probe @('relative-target.dll') 'invalid-path'
    Check ($invalid.exitCode -eq 33) 'Invalid or unresolvable probe paths fail closed instead of reporting clear'
    Assert-InstallerTreeEqual $baseline (Get-InstallerTreeSnapshot $fixtureRoot) 'All preflight fixtures'
    Check $true 'All probe outcomes preserve the complete isolated target tree'
    Check ((Get-InstallerFileHash $payload) -ceq $expectedPayloadSHA) 'Original R16 release payload remains bit-identical'
    Check ((Get-InstallerFileHash $Preflight) -ceq $toolSHA) 'Preflight executable remains identical throughout verification'
}
finally {
    if ($null -ne $handle) { $handle.Dispose() }
    if ($image -ne [IntPtr]::Zero) { [void][OpenDuckPreflightFixture17]::FreeLibrary($image) }
}

$result = [ordered]@{schema='openduck-preflight-lock-proof-v1';complete=($failed -eq 0);passed=$passed;failed=$failed;
    fixturePID=$PID;deleteFileError=$deleteError;payloadSHA256=$expectedPayloadSHA;preflightSHA256=$toolSHA;
    fixtureRoot=$fixtureRoot;productionInstallPerformed=$false;abletonControlled=$false;audioStarted=$false;
    completedUTC=[DateTime]::UtcNow.ToString('O');checks=@($checks)}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'results.json'), ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output "RESULT InstallerPreflight $passed passed, $failed failed"
if ($failed) { exit 1 }
