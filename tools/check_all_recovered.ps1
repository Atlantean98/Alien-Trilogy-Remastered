$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & cmake --build decompilation/cpp/build --target behavior_probe recovered_checks
    if ($LASTEXITCODE -ne 0) { throw 'Recovered C++ build failed.' }
    & ./decompilation/cpp/build/recovered_checks.exe
    if ($LASTEXITCODE -ne 0) { throw 'Utility checks failed.' }
    foreach ($script in @('check_image.py','check_scheduler_behavior.py','check_player_behavior.py','check_world_movement.py','check_player_states.py','check_player_control.py','check_weapon_runtime.py','check_projectile_runtime.py','check_projectile_simulation.py','check_combat_runtime.py','check_player_damage.py','check_drop_rendering.py','check_pickup_runtime.py','check_pickup_scene.py','check_software_rendering.py','check_palette_rendering.py','check_software_frame.py','check_render_commands.py','check_polygon_rasterizer.py','check_shader_spans.py','check_level_loading.py','check_enemy_runtime.py','check_enemy_construction.py','check_enemy_integration.py','check_enemy_ai.py','check_enemy_task.py','check_enemy_tick_integration.py','check_enemy_movement.py','check_enemy_moving_ticks.py')) {
        $result=& python (Join-Path $PSScriptRoot $script) 2>&1
        if ($LASTEXITCODE -ne 0) {$result | Write-Output; throw "Failed: $script"}
        Write-Output "Passed: $script"
    }
    & python (Join-Path $PSScriptRoot 'report_progress.py')
    if ($LASTEXITCODE -ne 0) { throw 'Progress report failed.' }
} finally { Pop-Location }
