$ErrorActionPreference = 'Stop'
$projectRoot = 'E:\Codex\Lavori\OpenDuck Pro X'
$pythonPath = 'C:\Users\PC\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$localUrl = 'http://127.0.0.1:8769/'
try {
    $existingPage = Invoke-WebRequest -UseBasicParsing -Uri $localUrl -TimeoutSec 2
    if ($existingPage.StatusCode -eq 200 -and $existingPage.Content -match 'OpenDuck Pro X') {
        Write-Output ('Laboratorio già attivo: ' + $localUrl)
        exit 0
    }
} catch { }
$serverPath = Join-Path $projectRoot 'tools\dashboard_server.py'
$stdoutPath = Join-Path $projectRoot 'work\dashboard.stdout.log'
$stderrPath = Join-Path $projectRoot 'work\dashboard.stderr.log'
$serverProcess = Start-Process -FilePath $pythonPath -ArgumentList @('-u', ('"' + $serverPath + '"')) -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$serverProcess.Id | Set-Content -LiteralPath (Join-Path $projectRoot 'work\dashboard.pid') -Encoding ascii
for ($attempt = 0; $attempt -lt 15; $attempt++) {
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri $localUrl -TimeoutSec 2
        if ($response.StatusCode -eq 200 -and $response.Content -match 'OpenDuck Pro X') {
            Write-Output ('Laboratorio attivo: ' + $localUrl)
            exit 0
        }
    } catch { }
    Start-Sleep -Milliseconds 200
}
throw 'Il server locale non ha risposto; consultare i log del progetto.'
