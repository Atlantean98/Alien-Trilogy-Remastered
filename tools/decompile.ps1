$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vendor = Join-Path $PSScriptRoot 'vendor'
$env:JAVA_HOME = (Get-ChildItem -LiteralPath $vendor -Directory -Filter 'jdk-*' | Select-Object -First 1).FullName
if (-not $env:JAVA_HOME) { throw 'Run python tools/setup_decompiler.py first.' }
$ghidra = Join-Path $vendor 'ghidra_12.2_PUBLIC/support/analyzeHeadless.bat'
$research = Join-Path $projectRoot 'decompilation'
$env:XDG_CONFIG_HOME = Join-Path $research 'tool-settings'
$env:XDG_CACHE_HOME = Join-Path $research 'tool-cache'
$image = Join-Path $research 'image'
$projects = Join-Path $research 'projects'
$export = Join-Path $research 'raw'
New-Item -ItemType Directory -Force -Path $projects,$export,$env:XDG_CONFIG_HOME,$env:XDG_CACHE_HOME | Out-Null
& python (Join-Path $PSScriptRoot 'prepare_image.py')
if ($LASTEXITCODE -ne 0) { throw 'LE preparation failed.' }
$manifest = Get-Content -LiteralPath (Join-Path $image 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.sha256 -ne '588606092836c4a0b9e31bd89a885963cc6edc908e60789378ba23e3f109e666') {
    throw 'This analysis configuration is pinned to the supplied DOS executable. Review the memory map before analysing another version.'
}
& $ghidra $projects AlienTrilogy -import (Join-Path $image 'object1.bin') -overwrite -loader BinaryLoader -processor 'x86:LE:32:default' -cspec gcc -loader-baseAddr 0x10000 -scriptPath (Join-Path $PSScriptRoot 'ghidra') -preScript PrepareTrilogy.java $image -postScript ExportTrilogy.java $export -postScript AuditTrilogy.java $export -analysisTimeoutPerFile 900 -log (Join-Path $research 'analysis.log') -scriptlog (Join-Path $research 'scripts.log')
if ($LASTEXITCODE -ne 0) { throw 'Ghidra analysis/export failed; inspect decompilation/analysis.log.' }
& (Join-Path $PSScriptRoot 'refine.ps1')
