$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$research = Join-Path $projectRoot 'decompilation'
$vendor = Join-Path $PSScriptRoot 'vendor'
$env:JAVA_HOME = (Get-ChildItem -LiteralPath $vendor -Directory -Filter 'jdk-*' | Select-Object -First 1).FullName
$env:XDG_CONFIG_HOME = Join-Path $research 'tool-settings'
$env:XDG_CACHE_HOME = Join-Path $research 'tool-cache'
$refined = Join-Path $research 'refined'
New-Item -ItemType Directory -Force -Path $refined | Out-Null
& python (Join-Path $PSScriptRoot 'index_task_callbacks.py')
if ($LASTEXITCODE -ne 0) { throw 'Task registration indexing failed.' }
Set-Content -LiteralPath (Join-Path $research 'refinement.log') -Value ''
& (Join-Path $vendor 'ghidra_12.2_PUBLIC/support/analyzeHeadless.bat') (Join-Path $research 'projects') AlienTrilogy -process object1.bin -scriptPath (Join-Path $PSScriptRoot 'ghidra') -preScript DiscoverTaskCallbacks.java (Join-Path $research 'reports') -preScript DiscoverPlayerStates.java (Join-Path $research 'reports') -preScript RepairPlayerBoundaries.java (Join-Path $research 'reports') -postScript ApplyResearch.java -postScript ExportTrilogy.java $refined -postScript AuditTrilogy.java $refined -analysisTimeoutPerFile 900 -log (Join-Path $research 'refinement.log')
if ($LASTEXITCODE -ne 0) { throw 'Refinement failed; inspect decompilation/refinement.log.' }
# Headless Ghidra may return success even when a script fails; inspect this run's log.
if (Select-String -LiteralPath (Join-Path $research 'refinement.log') -Pattern 'REPORT SCRIPT ERROR|error:|ERROR.*ApplyResearch|ERROR.*RepairPlayer' -Quiet) {
    throw 'A research script failed; inspect decompilation/refinement.log.'
}
