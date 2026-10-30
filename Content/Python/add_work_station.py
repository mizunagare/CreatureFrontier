import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
existing = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.WorkStation)]
if not existing:
    station = actors.spawn_actor_from_class(unreal.WorkStation, unreal.Vector(-450, 0, 0))
    station.set_actor_label('基地采集工作台')
assert levels.save_current_level()
unreal.log('WORK_STATION_MAP_OK')
