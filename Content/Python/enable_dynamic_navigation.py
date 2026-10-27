import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
found = 0
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.RecastNavMesh):
        actor.set_editor_property('runtime_generation', unreal.RuntimeGenerationType.DYNAMIC)
        assert actor.get_editor_property('runtime_generation') == unreal.RuntimeGenerationType.DYNAMIC
        found += 1
assert found > 0
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.FrontierEditorLibrary.prepare_prototype_navigation(world)
assert levels.save_current_level()
unreal.log('CF_DYNAMIC_NAV_OK: dynamic runtime navigation saved')
