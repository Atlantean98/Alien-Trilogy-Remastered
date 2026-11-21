param([string]$Level='GameData/CD/SECT11/L111LEV.MAP')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot 'build_playable.ps1')
    if($LASTEXITCODE -ne 0){throw 'Exploration build failed.'}
    $levelPath=(Resolve-Path -LiteralPath $Level).Path
    $executable=Join-Path $projectRoot 'decompilation/cpp/build/recovered_world_explorer.exe'
    # Opens the interactive game window.
    Start-Process -FilePath $executable -ArgumentList @('--level',('"'+$levelPath+'"')) -WorkingDirectory $projectRoot
} finally {Pop-Location}
