import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
specs = [
    ('MeadowGrove', '草甸林地', 'DA_Mosskip', (-1500, 1400, -50), 2, 60.0, False),
    ('CrystalRise', '水晶高地', 'DA_Craglet', (1500, 1400, -50), 5, 180.0, True),
]
for key, name, asset, location, level, seconds, boss in specs:
    matches = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.WildEncounter)
               and str(a.get_editor_property('encounter_id')) == key]
    assert len(matches) <= 1
    site = matches[0] if matches else actors.spawn_actor_from_class(unreal.WildEncounter, unreal.Vector(*location))
    site.set_actor_label(name + ' Encounter')
    site.set_editor_property('encounter_id', key)
    site.set_editor_property('region_name', name)
    site.set_editor_property('definition', unreal.load_asset('/Game/Creatures/' + asset))
    site.set_editor_property('creature_level', level)
    site.set_editor_property('respawn_seconds', seconds)
    site.set_editor_property('boss', boss)
assert levels.save_current_level()
unreal.log('CF_ENCOUNTER_ASSETS_OK: 已更新草甸林地与水晶高地首领栖息地')
