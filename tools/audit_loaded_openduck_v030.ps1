[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][int]$LiveProcessId,
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [string]$Output = ''
)
# OS module metadata and on-disk hashes only. Never reads process memory,
# launches/stops Live, changes preferences, or writes a loaded module.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Safe-Path([string]$Value) {
    if ($Value -notmatch '^[A-Za-z]:[\\/]' -or $Value.Substring(2) -match ':' -or $Value -match '(^|[\\/])\.{1,2}([\\/]|$)') { throw 'Absolute local path without relative components/ADS required' }
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
try {
    if ($LiveProcessId -le 0) { throw 'Positive explicit Live process ID required' }
    $project=Safe-Path $ProjectRoot
    $expected=Safe-Path (Join-Path $project 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3')
    if (-not [IO.File]::Exists($expected)) { throw 'Final project DLL missing' }
    $targetHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $expected).Hash.ToLowerInvariant()
    $qaRoot=Safe-Path (Join-Path $project 'work\Ableton-QA-v0.3.0')
    $baselinePath=Safe-Path (Join-Path $qaRoot 'baseline.json')
    $baselineDigest=Safe-Path (Join-Path $qaRoot 'baseline.json.sha256')
    $baselineHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $baselinePath).Hash.ToLowerInvariant()
    if ($baselineHash -ne [IO.File]::ReadAllText($baselineDigest).Trim()) { throw 'Baseline integrity check failed' }
    $baseline=[IO.File]::ReadAllText($baselinePath) | ConvertFrom-Json
    if ($baseline.version -ne '0.3.0' -or -not $baseline.baselineComplete -or $baseline.targetBinarySHA256 -ne $targetHash -or -not ([string]$baseline.targetBinary).Equals($expected,[StringComparison]::OrdinalIgnoreCase)) { throw 'Final target does not match the pre-session baseline' }
    $live=Get-Process -Id $LiveProcessId
    if ($live.ProcessName -notlike 'Ableton Live*') { throw 'Selected process is not Ableton Live' }
    $hostExecutable=Safe-Path $live.Path
    $hostVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($hostExecutable).ProductVersion
    $modules=@($live.Modules | Where-Object { [IO.Path]::GetFileName($_.FileName) -ieq 'OpenDuck Pro X.vst3' })
    $records=@()
    foreach ($module in $modules) {
        $path=Safe-Path $module.FileName
        $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
        $records += [ordered]@{path=$path; sha256=$hash;
            expectedProjectPath=$path.Equals($expected,[StringComparison]::OrdinalIgnoreCase);
            matchesTargetHash=$hash -eq $targetHash}
    }
    $allMatch=$records.Count -gt 0 -and @($records | Where-Object { -not $_.expectedProjectPath -or -not $_.matchesTargetHash }).Count -eq 0
    $report=[ordered]@{version='0.3.0'; atUTC=[DateTime]::UtcNow.ToString('O');
        processId=$LiveProcessId; processName=$live.ProcessName; hostExecutable=$hostExecutable;
        hostProductVersion=$hostVersion; expectedBinary=$expected;
        expectedSHA256=$targetHash; baselineSHA256=$baselineHash; loadedOpenDuckModules=$records;
        checks=[ordered]@{openDuckModuleFound=$records.Count -gt 0; allLoadedOpenDuckModulesAreFinalProjectBinary=$allMatch};
        proofPassed=$allMatch; scope='Read-only OS module filename enumeration and on-disk file hashes; no process-memory reads, UI actions or preference changes.'}
    $rendered=($report | ConvertTo-Json -Depth 8)+[Environment]::NewLine
    if ($Output) {
        $out=Safe-Path $Output
        if ([IO.Path]::GetDirectoryName($out) -ine $qaRoot -or [IO.Path]::GetExtension($out) -ine '.json' -or [IO.Path]::GetFileName($out) -in @('baseline.json','POST-RUN-REPORT.json')) { throw 'Use a new module-proof JSON directly inside existing QA root' }
        if (-not [IO.Directory]::Exists($qaRoot)) { throw 'Prepare final QA baseline first' }
        $stream=[IO.File]::Open($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $bytes=[Text.UTF8Encoding]::new($false).GetBytes($rendered); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
    }
    $rendered
    if ($allMatch) { exit 0 } else { exit 2 }
} catch {
    [Console]::Error.WriteLine('Loaded-module evidence failed: '+$_.Exception.Message)
    exit 1
}
