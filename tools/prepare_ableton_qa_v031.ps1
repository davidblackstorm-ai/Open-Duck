[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Baseline','Restore')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$PreferencesRoot,
    [Parameter(Mandatory=$true)][string]$OriginalSet,
    [Parameter(Mandatory=$true)][string]$QaSet,
    [Parameter(Mandatory=$true)][string]$ProductionBundle,
    [switch]$AllowRunningLiveBaseline
)

# File-only preparation/restoration for the single final authorized QA session.
# Never launches/stops Live, edits registry, enumerates unrelated preferences,
# installs a VST, changes the original set, or overwrites an existing baseline.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$settingNames = @('Preferences.cfg','Library.cfg','Options.txt','UsageData.cfg')
$utf8 = [System.Text.UTF8Encoding]::new($false)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-SafeAbsolutePath([string]$Candidate, [string]$Boundary = '') {
    Assert-True ($Candidate -match '^[A-Za-z]:[\\/]') "Absolute local drive path required: $Candidate"
    Assert-True ($Candidate.Substring(2) -notmatch ':') 'Alternate data streams are forbidden'
    Assert-True ($Candidate -notmatch '(^|[\\/])\.{1,2}([\\/]|$)') 'Relative path components are forbidden'
    $full = [IO.Path]::GetFullPath($Candidate).TrimEnd('\','/')
    Assert-True ($full.Length -gt 3) 'A drive root is not an allowed file-operation target'
    $walk = $full
    while ($walk) {
        $entry = Get-Item -LiteralPath $walk -Force -ErrorAction SilentlyContinue
        if ($null -ne $entry) {
            Assert-True (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Reparse/link path forbidden: $walk"
        }
        $parent = [IO.Path]::GetDirectoryName($walk)
        if (-not $parent -or $parent -eq $walk) { break }
        $walk = $parent
    }
    if ($Boundary) {
        $prefix = $Boundary.TrimEnd('\') + '\'
        Assert-True ($full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) "Path escapes boundary: $full"
    }
    return $full
}

function Get-LiveIdentity {
    foreach ($process in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'Ableton Live*' } | Sort-Object Id)) {
        $started = $process.StartTime.ToUniversalTime()
        [ordered]@{id=[int]$process.Id; processName=$process.ProcessName; startTimeUtc=$started.ToString('O');
            startTimeUtcTicks=$started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)}
    }
}

function Get-LiveKey($Process) {
    Assert-True ($Process.id -is [int] -or $Process.id -is [long]) 'Invalid Live PID in baseline'
    Assert-True ($Process.id -gt 0 -and $Process.processName -like 'Ableton Live*' -and [string]$Process.startTimeUtcTicks -match '^\d{16,20}$') 'Invalid Live identity in baseline'
    $storedStart = Get-StoredUtcDate $Process.startTimeUtc
    Assert-True ($storedStart.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture) -ceq [string]$Process.startTimeUtcTicks) 'Live start timestamp and ticks disagree'
    return ([string]$Process.id + ':' + [string]$Process.startTimeUtcTicks)
}

function Assert-LiveState($Expected) {
    $expectedKeys = @(@($Expected) | ForEach-Object { Get-LiveKey $_ } | Sort-Object)
    $actualKeys = @(Get-LiveIdentity | ForEach-Object { Get-LiveKey $_ } | Sort-Object)
    Assert-True (@($expectedKeys | Select-Object -Unique).Count -eq $expectedKeys.Count) 'Duplicate baseline Live identity'
    Assert-True (@(@($Expected) | ForEach-Object { $_.id } | Select-Object -Unique).Count -eq @($Expected).Count) 'Duplicate baseline Live PID'
    Assert-True ($expectedKeys.Count -eq $actualKeys.Count -and [string]::Join('|',$expectedKeys) -ceq [string]::Join('|',$actualKeys)) 'Live process set changed: original PID/start time missing or additional QA process still running. No process will be stopped.'
}

function Assert-LiveProfile([string]$Path) {
    Assert-True ([IO.Path]::GetFileName($Path) -ieq 'Preferences' -and [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($Path)) -ieq 'Live 12.4.15') 'Running-Live QA must use the explicit Live 12.4.15 Preferences directory'
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-StoredUtcDate($Value) {
    # ConvertFrom-Json on recent PowerShell can materialize ISO strings as
    # DateTime. Re-parsing its localized string loses both format and precision.
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    return [DateTime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}

function Write-NewJson([string]$Path, $Value) {
    Assert-True (-not (Test-Path -LiteralPath $Path)) "Refusing to overwrite metadata: $Path"
    $rendered = ($Value | ConvertTo-Json -Depth 12) + [Environment]::NewLine
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $bytes = $utf8.GetBytes($rendered); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
}

function Get-FileRecord([string]$Path, [string]$Name) {
    [void](Get-SafeAbsolutePath $Path)
    Assert-True (-not [IO.Directory]::Exists($Path)) "Whitelisted file is a directory: $Path"
    $exists = [IO.File]::Exists($Path)
    if (-not $exists) { return [ordered]@{name=$Name; exists=$false; sha256=$null; length=$null; attributes=$null; creationTimeUtc=$null; lastWriteTimeUtc=$null} }
    $item = Get-Item -LiteralPath $Path -Force
    return [ordered]@{name=$Name; exists=$true; sha256=(Get-Hash $Path); length=$item.Length;
        attributes=[int]$item.Attributes; creationTimeUtc=$item.CreationTimeUtc.ToString('O'); lastWriteTimeUtc=$item.LastWriteTimeUtc.ToString('O')}
}

function Copy-NewVerified([string]$Source, [string]$Destination, [string]$ExpectedHash) {
    [void](Get-SafeAbsolutePath $Source)
    [void](Get-SafeAbsolutePath $Destination)
    Assert-True ([IO.File]::Exists($Source)) "Snapshot source missing: $Source"
    Assert-True ((Get-Hash $Source) -eq $ExpectedHash) "Source changed before snapshot: $Source"
    [IO.File]::Copy($Source,$Destination,$false)
    Assert-True ((Get-Hash $Destination) -eq $ExpectedHash -and (Get-Hash $Source) -eq $ExpectedHash) "Snapshot failed verification: $Source"
}

function Get-BundleFiles([string]$Bundle) {
    [void](Get-SafeAbsolutePath $Bundle)
    Assert-True (-not [IO.File]::Exists($Bundle)) 'Production bundle must be a directory or initially absent'
    if (-not [IO.Directory]::Exists($Bundle)) { return }
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($Bundle)
    $result = [Collections.Generic.List[object]]::new()
    while ($stack.Count -gt 0) {
        $current = $stack.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $current -Force)) {
            $path = Get-SafeAbsolutePath $item.FullName $Bundle
            if ($item.PSIsContainer) { $stack.Push($path) }
            else {
                Assert-True ($result.Count -lt 10000) 'Unexpectedly large plugin bundle'
                $relative = $path.Substring($Bundle.Length+1).Replace('\','/')
                $result.Add([ordered]@{relative=$relative; sha256=(Get-Hash $path)})
            }
        }
    }
    return @($result | Sort-Object { $_.relative.ToLowerInvariant() })
}

function Assert-BundleUnchanged($Baseline, [string]$Bundle) {
    Assert-True ([IO.Directory]::Exists($Bundle) -eq [bool]$Baseline.bundleExists) 'Production bundle existence changed; no production files will be written'
    $current = @(Get-BundleFiles $Bundle)
    $expected = @($Baseline.bundleFiles)
    Assert-True ($current.Count -eq $expected.Count) 'Production bundle payload count changed'
    for ($index=0; $index -lt $current.Count; $index++) {
        Assert-True ($current[$index].relative -eq $expected[$index].relative -and $current[$index].sha256 -eq $expected[$index].sha256) 'Production bundle hash changed'
    }
}

try {
    $project = Get-SafeAbsolutePath $ProjectRoot
    Assert-True (-not $AllowRunningLiveBaseline) 'Hotfix031 baseline and restore require Live closed; reopen the preserved recovered set only after verified restoration'
    $preferences = Get-SafeAbsolutePath $PreferencesRoot
    Assert-LiveProfile $preferences
    $original = Get-SafeAbsolutePath $OriginalSet $project
    $qaSetPath = Get-SafeAbsolutePath $QaSet $project
    $bundle = Get-SafeAbsolutePath $ProductionBundle
    $recovery = Get-SafeAbsolutePath (Join-Path $project 'work\crash-0.3.0-20261028\private\recovery-before-close') $project
    Assert-True ([IO.Directory]::Exists($recovery) -and [IO.File]::Exists((Join-Path $recovery 'MANIFEST.json'))) 'The private original recovery archive and manifest must be present and untouched'
    $qaRoot = Get-SafeAbsolutePath (Join-Path $project 'work\Ableton-QA-v0.3.1') $project
    $baselinePath = Get-SafeAbsolutePath (Join-Path $qaRoot 'baseline.json') $qaRoot
    $baselineDigest = Get-SafeAbsolutePath (Join-Path $qaRoot 'baseline.json.sha256') $qaRoot
    $snapshots = Get-SafeAbsolutePath (Join-Path $qaRoot 'Preferences-before') $qaRoot
    $originalSnapshot = Get-SafeAbsolutePath (Join-Path $qaRoot 'TEST-LAB-before.als') $qaRoot
    $target = Get-SafeAbsolutePath (Join-Path $project 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3\Contents\x86_64-win\OpenDuck Pro X.vst3') $project
    Assert-True ([IO.Directory]::Exists($project) -and [IO.Directory]::Exists($preferences)) 'Project and explicit PreferencesRoot must exist'
    Assert-True (-not $preferences.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -and -not $project.StartsWith($preferences+'\',[StringComparison]::OrdinalIgnoreCase) -and $preferences -ne $project) 'Project and preference boundaries must be separate'
    Assert-True ([IO.Path]::GetFileName($bundle) -eq 'OpenDuck Pro X.vst3') 'Only the named OpenDuck production bundle may be hashed'
    Assert-True (-not $bundle.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -and -not $bundle.StartsWith($preferences+'\',[StringComparison]::OrdinalIgnoreCase)) 'Production bundle must be separate from project and preferences'
    Assert-True ([IO.Path]::GetExtension($original) -eq '.als' -and [IO.File]::Exists($original)) 'Original named project .als must exist'
    Assert-True ([IO.Path]::GetExtension($qaSetPath) -eq '.als' -and $qaSetPath -ne $original) 'QA .als must be a distinct named project path'
    Assert-True ([IO.File]::Exists($target)) 'Final target VST3 binary must already exist'
    if ($AllowRunningLiveBaseline) { Assert-LiveProfile $preferences }

    if ($Mode -eq 'Baseline') {
        $initialLive = @(Get-LiveIdentity)
        Assert-True ($AllowRunningLiveBaseline -or $initialLive.Count -eq 0) 'Live must be closed by default; preserving existing Live processes requires explicit -AllowRunningLiveBaseline'
        Assert-True (-not (Test-Path -LiteralPath $qaRoot)) 'QA baseline directory already exists; it will never be overwritten'
        Assert-True (-not (Test-Path -LiteralPath $qaSetPath)) 'The planned QA .als already exists; choose a distinct QA path before baseline'
        $settings = @()
        foreach ($name in $settingNames) { $settings += Get-FileRecord (Get-SafeAbsolutePath (Join-Path $preferences $name) $preferences) $name }
        $bundleFiles = @(Get-BundleFiles $bundle)
        $targetHash = Get-Hash $target
        $originalHash = Get-Hash $original
        $recoveryFiles = @(Get-BundleFiles $recovery)
        Assert-True ($recoveryFiles.Count -gt 0) 'Recovery archive cannot be empty'
        [void][IO.Directory]::CreateDirectory($snapshots)
        [void](Get-SafeAbsolutePath $snapshots $qaRoot)
        foreach ($item in $settings) {
            if ($item.exists) { Copy-NewVerified (Join-Path $preferences $item.name) (Join-Path $snapshots $item.name) $item.sha256 }
        }
        Copy-NewVerified $original $originalSnapshot $originalHash
        Assert-LiveState $initialLive
        Assert-True ((Get-Hash $target) -eq $targetHash) 'Target binary changed during baseline capture'
        $baseline = [ordered]@{version='0.3.1'; baselineComplete=$true; startedAtUTC=[DateTime]::UtcNow.ToString('O');
            projectRoot=$project; preferencesRoot=$preferences; originalSet=$original; qaSet=$qaSetPath;
            settings=$settings; bundle=$bundle; bundleExists=[IO.Directory]::Exists($bundle); bundleFiles=$bundleFiles;
            targetBinary=$target; targetBinarySHA256=$targetHash; testProjectSHA256=$originalHash;
            recoveryArchive=[ordered]@{path=$recovery; bundleExists=$true; bundleFiles=$recoveryFiles};
            abletonInitiallyRunning=($initialLive.Count -gt 0); allowRunningLiveBaseline=[bool]$AllowRunningLiveBaseline;
            processIdentitySchema=1; initialLiveProcesses=@($initialLive);
            loadingPlan='After all final0.3.1 non-DAW and installer gates: Live12.4.15 using the installed final bundle. Preserve original recovery archive and named original TESTLAB. Recover personal work through UI and Save As a distinct private copy before QA. Close Live before restoring exactly four preference files. A later authorized reopen is recorded separately.';
            privateArtifacts='Preferences-before, TEST-LAB-before.als, baseline.json and Restore-snapshots are private and excluded from release archives.'}
        Assert-BundleUnchanged ([pscustomobject]$baseline) $bundle
        Assert-BundleUnchanged ([pscustomobject]$baseline.recoveryArchive) $recovery
        Write-NewJson $baselinePath $baseline
        [IO.File]::WriteAllText($baselineDigest,(Get-Hash $baselinePath)+[Environment]::NewLine,$utf8)
        [pscustomobject]@{mode='Baseline'; baseline=$baselinePath; baselineSHA256=(Get-Hash $baselinePath); targetSHA256=$targetHash; preferencesCaptured=4; liveClosed=($initialLive.Count -eq 0); initialLiveProcesses=@($initialLive); allowRunningLiveBaseline=[bool]$AllowRunningLiveBaseline} | ConvertTo-Json -Depth 5
        exit 0
    }

    Assert-True ([IO.File]::Exists($baselinePath) -and [IO.File]::Exists($baselineDigest)) 'A complete baseline and digest are required'
    Assert-True ((Get-Hash $baselinePath) -eq ([IO.File]::ReadAllText($baselineDigest).Trim())) 'Baseline metadata digest changed'
    $saved = [IO.File]::ReadAllText($baselinePath) | ConvertFrom-Json
    Assert-True ($saved.version -eq '0.3.1' -and $saved.baselineComplete -is [bool] -and $saved.baselineComplete) 'Wrong/incomplete baseline'
    Assert-True ((Get-SafeAbsolutePath ([string]$saved.recoveryArchive.path) $project) -eq $recovery) 'Recovery archive identity mismatch'
    Assert-BundleUnchanged $saved.recoveryArchive $recovery
    foreach ($pair in @(@('projectRoot',$project),@('preferencesRoot',$preferences),@('originalSet',$original),@('qaSet',$qaSetPath),@('bundle',$bundle),@('targetBinary',$target))) {
        $name = $pair[0]
        Assert-True ((Get-SafeAbsolutePath ([string]$saved.$name)) -eq $pair[1]) "Baseline path mismatch: $name"
    }
    $allowProperty = $saved.PSObject.Properties['allowRunningLiveBaseline']
    $savedAllow = $false
    if ($null -ne $allowProperty) {
        Assert-True ($allowProperty.Value -is [bool]) 'Invalid running-Live authorization flag'
        $savedAllow = $allowProperty.Value
    }
    Assert-True ($savedAllow -eq [bool]$AllowRunningLiveBaseline) 'Restore must explicitly match the baseline -AllowRunningLiveBaseline option'
    $initialLive = @()
    $identityProperty = $saved.PSObject.Properties['initialLiveProcesses']
    if ($null -ne $identityProperty) { $initialLive = @($identityProperty.Value) }
    Assert-True ($saved.abletonInitiallyRunning -is [bool] -and $saved.abletonInitiallyRunning -eq ($initialLive.Count -gt 0)) 'Baseline running state and initial identities disagree'
    Assert-True ($savedAllow -or $initialLive.Count -eq 0) 'A running baseline requires explicit authorization'
    if ($savedAllow) { Assert-LiveProfile $preferences }
    Assert-LiveState $initialLive
    $items = @($saved.settings)
    Assert-True ($items.Count -eq 4 -and @($items | Select-Object -ExpandProperty name -Unique).Count -eq 4) 'Baseline must contain four unique settings'
    foreach ($item in $items) {
        Assert-True ($settingNames -ccontains [string]$item.name -and $item.exists -is [bool]) 'Invalid settings whitelist entry'
        $destination = Get-SafeAbsolutePath (Join-Path $preferences $item.name) $preferences
        $snapshot = Get-SafeAbsolutePath (Join-Path $snapshots $item.name) $snapshots
        Assert-True (-not [IO.Directory]::Exists($destination)) 'Setting destination must be a regular file or absent'
        if ($item.exists) {
            Assert-True ($item.sha256 -match '^[0-9a-fA-F]{64}$' -and [IO.File]::Exists($snapshot)) 'Initial preference snapshot missing/hash invalid'
            Assert-True ((Get-Hash $snapshot) -eq $item.sha256) 'Initial snapshot hash changed; refusing restoration'
            [void](Get-StoredUtcDate $item.creationTimeUtc)
            [void](Get-StoredUtcDate $item.lastWriteTimeUtc)
            Assert-True (([int]$item.attributes -band [int][IO.FileAttributes]::ReparsePoint) -eq 0) 'Invalid saved file attributes'
        } else { Assert-True (-not (Test-Path -LiteralPath $snapshot)) 'Snapshot unexpectedly exists for an initially absent setting' }
    }
    Assert-True ((Get-Hash $originalSnapshot) -eq $saved.testProjectSHA256 -and (Get-Hash $original) -eq $saved.testProjectSHA256) 'Original set or snapshot changed; no project set will be overwritten'
    Assert-True ((Get-Hash $target) -eq $saved.targetBinarySHA256) 'Final target binary changed during QA'
    Assert-BundleUnchanged $saved $bundle
    Assert-LiveState $initialLive

    # Every generated current whitelist file is snapshotted BEFORE any restore/delete.
    # Failures preserve both the original baseline and this reversible pre-restore state.
    $restoreId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N')
    $restoreRoot = Get-SafeAbsolutePath (Join-Path $qaRoot ('Restore-snapshots\'+$restoreId)) $qaRoot
    [void][IO.Directory]::CreateDirectory($restoreRoot)
    [void](Get-SafeAbsolutePath $restoreRoot $qaRoot)
    $generated = @()
    foreach ($name in $settingNames) {
        $path = Get-SafeAbsolutePath (Join-Path $preferences $name) $preferences
        $record = Get-FileRecord $path $name
        $generated += $record
        if ($record.exists) { Copy-NewVerified $path (Get-SafeAbsolutePath (Join-Path $restoreRoot $name) $restoreRoot) $record.sha256 }
    }
    Write-NewJson (Join-Path $restoreRoot 'before-restore.json') ([ordered]@{version='0.3.1'; atUTC=[DateTime]::UtcNow.ToString('O'); baselineSHA256=(Get-Hash $baselinePath); settings=$generated})
    $results = @()
    foreach ($item in $items) {
        Assert-LiveState $initialLive
        $destination = Get-SafeAbsolutePath (Join-Path $preferences $item.name) $preferences
        $snapshot = Get-SafeAbsolutePath (Join-Path $snapshots $item.name) $snapshots
        $was = @($generated | Where-Object { $_.name -ceq $item.name })[0]
        $now = Get-FileRecord $destination $item.name
        Assert-True ($now.exists -eq $was.exists -and $now.sha256 -eq $was.sha256) 'Preference changed since pre-restore backup; stop before overwriting it'
        if ($item.exists) {
            Assert-True ((Get-Hash $snapshot) -eq $item.sha256) 'Initial snapshot changed before write'
            if ([IO.File]::Exists($destination)) { [IO.File]::SetAttributes($destination,[IO.FileAttributes]::Normal) }
            [IO.File]::Copy($snapshot,$destination,$true)
            [IO.File]::SetCreationTimeUtc($destination,(Get-StoredUtcDate $item.creationTimeUtc))
            [IO.File]::SetLastWriteTimeUtc($destination,(Get-StoredUtcDate $item.lastWriteTimeUtc))
            [IO.File]::SetAttributes($destination,[IO.FileAttributes][int]$item.attributes)
        } elseif ([IO.File]::Exists($destination)) {
            Remove-Item -LiteralPath $destination -Force
        }
        $restored = Get-FileRecord $destination $item.name
        $ok = $restored.exists -eq $item.exists -and (-not $item.exists -or $restored.sha256 -eq $item.sha256)
        Assert-True $ok "Restored file did not verify: $($item.name)"
        $metadataOk = -not $item.exists -or ($restored.attributes -eq $item.attributes -and $restored.creationTimeUtc -eq (Get-StoredUtcDate $item.creationTimeUtc).ToString('O') -and $restored.lastWriteTimeUtc -eq (Get-StoredUtcDate $item.lastWriteTimeUtc).ToString('O'))
        Assert-True $metadataOk "Restored file metadata did not verify: $($item.name)"
        $results += [ordered]@{name=$item.name; initiallyExisted=$item.exists; restoredExactly=$ok; metadataRestored=$metadataOk; sha256=$restored.sha256}
    }
    Assert-LiveState $initialLive
    Assert-BundleUnchanged $saved $bundle
    Assert-BundleUnchanged $saved.recoveryArchive $recovery
    $restoreReport = [ordered]@{version='0.3.1'; atUTC=[DateTime]::UtcNow.ToString('O'); restoreComplete=$true;
        baselineSHA256=(Get-Hash $baselinePath); generatedFilesBackup=$restoreRoot; settings=$results;
        recoveryArchiveUnchanged=$true; productionBundleUnchanged=$true; liveClosed=($initialLive.Count -eq 0); qaProcessClosed=$true;
        initialProcessStateRestored=$true; noAdditionalLiveProcesses=$true; initialLiveProcesses=@($initialLive);
        allowRunningLiveBaseline=$savedAllow; targetReleaseBinaryUnchanged=((Get-Hash $target) -eq $saved.targetBinarySHA256);
        scope='Only four whitelisted preference files restored; recorded initial Live processes preserved by PID/start time; no process stopped and no original project action performed by this script.'}
    Write-NewJson (Join-Path $restoreRoot 'restore-report.json') $restoreReport
    $restoreReport | ConvertTo-Json -Depth 8
    exit 0
} catch {
    [Console]::Error.WriteLine('Ableton file-only QA preparation/restoration failed: ' + $_.Exception.Message)
    exit 1
}
