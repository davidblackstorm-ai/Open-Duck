[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$bundle=Join-Path $projectRoot 'build\OpenDuckProX_artefacts\Release\VST3\OpenDuck Pro X.vst3'
$binary=Join-Path $bundle 'Contents\x86_64-win\OpenDuck Pro X.vst3'
$evidenceRoot=Join-Path $projectRoot 'work\revision041'
$receipt=Join-Path $evidenceRoot 'validator-verification.json'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$builtHash=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
$started=[DateTime]::UtcNow.ToString('O')
[ordered]@{version='0.4.1';complete=$false;builtSHA256=$builtHash;startedUTC=$started} | ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding utf8
$records=[Collections.Generic.List[object]]::new()
foreach($spec in @(
    @{name='steinberg';exe=(Join-Path $projectRoot 'build\validator\bin\Release\validator.exe');args=('"'+$bundle+'"')},
    @{name='pluginval';exe=(Join-Path $projectRoot 'tools\pluginval\pluginval.exe');args=('--headless --strictness-level 10 --validate "'+$bundle+'"')}
)) {
    $out=Join-Path $evidenceRoot ('release-'+$spec.name+$(if($spec.name -eq 'steinberg'){'.log'}else{'.stdout.log'}))
    $err=Join-Path $evidenceRoot ('release-'+$spec.name+'.stderr.log')
    $toolHash=(Get-FileHash -LiteralPath $spec.exe -Algorithm SHA256).Hash
    $job=Start-Process -FilePath $spec.exe -ArgumentList $spec.args -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    if($job.ExitCode -ne 0){throw ($spec.name+' exited '+$job.ExitCode+'; see '+$out)}
    $body=Get-Content -LiteralPath $out -Raw
    if($spec.name -eq 'steinberg'){
        if($body -notmatch '(?m)\b[1-9][0-9]* tests passed' -or $body -notmatch '\b0 tests failed'){throw 'Steinberg did not report a positive successful result'}
    } elseif($body -notmatch 'Strictness level: 10' -or !$body.TrimEnd().EndsWith('SUCCESS')){throw 'Pluginval strictness10 did not report SUCCESS'}
    if((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $builtHash){throw 'VST3 changed during validation'}
    if((Get-FileHash -LiteralPath $spec.exe -Algorithm SHA256).Hash -ne $toolHash){throw 'Validator changed during execution'}
    $records.Add([ordered]@{name=$spec.name;exitCode=$job.ExitCode;tool=$spec.exe;toolSHA256=$toolHash;log=$out;logSHA256=(Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash;stderr=$err;stderrSHA256=(Get-FileHash -LiteralPath $err -Algorithm SHA256).Hash})
    Write-Output ('PASS '+$spec.name)
}
[ordered]@{version='0.4.1';complete=$true;builtSHA256=$builtHash;startedUTC=$started;finishedUTC=[DateTime]::UtcNow.ToString('O');checks=@($records)} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding utf8
