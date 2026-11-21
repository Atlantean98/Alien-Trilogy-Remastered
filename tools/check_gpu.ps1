$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
 & (Join-Path $PSScriptRoot 'build_playable.ps1')
 $executable=Join-Path $projectRoot ('decompilation/cpp/build/gpu_check_'+[guid]::NewGuid().ToString('N')+'.exe')
 Copy-Item -LiteralPath 'decompilation/cpp/build/recovered_world_explorer.exe' -Destination $executable
 foreach($backend in @('dx11','dx12','vulkan')){
  $started=[DateTime]::UtcNow
  $process=Start-Process -FilePath $executable -ArgumentList "--gpu-self-test --renderer $backend" -WindowStyle Hidden -PassThru -Wait
  if($process.ExitCode -ne 0){Get-Content 'decompilation/reports/exploration-error.txt' -ErrorAction SilentlyContinue;throw "GPU check failed: $backend"}
  $report=Get-Item "decompilation/reports/gpu_${backend}_checks.json"
  if($report.LastWriteTimeUtc -lt $started){throw 'GPU check did not write a fresh report'}
  $result=Get-Content $report.FullName -Raw | ConvertFrom-Json
  if($result.status -ne 'passed'){throw "GPU check failed: $backend"}
  Write-Output ("Passed {0}: {1:N2} ms average, {2:N2} ms p95" -f $backend,$result.average_ms,$result.p95_ms)
 }
} finally {Pop-Location}
