# Read-only evidence helpers. Dot-sourcing this file performs no installation,
# registry mutation, process control, or preference access.
Set-StrictMode -Version Latest

function Assert-InstallerPath([string]$Candidate, [string]$Boundary = '') {
    if ($Candidate -notmatch '^[A-Za-z]:[\\/]' -or $Candidate.Substring(2) -match ':' -or
        $Candidate -match '(^|[\\/])\.{1,2}([\\/]|$)') { throw 'Expected an absolute local path without traversal or alternate streams' }
    $path = [IO.Path]::GetFullPath($Candidate).TrimEnd('\','/')
    if ($path.Length -le 3) { throw 'Drive roots are forbidden' }
    if ($Boundary -and $path -ine $Boundary -and -not $path.StartsWith($Boundary.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Installer evidence path escapes its boundary' }
    $walk = $path
    while ($walk) {
        $item = Get-Item -LiteralPath $walk -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse path forbidden: $walk" }
        $parent = [IO.Path]::GetDirectoryName($walk)
        if (-not $parent -or $parent -eq $walk) { break }
        $walk = $parent
    }
    return $path
}

function Get-InstallerFileHash([string]$Path) {
    [void](Assert-InstallerPath $Path)
    if (-not [IO.File]::Exists($Path)) { throw "Installer evidence file missing: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-InstallerCanonicalJson($Value) {
    return ConvertTo-Json -InputObject $Value -Depth 32 -Compress
}

function Get-InstallerValueHash($Value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-InstallerCanonicalJson $Value))
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Get-InstallerTreeSnapshot([string]$Root) {
    $rootPath = Assert-InstallerPath $Root
    if ([IO.File]::Exists($rootPath)) { throw 'Expected a directory or an initially absent product root' }
    $directories = [Collections.Generic.List[string]]::new()
    $files = [Collections.Generic.List[object]]::new()
    $exists = [IO.Directory]::Exists($rootPath)
    if ($exists) {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($rootPath)
        while ($pending.Count) {
            $current = $pending.Pop()
            foreach ($item in @(Get-ChildItem -LiteralPath $current -Force)) {
                $path = Assert-InstallerPath $item.FullName $rootPath
                $relative = $path.Substring($rootPath.Length+1).Replace('\','/')
                if ($item.PSIsContainer) { $directories.Add($relative); $pending.Push($path) }
                else {
                    if ($files.Count -ge 10000) { throw 'Unexpectedly large product tree' }
                    $files.Add([ordered]@{relative=$relative; length=[long]$item.Length; sha256=(Get-InstallerFileHash $path)})
                }
            }
        }
    }
    return [ordered]@{root=$rootPath; exists=$exists;
        directories=@($directories | Sort-Object); files=@($files | Sort-Object { $_.relative })}
}

function Assert-InstallerTreeEqual($Before, $After, [string]$Label) {
    if ((ConvertTo-InstallerCanonicalJson $Before) -cne (ConvertTo-InstallerCanonicalJson $After)) { throw "$Label changed: roots/directories/file sets/lengths/hashes differ" }
}

function Get-InstallerRegistrySnapshot([string]$Path) {
    # Only the exact named application uninstall key is queried by the caller.
    # Unexpanded string values and registry kinds are retained, including binary
    # and multi-string values. Tests replace the provider with fake key objects.
    if ($Path -notmatch '^HK(?:LM|CU):\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\\{[0-9A-Fa-f-]{36}\}_is1$') { throw 'Only an explicit application uninstall key may be snapshotted' }
    $exists = Test-Path -LiteralPath $Path
    $keys = [Collections.Generic.List[object]]::new()
    if ($exists) {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push('')
        while ($pending.Count) {
            $relative = $pending.Pop()
            $keyPath = if ($relative) { $Path+'\'+$relative } else { $Path }
            $key = Get-Item -LiteralPath $keyPath -ErrorAction Stop
            $values = [Collections.Generic.List[object]]::new()
            foreach ($name in @($key.GetValueNames() | Sort-Object)) {
                $kind = $key.GetValueKind($name).ToString()
                $raw = $key.GetValue($name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                $data = switch ($kind) {
                    'Binary' { [Convert]::ToBase64String([byte[]]$raw); break }
                    'None' { [Convert]::ToBase64String([byte[]]$raw); break }
                    'MultiString' { ,@([string[]]$raw); break }
                    'DWord' { ([int]$raw).ToString([Globalization.CultureInfo]::InvariantCulture); break }
                    'QWord' { ([long]$raw).ToString([Globalization.CultureInfo]::InvariantCulture); break }
                    'String' { [string]$raw; break }
                    'ExpandString' { [string]$raw; break }
                    default { throw "Unsupported registry kind: $kind" }
                }
                $values.Add([ordered]@{name=$name; kind=$kind; data=$data})
            }
            $keys.Add([ordered]@{relative=$relative; values=@($values)})
            foreach ($child in @($key.GetSubKeyNames())) {
                if ($child -match '[\\/]' -or -not $child) { throw 'Invalid uninstall subkey name' }
                $pending.Push($(if ($relative) { $relative+'\'+$child } else { $child }))
            }
            if ($keys.Count -gt 1000) { throw 'Unexpectedly large uninstall metadata tree' }
        }
    }
    return [ordered]@{path=$Path; exists=[bool]$exists; keys=@($keys | Sort-Object { $_.relative })}
}

function Get-InstallerInputManifest([string]$ProjectRoot) {
    $root = Assert-InstallerPath $ProjectRoot
    $files = [Collections.Generic.List[object]]::new()
    # These are precisely the project files read by OpenDuck.iss. Draft images,
    # old reports and unused brand assets are not compiler inputs.
    foreach ($relative in @('installer/OpenDuck.iss','installer/LICENSE.txt','installer/RELEASE-NOTES.txt',
            'installer/WizardPanel.bmp','installer/WizardSmall.bmp','installer/OriginalTechno.wav')) {
        $path = Assert-InstallerPath (Join-Path $root $relative) $root
        $files.Add([ordered]@{relative=$relative; length=(Get-Item -LiteralPath $path -Force -ErrorAction Stop).Length; sha256=(Get-InstallerFileHash $path)})
    }
    $bundleRelative = 'build/OpenDuckProX_artefacts/Release/VST3/OpenDuck Pro X.vst3/Contents'
    $payload = Get-InstallerTreeSnapshot (Join-Path $root $bundleRelative)
    if (-not $payload.exists -or $payload.files.Count -eq 0) { throw 'The Release VST3 payload is missing/empty' }
    foreach ($file in $payload.files) {
        $files.Add([ordered]@{relative=$bundleRelative+'/'+$file.relative; length=$file.length; sha256=$file.sha256})
    }
    $entries = @($files | Sort-Object { $_.relative })
    return [ordered]@{schema='openduck-installer-inputs-v1'; version='0.3.0'; files=$entries;
        payloadDirectories=@($payload.directories); inputSHA256=(Get-InstallerValueHash ([ordered]@{files=$entries; payloadDirectories=@($payload.directories)}))}
}

function Assert-InstallerInputsEqual($Before, $After) {
    if ((ConvertTo-InstallerCanonicalJson $Before) -cne (ConvertTo-InstallerCanonicalJson $After)) { throw 'Installer inputs changed during compilation/QA' }
}

function Get-InstallerPayloadManifest([string]$Contents) {
    $snapshot = Get-InstallerTreeSnapshot $Contents
    if (-not $snapshot.exists -or $snapshot.files.Count -eq 0) { throw 'Managed VST3 payload is empty' }
    return [ordered]@{directories=@($snapshot.directories); files=@($snapshot.files)}
}

function Assert-InstallerPayloadMatches($Expected, [string]$InstalledContents, [string]$Label) {
    $actual = Get-InstallerPayloadManifest $InstalledContents
    if ((ConvertTo-InstallerCanonicalJson $Expected) -cne (ConvertTo-InstallerCanonicalJson $actual)) { throw "$Label VST3 payload differs: file/directory sets, sizes or hashes" }
    return $actual
}

function Archive-InstallerAttempt([string]$ProjectRoot, [string]$Kind, [string[]]$RelativeFiles) {
    if ($Kind -notin @('qa','production')) { throw 'Invalid installer attempt type' }
    $root = Assert-InstallerPath $ProjectRoot
    $id = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N')
    $archive = Assert-InstallerPath (Join-Path $root ('work\v0.3.0\installer-'+$Kind+'-attempts\'+$id)) $root
    $records = @()
    foreach ($relative in $RelativeFiles) {
        if ($relative -match '(^|[\\/])\.{1,2}([\\/]|$)' -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) { throw 'Invalid prior evidence relative path' }
        $source = Assert-InstallerPath (Join-Path $root $relative) $root
        if ([IO.Directory]::Exists($source)) { throw 'Prior evidence must be a regular file or absent' }
        if ([IO.File]::Exists($source)) { $records += [ordered]@{relative=$relative; source=$source; sha256=(Get-InstallerFileHash $source)} }
    }
    [void][IO.Directory]::CreateDirectory($archive)
    foreach ($record in $records) {
        $destination = Assert-InstallerPath (Join-Path $archive $record.relative) $archive
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        if ((Get-InstallerFileHash $record.source) -cne $record.sha256) { throw 'Prior evidence changed before archive' }
        [IO.File]::Move($record.source,$destination)
        if ((Get-InstallerFileHash $destination) -cne $record.sha256) { throw 'Prior evidence archive failed hash verification' }
    }
    return [ordered]@{id=$id; archive=$archive; previousFiles=@($records | ForEach-Object { [ordered]@{relative=$_.relative; sha256=$_.sha256} })}
}
