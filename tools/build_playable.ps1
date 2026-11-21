$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    # Retain any loaded image while publishing a fresh executable at the
    # launcher path. Windows can keep a tested/running image write-locked.
    $executable=Join-Path $projectRoot 'decompilation/cpp/build/recovered_world_explorer.exe'
    $previous=$null
    if(Test-Path -LiteralPath $executable){
        $previous=Join-Path $projectRoot ('decompilation/cpp/build/world_previous_'+[guid]::NewGuid().ToString('N')+'.exe')
        Move-Item -LiteralPath $executable -Destination $previous
    }
    & cmake --build decompilation/cpp/build --target recovered_level_explorer
    if($LASTEXITCODE -ne 0){
        if($previous){Copy-Item -LiteralPath $previous -Destination $executable -Force}
        throw 'Playable world build failed.'
    }
} finally {Pop-Location}
