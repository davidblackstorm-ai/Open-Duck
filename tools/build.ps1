[CmdletBinding()]
param(
    [string]$InnoCompiler = '',
    [string]$CMakePath = '',
    [string]$PythonPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent

function Resolve-OpenDuckCMake {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "CMake non trovato nel percorso specificato: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $command = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }

    $candidates = @()
    if ($env:VSINSTALLDIR) {
        $candidates += Join-Path $env:VSINSTALLDIR 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
    foreach ($edition in @('Community', 'Professional', 'Enterprise', 'BuildTools')) {
        $candidates += Join-Path $env:ProgramFiles ("Microsoft Visual Studio\2022\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installation) {
            $candidates += Join-Path ($installation | Select-Object -First 1) 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'CMake >= 3.22 non trovato. Installare il componente CMake di Visual Studio 2022 oppure usare -CMakePath.'
}

$cmake = Resolve-OpenDuckCMake -ExplicitPath $CMakePath
Write-Host "CMake: $cmake"

if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party\JUCE\CMakeLists.txt'))) {
    $git = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$git) { throw 'JUCE mancante: occorre Git per acquisire la dipendenza fissata.' }
    $jucePath = Join-Path $projectRoot 'third_party\JUCE'
    & $git.Source clone --branch 8.0.9 --depth 1 https://github.com/juce-framework/JUCE.git $jucePath
    if ($LASTEXITCODE -ne 0) { throw 'Acquisizione JUCE fallita' }
    $commit = & $git.Source -C $jucePath rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $commit.Trim() -ne 'f72bad64d29715216226685810c5196bd0d79d77') {
        throw 'Il commit JUCE acquisito non corrisponde alla dipendenza prevista.'
    }
}

& $cmake -S $projectRoot -B (Join-Path $projectRoot 'build') -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Configurazione CMake fallita' }

& $cmake --build (Join-Path $projectRoot 'build') --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compilazione Release fallita' }

& (Join-Path $projectRoot 'build\OpenDuckTests_artefacts\Release\OpenDuckTests.exe') (Join-Path $projectRoot 'build\visual-qa-v0.3.0')
if ($LASTEXITCODE -ne 0) { throw 'Test falliti: installer non compilato' }

# Revision suites exercise DSP, native UI and real local-model integration.
foreach ($suite in @('Revision030DspTests', 'Revision030QualityTests', 'Revision030UiTests', 'Revision030AiTests', 'Revision030AssistBridgeTests')) {
    $executable = Join-Path $projectRoot ("build\$($suite)_artefacts\Release\$suite.exe")
    if (!(Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Suite obbligatoria mancante: $suite" }
    & $executable (Join-Path $projectRoot 'build\visual-qa-v0.3.0')
    if ($LASTEXITCODE -ne 0) { throw "Suite fallita: $suite; installer non compilato" }
}

# This is a build-time evidence tool only; the installed VST has no Python dependency.
if (!$PythonPath) {
    $pythonCommand = Get-Command python.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pythonCommand) { $PythonPath = $pythonCommand.Source }
}
if (!$PythonPath -or !(Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
    throw 'Per verificare i35 render serve Python3.11+ sul computer di sviluppo; specificare -PythonPath. Il VST installato non richiede Python.'
}
$qualityDirectory = Join-Path $projectRoot 'build\quality-audio-v0.3.0'
& (Join-Path $projectRoot 'build\Revision030QualityAudit_artefacts\Release\Revision030QualityAudit.exe') (Join-Path $projectRoot 'tests\fixtures\audio') $qualityDirectory
if ($LASTEXITCODE -ne 0) { throw 'Render musicali non completati: installer non compilato' }
& $PythonPath (Join-Path $projectRoot 'tools\verify_quality_audio.py') --root $projectRoot --audit-dir $qualityDirectory --output (Join-Path $projectRoot 'build\quality-audio-v0.3.0.json')
if ($LASTEXITCODE -ne 0) { throw 'Gate qualita dei35 render fallito: installer non compilato' }

$parity = Join-Path $projectRoot 'build\AssistModelParity_artefacts\Release\AssistModelParity.exe'
if (!(Test-Path -LiteralPath $parity -PathType Leaf)) { throw 'AssistModelParity mancante' }
& $parity --golden (Join-Path $projectRoot 'docs\research-v0.3.0\golden.csv') --audio-report (Join-Path $projectRoot 'build\ai-heldout-v0.3.0.json') --heldout-scenes 12
if ($LASTEXITCODE -ne 0) { throw 'Parita o held-out AI falliti: installer non compilato' }

$hostContract = Join-Path $projectRoot 'build\OpenDuckHostContract_artefacts\Release\OpenDuckHostContract.exe'
if (Test-Path -LiteralPath $hostContract -PathType Leaf) {
    & $hostContract (Join-Path $projectRoot 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3')
    if ($LASTEXITCODE -ne 0) { throw 'Contratto host VST3 fallito: installer non compilato' }
}

if ($InnoCompiler) {
    if (!(Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw "Compilatore Inno non trovato: $InnoCompiler" }
    & $InnoCompiler /Qp (Join-Path $projectRoot 'installer\OpenDuck.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Compilazione installer fallita' }
}

Write-Host 'Build e test completati. Nessuna installazione di sistema eseguita.'
