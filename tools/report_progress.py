"""Combine distinct comparison groups, excluding repeated imported base checks."""
import hashlib
import json
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
reports=ROOT/'decompilation/reports'
files=['behavior_checks.json','scheduler_checks.json','player_checks.json','world_movement_checks.json','player_states_checks.json','player_control_checks.json','weapon_runtime_checks.json','projectile_runtime_checks.json','projectile_simulation_checks.json','combat_runtime_checks.json','player_damage_checks.json','drop_rendering_checks.json','pickup_runtime_checks.json','pickup_scene_checks.json','software_rendering_checks.json','palette_rendering_checks.json','software_frame_checks.json','render_commands_checks.json','polygon_rasterizer_checks.json','shader_spans_checks.json','level_loading_checks.json','enemy_runtime_checks.json','enemy_construction_checks.json','enemy_integration_checks.json','enemy_ai_checks.json','enemy_task_checks.json','enemy_tick_integration_checks.json','enemy_movement_checks.json','enemy_moving_ticks_checks.json']
data=[json.loads((reports/f).read_text()) for f in files]
reference=hashlib.sha256((ROOT/'GameData/TRILOGY.EXE').read_bytes()).hexdigest()
assert all(d['status']=='passed' and d['reference_sha256']==reference for d in data)
counts={
    'utility_input_runtime':data[0]['counts']['total_cases'],
    'scheduler':data[1]['scheduler_state_comparisons'],
    'player_helpers_and_orchestration':data[2]['player_comparisons'],
    'world_movement':data[3]['new_comparisons'],
    'camera_and_player_states':data[4]['new_comparisons'],
    'input_weapon_control_and_initialization':data[5]['new_comparisons'],
    'weapon_selection_animation_and_shot_wrappers':data[6]['new_comparisons'],
    'projectile_pool_construction_map_collision_and_damage':data[7]['new_comparisons'],
    'projectile_movement_ticks_entity_and_player_hits':data[8]['new_comparisons'],
    'enemy_reactions_animation_knockback_and_area_blasts':data[9]['new_comparisons'],
    'player_damage_armor_feedback_and_status':data[10]['new_comparisons'],
    'item_drops_projection_and_projectile_render_packets':data[11]['new_comparisons'],
    'pickup_motion_collection_inventory_and_pool_reuse':data[12]['new_comparisons'],
    'level_pickup_scene_and_object_transforms':data[13]['new_comparisons'],
    'model_dispatch_ordering_and_software_pixels':data[14]['new_comparisons'],
    'palette_components_shading_and_blending':data[15]['new_comparisons'],
    'software_viewport_buffer_clear_and_frame_flow':data[16]['new_comparisons'],
    'drawing_commands_rectangles_quad_submission_and_render_math':data[17]['new_comparisons'],
    'polygon_setup_solid_and_indexed_texture_pixels_and_shaded_sprites':data[18]['new_comparisons'],
    'opaque_transparent_and_blended_shaded_polygon_pixels':data[19]['new_comparisons'],
    'original_level_layout_geometry_collision_and_real_map_height_queries':data[20]['new_comparisons'],
    'enemy_pool_population_health_turning_paths_and_detection':data[21]['new_comparisons'],
    'enemy_species_constructor':data[22]['new_comparisons'],
    'enemy_construction_scheduler_animation_and_46_level_integration':data[23]['new_comparisons'],
    'enemy_distances_spawn_triggers_unfolding_attack_selection_and_hits':data[24]['new_comparisons'],
    'enemy_main_task_state_machine':data[25]['new_comparisons'],
    'enemy_consecutive_ticks_with_native_distance_yaw_attack_animation_and_scheduler':data[26]['new_comparisons'],
    'enemy_axis_collision_and_movement_including_46_level_grids':data[27]['new_comparisons'],
    'enemy_moving_ai_core_integration':data[28]['new_comparisons'],
}
coverage=(ROOT/'decompilation/refined/coverage.txt').read_text()
functions=int(re.search(r'Functions starting in LE code object: (\d+)',coverage)[1])
batch_entries=data[17]['recovered_original_entries']
assert len(batch_entries)==len(set(batch_entries))==22
polygon_entries=data[18]['recovered_original_entries']
assert len(polygon_entries)==len(set(polygon_entries))==8
assert not set(batch_entries)&set(polygon_entries)
shader_entries=data[19]['recovered_original_entries']
assert len(shader_entries)==len(set(shader_entries))==3
assert not (set(batch_entries)|set(polygon_entries))&set(shader_entries)
level_entries=data[20]['recovered_original_entries']
assert len(level_entries)==len(set(level_entries))==7
assert not (set(batch_entries)|set(polygon_entries)|set(shader_entries))&set(level_entries)
enemy_entries=data[21]['recovered_original_entries']
assert len(enemy_entries)==len(set(enemy_entries))==10
assert not (set(batch_entries)|set(polygon_entries)|set(shader_entries)|set(level_entries))&set(enemy_entries)
new_entries=[entry for group in data[22:] for entry in group['recovered_original_entries']]
assert len(new_entries)==len(set(new_entries))==16
assert not set(new_entries)&set(enemy_entries)
recovered=len(new_entries)+132+len(batch_entries)+len(polygon_entries)+len(shader_entries)+len(level_entries)+len(enemy_entries)
result=dict(reference_sha256=reference,reconstructed_original_routine_bodies=recovered,
    identified_code_functions=functions,distinct_passing_comparisons=sum(counts.values()),
    comparison_groups=counts,
    stage='Enemy constructor, distance and attack helpers, spawn triggers and main task state machine recovered. Constructor/scheduler/original animation integration matches all 46 levels. Enemy axis collision and movement recovered and connected to the AI core; faithful original navigation remains unfinished. The default playable host now runs 19 first-level enemies with original sprites and native AI/movement/combat, using provisional navigation, display and hitscan bridges.',
    remaining=['Remaining model primitive handlers, mode-six spans and scene integration',
               'Enemy navigation/line-of-sight, special-species callbacks and full gameplay integration',
               'Map/region triggers and remaining interactions',
               'HUD, menu, audiovisual and other gameplay services',
               'Remaining weapon-specific simulation',
               'Whole-game integration and behavior checks','Modern PC features'],
    scope='Routine counts and sampled checks are not an overall completion percentage.',
    routine_count_percentage=round(recovered/functions*100,2),
    reports={f:hashlib.sha256((reports/f).read_bytes()).hexdigest() for f in files})
if (reports/'enemy_playable_checks.json').exists():
    playable=json.loads((reports/'enemy_playable_checks.json').read_text())
    assert playable['status']=='passed'
    result['playable_build']={'default_level':'L111LEV.MAP','initial_active_enemies':playable['initial_active_enemies'],'validation':'enemy_playable_checks.json','original_comparison_credit':0,'provisional':['Navigation and line of sight','Sprite display and palette integration','Hitscan firing and ranged impact bridge'],'unfinished':['Other levels/special-species callbacks','Facehugger attachment','Enemy audio','Doors/triggers/pickups and full weapon simulation']}
(reports/'progress.json').write_text(json.dumps(result,indent=2)+'\n')
print(f"{result['reconstructed_original_routine_bodies']} reconstructed original routine bodies; {result['distinct_passing_comparisons']} distinct passing comparisons.")
