import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.FrontierEditorLibrary.prepare_prototype_navigation(world)
assert levels.save_current_level()
unreal.log('CF_NAV_BUILD_OK')
