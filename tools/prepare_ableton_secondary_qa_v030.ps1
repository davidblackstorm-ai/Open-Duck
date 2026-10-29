[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Baseline','Restore')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$PreferencesRoot,
    [Parameter(Mandatory=$true)][string]$PrimaryBaseline,
    [switch]$Fixture,
    [string]$FixtureRoot = '',
    [string]$FixtureProcessRecords = ''
)

# Supplementary rollback ONLY: never launches/stops Live, writes the primary
# baseline/profile, scans User Library/Undo/logs, installs, or changes a Set.
# Fixture execution has distinct filenames and cannot produce release evidence.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$settingNames = @('Preferences.cfg','Library.cfg','UsageData.cfg','Options.txt')
$utf8 = [Text.UTF8Encoding]::new($false)
$scriptHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-SafeAbsolutePath([string]$Candidate, [string]$Boundary = '') {
    Assert-True ($Candidate -match '^[A-Za-z]:[\\/]') 'Absolute local drive path required'
    Assert-True ($Candidate.Substring(2) -notmatch ':') 'Alternate data streams are forbidden'
    Assert-True ($Candidate -notmatch '(^|[\\/])\.{1,2}([\\/]|$)') 'Relative path components are forbidden'
    $full = [IO.Path]::GetFullPath($Candidate).TrimEnd('\','/')
    Assert-True ($full.Length -gt 3) 'Drive roots are forbidden'
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
        Assert-True ($full.StartsWith($Boundary.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Path escapes its explicit boundary'
    }
    return $full
}

function Get-Hash([string]$Path) {
    [void](Get-SafeAbsolutePath $Path)
    Assert-True ([IO.File]::Exists($Path) -and -not [IO.Directory]::Exists($Path)) "Expected regular file: $Path"
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-StoredUtcDate($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    return [DateTime]::Parse([string]$Value,[Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}

function Get-LiveKey($Process) {
    Assert-True ($Process.id -is [int] -or $Process.id -is [long]) 'Invalid PID type'
    Assert-True ($Process.id -gt 0 -and $Process.processName -like 'Ableton Live*') 'Invalid Live process identity'
    Assert-True ([string]$Process.startTimeUtcTicks -match '^\d{16,20}$') 'Invalid Live start ticks'
    $started = Get-StoredUtcDate $Process.startTimeUtc
    Assert-True ($started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture) -ceq [string]$Process.startTimeUtcTicks) 'Live timestamp and ticks disagree'
    return ([string]$Process.id+':'+[string]$Process.startTimeUtcTicks+':'+[string]$Process.processName)
}

function Get-LiveIdentity {
    if ($Fixture) {
        $document = [IO.File]::ReadAllText($processFixturePath) | ConvertFrom-Json
        Assert-True ($document.fixtureOnly -is [bool] -and $document.fixtureOnly -and $document.synthetic -is [bool] -and $document.synthetic) 'Fixture process records must explicitly declare synthetic=true and fixtureOnly=true'
        return @($document.processes)
    }
    foreach ($process in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'Ableton Live*' } | Sort-Object Id)) {
        $started = $process.StartTime.ToUniversalTime()
        [ordered]@{id=[int]$process.Id; processName=$process.ProcessName;
            startTimeUtc=$started.ToString('O'); startTimeUtcTicks=$started.Ticks.ToString([Globalization.CultureInfo]::InvariantCulture)}
    }
}

function Assert-LiveState($Expected) {
    $expectedKeys = @(@($Expected) | ForEach-Object { Get-LiveKey $_ } | Sort-Object)
    Assert-True (@($expectedKeys | Select-Object -Unique).Count -eq $expectedKeys.Count) 'Duplicate primary process identity'
    Assert-True (@(@($Expected) | ForEach-Object { $_.id } | Select-Object -Unique).Count -eq @($Expected).Count) 'Duplicate primary process PID'
    Assert-True (@(@($Expected) | Where-Object { $_.processName -like '*Beta*' }).Count -eq 0) 'Primary baseline must not contain an active Live Beta'
    $actual = @(Get-LiveIdentity)
    Assert-True (@($actual | Where-Object { $_.processName -like '*Beta*' }).Count -eq 0) 'Live Beta must be closed before secondary snapshot/restoration; no process will be stopped'
    $actualKeys = @($actual | ForEach-Object { Get-LiveKey $_ } | Sort-Object)
    Assert-True ($expectedKeys.Count -eq $actualKeys.Count -and [string]::Join('|',$expectedKeys) -ceq [string]::Join('|',$actualKeys)) 'Original Live PID/start identities changed or an additional process remains; no process will be stopped'
}

function Get-FileRecord([string]$Path, [string]$Name) {
    [void](Get-SafeAbsolutePath $Path)
    Assert-True (-not [IO.Directory]::Exists($Path)) "Whitelisted file is a directory: $Name"
    if (-not [IO.File]::Exists($Path)) {
        return [ordered]@{name=$Name; exists=$false; sha256=$null; length=$null; attributes=$null; creationTimeUtc=$null; lastWriteTimeUtc=$null}
    }
    $item = Get-Item -LiteralPath $Path -Force
    return [ordered]@{name=$Name; exists=$true; sha256=(Get-Hash $Path); length=$item.Length;
        attributes=[int]$item.Attributes; creationTimeUtc=$item.CreationTimeUtc.ToString('O'); lastWriteTimeUtc=$item.LastWriteTimeUtc.ToString('O')}
}

function Assert-SameFile($Actual, $Expected, [string]$Message) {
    Assert-True ($Actual.exists -eq $Expected.exists) $Message
    if ($Expected.exists) {
        Assert-True ($Actual.sha256 -ceq [string]$Expected.sha256 -and $Actual.length -eq $Expected.length -and
            $Actual.attributes -eq $Expected.attributes -and
            $Actual.creationTimeUtc -ceq (Get-StoredUtcDate $Expected.creationTimeUtc).ToString('O') -and
            $Actual.lastWriteTimeUtc -ceq (Get-StoredUtcDate $Expected.lastWriteTimeUtc).ToString('O')) $Message
    }
}

function Copy-NewVerified([string]$Source, [string]$Destination, [string]$ExpectedHash) {
    [void](Get-SafeAbsolutePath $Destination)
    Assert-True ((Get-Hash $Source) -ceq $ExpectedHash) 'Source changed before snapshot'
    [IO.File]::Copy($Source,$Destination,$false)
    Assert-True ((Get-Hash $Destination) -ceq $ExpectedHash -and (Get-Hash $Source) -ceq $ExpectedHash) 'Snapshot content verification failed'
}

function Write-NewJson([string]$Path, $Value) {
    [void](Get-SafeAbsolutePath $Path $secondaryRoot)
    $stream = [IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $bytes=$utf8.GetBytes(($Value | ConvertTo-Json -Depth 16)+[Environment]::NewLine); $stream.Write($bytes,0,$bytes.Length) }
    finally { $stream.Dispose() }
}

function Assert-PrimaryUnchanged {
    Assert-True ((Get-Hash $primaryPath) -ceq $primaryHash -and (Get-Hash $primaryDigestPath) -ceq $primaryDigestHash) 'Primary baseline or its digest changed; no primary file will be written'
}

try {
    $project = Get-SafeAbsolutePath $ProjectRoot
    $preferences = Get-SafeAbsolutePath $PreferencesRoot
    $primaryPath = Get-SafeAbsolutePath $PrimaryBaseline
    if ($Fixture) {
        Assert-True ($FixtureRoot -ne '' -and $FixtureProcessRecords -ne '') '-Fixture requires both explicit fixture paths'
        $fixtureBoundary = Get-SafeAbsolutePath $FixtureRoot
        Assert-True ($fixtureBoundary -match '^[CE]:\\.+\\(?:qa-tools-proof|qa-fixtures)\\secondary-profile-[A-Za-z0-9_-]+$') 'Fixture root must be an explicitly named C:/E: QA fixture directory'
        Assert-True ($project -ieq (Join-Path $fixtureBoundary 'project')) 'Fixture project path must be inside the explicit fixture root'
        Assert-True ($preferences -ieq (Join-Path $fixtureBoundary 'fake-profile\Live 12.3b10\Preferences')) 'Fixture preference path must be the dedicated fake beta profile'
        $processFixturePath = Get-SafeAbsolutePath $FixtureProcessRecords $fixtureBoundary
        Assert-True ($processFixturePath -ieq (Join-Path $fixtureBoundary 'fixture-processes.json')) 'Fixture process records must use the explicit synthetic filename'
        [void](Get-Hash $processFixturePath)
    } else {
        Assert-True ($FixtureRoot -eq '' -and $FixtureProcessRecords -eq '') 'Synthetic process records are forbidden without -Fixture'
        Assert-True ($project -ieq 'E:\Codex\Lavori\OpenDuck Pro X') 'Production secondary QA is scoped to the explicit E: project'
        $installedProfile = Get-SafeAbsolutePath (Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Ableton\Live 12.3b10\Preferences')
        Assert-True ($preferences -ieq $installedProfile) 'Only the current Windows user exact Live 12.3b10 Preferences profile is allowed'
    }
    Assert-True ([IO.Directory]::Exists($project) -and [IO.Directory]::Exists($preferences)) 'Explicit project and beta Preferences directory must exist'
    Assert-True (-not $preferences.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -and -not $project.StartsWith($preferences+'\',[StringComparison]::OrdinalIgnoreCase)) 'Preference/project boundaries must be separate'
    $primaryRoot = Get-SafeAbsolutePath (Join-Path $project 'work\Ableton-QA-v0.3.0') $project
    Assert-True ($primaryPath -ieq (Join-Path $primaryRoot 'baseline.json')) 'The canonical primary baseline is required'
    $primaryDigestPath = Get-SafeAbsolutePath ($primaryPath+'.sha256') $primaryRoot
    $primaryHash = Get-Hash $primaryPath
    $primaryDigestHash = Get-Hash $primaryDigestPath
    Assert-True ($primaryHash -ceq [IO.File]::ReadAllText($primaryDigestPath).Trim()) 'Primary baseline digest mismatch'
    $primary = [IO.File]::ReadAllText($primaryPath) | ConvertFrom-Json
    Assert-True ($primary.version -ceq '0.3.0' -and $primary.baselineComplete -is [bool] -and $primary.baselineComplete) 'Incomplete/wrong primary baseline'
    Assert-True ((Get-SafeAbsolutePath ([string]$primary.projectRoot)) -ieq $project) 'Primary project boundary mismatch'
    $primaryFixture = $primary.PSObject.Properties['fixtureOnly']
    if ($Fixture) { Assert-True ($null -ne $primaryFixture -and $primaryFixture.Value -is [bool] -and $primaryFixture.Value) 'Fixture requires an explicitly synthetic primary baseline' }
    else { Assert-True ($null -eq $primaryFixture -or ($primaryFixture.Value -is [bool] -and -not $primaryFixture.Value)) 'Fixture primary metadata cannot be used for production restoration' }
    $initialLive = @($primary.initialLiveProcesses)
    Assert-True ($primary.abletonInitiallyRunning -is [bool] -and $primary.abletonInitiallyRunning -eq ($initialLive.Count -gt 0)) 'Primary initial process state is inconsistent'
    Assert-True ($primary.allowRunningLiveBaseline -is [bool] -and ($primary.allowRunningLiveBaseline -or $initialLive.Count -eq 0)) 'Primary does not authorize preserving running Live processes'
    Assert-LiveState $initialLive

    $secondaryRoot = Get-SafeAbsolutePath (Join-Path $primaryRoot 'secondary-profile') $primaryRoot
    $metadataPrefix = if ($Fixture) { 'FIXTURE-' } else { '' }
    $baselinePath = Get-SafeAbsolutePath (Join-Path $secondaryRoot ($metadataPrefix+'baseline.json')) $secondaryRoot
    $baselineDigest = Get-SafeAbsolutePath ($baselinePath+'.sha256') $secondaryRoot
    $snapshots = Get-SafeAbsolutePath (Join-Path $secondaryRoot 'Preferences-before') $secondaryRoot

    if ($Mode -eq 'Baseline') {
        Assert-True (-not (Test-Path -LiteralPath $secondaryRoot)) 'Secondary baseline already exists and will never be overwritten'
        $settings = @()
        foreach ($name in $settingNames) { $settings += Get-FileRecord (Get-SafeAbsolutePath (Join-Path $preferences $name) $preferences) $name }
        Assert-PrimaryUnchanged
        Assert-LiveState $initialLive
        [void][IO.Directory]::CreateDirectory($snapshots)
        [void](Get-SafeAbsolutePath $snapshots $secondaryRoot)
        foreach ($item in $settings) {
            if ($item.exists) { Copy-NewVerified (Join-Path $preferences $item.name) (Get-SafeAbsolutePath (Join-Path $snapshots $item.name) $snapshots) $item.sha256 }
            Assert-SameFile (Get-FileRecord (Join-Path $preferences $item.name) $item.name) $item 'Preference changed while taking baseline'
        }
        Assert-PrimaryUnchanged
        Assert-LiveState $initialLive
        $baseline = [ordered]@{version='0.3.0'; schema=1; secondaryBaselineComplete=$true; atUTC=[DateTime]::UtcNow.ToString('O');
            fixtureOnly=[bool]$Fixture; eligibleForReleaseEvidence=(-not [bool]$Fixture); qaValidationPerformed=$false;
            projectRoot=$project; preferencesRoot=$preferences; profileVersion='12.3b10'; primaryBaseline=$primaryPath;
            primaryBaselineSHA256=$primaryHash; primaryBaselineDigestSHA256=$primaryDigestHash;
            initialLiveProcesses=@($initialLive); settings=$settings; scriptSHA256=$scriptHash;
            scope='Only four named beta preference files. No Live process, project, primary profile, User Library, Undo, log, authorization or application changed.';
            privateArtifacts='All snapshots, baselines and restore backups are private and excluded from release archives.'}
        Write-NewJson $baselinePath $baseline
        [IO.File]::WriteAllText($baselineDigest,(Get-Hash $baselinePath)+[Environment]::NewLine,$utf8)
        [pscustomobject]@{mode='Baseline'; fixtureOnly=[bool]$Fixture; eligibleForReleaseEvidence=(-not [bool]$Fixture);
            secondaryBaseline=$baselinePath; secondaryBaselineSHA256=(Get-Hash $baselinePath); primaryBaselineSHA256=$primaryHash;
            preferencesCaptured=4; betaClosed=$true; initialProcessStatePreserved=$true; qaValidationPerformed=$false} | ConvertTo-Json -Depth 8
        exit 0
    }

    Assert-True ((Get-Hash $baselinePath) -ceq [IO.File]::ReadAllText($baselineDigest).Trim()) 'Secondary baseline digest mismatch'
    $saved = [IO.File]::ReadAllText($baselinePath) | ConvertFrom-Json
    Assert-True ($saved.version -ceq '0.3.0' -and $saved.schema -eq 1 -and $saved.secondaryBaselineComplete -is [bool] -and $saved.secondaryBaselineComplete) 'Invalid secondary baseline'
    Assert-True ($saved.fixtureOnly -is [bool] -and $saved.fixtureOnly -eq [bool]$Fixture -and $saved.eligibleForReleaseEvidence -is [bool] -and $saved.eligibleForReleaseEvidence -eq (-not [bool]$Fixture)) 'Fixture mode/evidence eligibility mismatch'
    Assert-True ($saved.qaValidationPerformed -is [bool] -and -not $saved.qaValidationPerformed -and $saved.profileVersion -ceq '12.3b10') 'Secondary baseline has invalid QA/profile scope'
    foreach ($pair in @(@('projectRoot',$project),@('preferencesRoot',$preferences),@('primaryBaseline',$primaryPath))) {
        Assert-True ((Get-SafeAbsolutePath ([string]$saved.($pair[0]))) -ieq $pair[1]) 'Secondary baseline path mismatch'
    }
    Assert-True ($saved.primaryBaselineSHA256 -ceq $primaryHash -and $saved.primaryBaselineDigestSHA256 -ceq $primaryDigestHash) 'Primary baseline differs from secondary snapshot'
    Assert-LiveState @($saved.initialLiveProcesses)
    Assert-True ((@($saved.initialLiveProcesses | ForEach-Object { Get-LiveKey $_ } | Sort-Object) -join '|') -ceq (@($initialLive | ForEach-Object { Get-LiveKey $_ } | Sort-Object) -join '|')) 'Secondary initial identities disagree with primary'
    $items = @($saved.settings)
    Assert-True ($items.Count -eq 4 -and @($items | ForEach-Object { $_.name } | Select-Object -Unique).Count -eq 4) 'Exactly four unique settings are required'
    foreach ($item in $items) {
        Assert-True ($settingNames -ccontains [string]$item.name -and $item.exists -is [bool]) 'Invalid whitelist entry'
        $destination = Get-SafeAbsolutePath (Join-Path $preferences $item.name) $preferences
        $snapshot = Get-SafeAbsolutePath (Join-Path $snapshots $item.name) $snapshots
        Assert-True (-not [IO.Directory]::Exists($destination)) 'Preference destination is a directory'
        if ($item.exists) {
            Assert-True ($item.sha256 -cmatch '^[0-9a-f]{64}$' -and $item.length -is [ValueType] -and $item.length -ge 0) 'Invalid snapshot hash/length'
            Assert-True ((Get-Hash $snapshot) -ceq $item.sha256 -and (Get-Item -LiteralPath $snapshot -Force).Length -eq $item.length) 'Snapshot content changed'
            [void](Get-StoredUtcDate $item.creationTimeUtc)
            [void](Get-StoredUtcDate $item.lastWriteTimeUtc)
            Assert-True ($item.attributes -is [int] -or $item.attributes -is [long]) 'Invalid saved attributes type'
            $unsupported = [int][IO.FileAttributes]::Directory -bor [int][IO.FileAttributes]::ReparsePoint -bor [int][IO.FileAttributes]::Device
            Assert-True (([int]$item.attributes -band $unsupported) -eq 0) 'Unsafe saved attributes'
        } else {
            Assert-True ($null -eq $item.sha256 -and $null -eq $item.length -and $null -eq $item.attributes -and $null -eq $item.creationTimeUtc -and $null -eq $item.lastWriteTimeUtc) 'Absent entry contains unexpected metadata'
            Assert-True (-not (Test-Path -LiteralPath $snapshot)) 'Unexpected snapshot for absent preference'
        }
    }
    Assert-PrimaryUnchanged
    Assert-LiveState $initialLive

    # Preserve every current whitelist file before any overwrite/removal.
    $restoreId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N')
    $restoreRoot = Get-SafeAbsolutePath (Join-Path $secondaryRoot ('Restore-snapshots\'+$restoreId)) $secondaryRoot
    [void][IO.Directory]::CreateDirectory($restoreRoot)
    [void](Get-SafeAbsolutePath $restoreRoot $secondaryRoot)
    $generated = @()
    foreach ($name in $settingNames) {
        $path = Get-SafeAbsolutePath (Join-Path $preferences $name) $preferences
        $record = Get-FileRecord $path $name
        $generated += $record
        if ($record.exists) { Copy-NewVerified $path (Get-SafeAbsolutePath (Join-Path $restoreRoot $name) $restoreRoot) $record.sha256 }
    }
    $secondaryHash = Get-Hash $baselinePath
    Write-NewJson (Join-Path $restoreRoot ($metadataPrefix+'before-restore.json')) ([ordered]@{version='0.3.0'; fixtureOnly=[bool]$Fixture;
        atUTC=[DateTime]::UtcNow.ToString('O'); primaryBaselineSHA256=$primaryHash; secondaryBaselineSHA256=$secondaryHash; settings=$generated})
    $results = @()
    foreach ($item in $items) {
        Assert-PrimaryUnchanged
        Assert-LiveState $initialLive
        $destination = Get-SafeAbsolutePath (Join-Path $preferences $item.name) $preferences
        $snapshot = Get-SafeAbsolutePath (Join-Path $snapshots $item.name) $snapshots
        $previous = @($generated | Where-Object { $_.name -ceq $item.name })[0]
        Assert-SameFile (Get-FileRecord $destination $item.name) $previous 'Preference changed since its reversible pre-restore backup'
        if ($item.exists) {
            Assert-True ((Get-Hash $snapshot) -ceq $item.sha256) 'Snapshot changed before restoration'
            if ([IO.File]::Exists($destination)) { [IO.File]::SetAttributes($destination,[IO.FileAttributes]::Normal) }
            [IO.File]::Copy($snapshot,$destination,$true)
            [IO.File]::SetCreationTimeUtc($destination,(Get-StoredUtcDate $item.creationTimeUtc))
            [IO.File]::SetLastWriteTimeUtc($destination,(Get-StoredUtcDate $item.lastWriteTimeUtc))
            [IO.File]::SetAttributes($destination,[IO.FileAttributes][int]$item.attributes)
        } elseif ([IO.File]::Exists($destination)) { Remove-Item -LiteralPath $destination -Force }
        $restored = Get-FileRecord $destination $item.name
        Assert-SameFile $restored $item ('Restored preference bytes/metadata/absence did not match: '+$item.name)
        $results += [ordered]@{name=$item.name; initiallyExisted=$item.exists; restoredExactly=$true; metadataRestored=$true; sha256=$restored.sha256}
    }
    Assert-PrimaryUnchanged
    Assert-LiveState $initialLive
    $report = [ordered]@{version='0.3.0'; schema=1; fixtureOnly=[bool]$Fixture; eligibleForReleaseEvidence=(-not [bool]$Fixture);
        atUTC=[DateTime]::UtcNow.ToString('O'); restoreComplete=$true; secondaryPreferencesRestoredExactly=$true;
        qaValidationPerformed=$false; profileVersion='12.3b10'; preferencesRoot=$preferences; secondaryBaselineSHA256=$secondaryHash;
        primaryBaseline=$primaryPath; primaryBaselineSHA256=$primaryHash; primaryBaselineUnchanged=$true;
        generatedFilesBackup=$restoreRoot; settings=$results; betaClosed=$true; initialProcessStatePreserved=$true;
        noAdditionalLiveProcesses=$true; initialLiveProcesses=@($initialLive); scriptSHA256=$scriptHash;
        scope='Only four beta preference files restored. No process stopped; no primary preference, Set, User Library, Undo, log, authorization or application changed.'}
    Write-NewJson (Join-Path $restoreRoot ($metadataPrefix+'restore-report.json')) $report
    $report | ConvertTo-Json -Depth 10
    exit 0
} catch {
    [Console]::Error.WriteLine('Secondary Ableton file-only rollback failed: '+$_.Exception.Message)
    exit 1
}
