"""仅启用余烬洞窟的脱险经历来源，不重建几何、物种或资源。"""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
sources = [a for a in actors.get_all_level_actors()
           if isinstance(a, unreal.WildEncounter)
           and str(a.get_editor_property('encounter_id')) == 'EmberHollow']
assert len(sources) == 1, '余烬洞窟来源必须唯一'
sources[0].set_editor_property('records_rescue_journey', True)
assert sources[0].get_editor_property('records_rescue_journey')
assert levels.save_current_level()
unreal.log('CF_RESCUE_MEMORY_ASSETS PASS')
