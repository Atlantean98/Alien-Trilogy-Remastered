$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot 'build_playable.ps1')
    if($LASTEXITCODE -ne 0){throw 'Playable enemy build failed.'}
    $source=Join-Path $projectRoot 'decompilation/cpp/build/recovered_world_explorer.exe'
    $executable=Join-Path $projectRoot ('decompilation/cpp/build/world_check_'+[guid]::NewGuid().ToString('N')+'.exe')
    Copy-Item -LiteralPath $source -Destination $executable
    foreach($check in @(@('--self-test','exploration_checks.json'),@('--enemy-self-test','enemy_playable_checks.json'),@('--all-levels-self-test','all_levels_playable_checks.json'),@('--door-panel-self-test','door_panel_checks.json'),@('--day-one-self-test','day_one_checks.json'),@('--alarm-self-test','alarm_lighting_checks.json'),@('--pickup-self-test','pickup_playable_checks.json'),@('--weapon-switch-self-test','weapon_switch_checks.json'),@('--weapon-audio-self-test','weapon_audio_checks.json'),@('--mission-object-self-test','mission_object_checks.json'),@('--menu-self-test','menu_checks.json'),@('--end-level-self-test','end_level_checks.json'),@('--ps1-self-test','ps1_playable_checks.json'),@('--controls-world-self-test','controls_world_checks.json'),@('--environment-self-test','environment_checks.json'),@('--bullet-trail-self-test','bullet_trail_checks.json'),@('--flame-self-test','flame_checks.json'),@('--secret-enemy-self-test','secret_enemy_checks.json'),@('--secret-entrance-self-test','secret_entrance_checks.json'),@('--all-entrance-self-test','all_entrance_checks.json'),@('--pod-self-test','pod_checks.json'),@('--mission-threshold-self-test','mission_threshold_checks.json'),@('--password-self-test','password_checks.json'),@('--late-colony-self-test','late_colony_checks.json'),@('--egg-self-test','egg_checks.json'))){
        $started=[DateTime]::UtcNow
        $process=Start-Process -FilePath $executable -ArgumentList $check[0] -WindowStyle Hidden -PassThru -Wait
        $exitCode=$process.ExitCode
        $process.Dispose()
        if($exitCode -ne 0){
            $errorFile=Join-Path $projectRoot 'decompilation/reports/exploration-error.txt'
            if(Test-Path -LiteralPath $errorFile){Get-Content -LiteralPath $errorFile}
            throw "Playable check failed: $($check[0])"
        }
        $report=Get-Item -LiteralPath (Join-Path $projectRoot "decompilation/reports/$($check[1])")
        if($report.LastWriteTimeUtc -lt $started){throw 'Playable check did not write a fresh report.'}
        $result=Get-Content -LiteralPath $report.FullName -Raw | ConvertFrom-Json
        if($result.status -ne 'passed'){throw 'Playable check report failed.'}
        Write-Output "Passed: $($check[0])"
    }
} finally {Pop-Location}
