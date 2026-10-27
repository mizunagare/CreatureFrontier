import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
palette_path = '/Game/Materials/M_CreaturePalette'
palette = unreal.load_asset(palette_path) if editor_assets.does_asset_exist(palette_path) else None
if not palette:
    palette = tools.create_asset('M_CreaturePalette', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    tint = unreal.MaterialEditingLibrary.create_material_expression(palette, unreal.MaterialExpressionVectorParameter, -240, 0)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(0.2, 0.6, 0.3, 1))
    unreal.MaterialEditingLibrary.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(palette, unreal.MaterialExpressionConstant, -240, 180)
    roughness.set_editor_property('r', 0.65)
    unreal.MaterialEditingLibrary.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(palette)
    assert editor_assets.save_loaded_asset(palette)

specs = [
    dict(asset='DA_Mosskip', name='Mosskip', form=unreal.CreatureForm.LEAF,
         description='A springy leaf-eared grazer. Its patient paws excel at gathering.',
         health=90, attack=9, rate=0.45, speed=360.0, seconds=1.1,
         color=(0.12, 0.48, 0.22), accent=(0.42, 0.75, 0.08), job=unreal.WorkSpecialty.GATHERING, work=1.8),
    dict(asset='DA_Cinderkit', name='Cinderkit', form=unreal.CreatureForm.EMBER,
         description='A bright-backed frill creature that uses its warm tail to help craft tools.',
         health=80, attack=13, rate=0.30, speed=340.0, seconds=0.85,
         color=(0.72, 0.16, 0.045), accent=(1.0, 0.56, 0.08), job=unreal.WorkSpecialty.CRAFTING, work=1.5),
    dict(asset='DA_Craglet', name='Craglet', form=unreal.CreatureForm.STONE,
         description='A sturdy crystal-horned burrower. Slow strides hide powerful mining strikes.',
         health=160, attack=18, rate=0.22, speed=220.0, seconds=1.8,
         color=(0.17, 0.24, 0.38), accent=(0.24, 0.64, 0.88), job=unreal.WorkSpecialty.MINING, work=2.0),
]

definitions = []
for spec in specs:
    asset_path = '/Game/Creatures/' + spec['asset']
    asset = unreal.load_asset(asset_path) if editor_assets.does_asset_exist(asset_path) else None
    if not asset:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.CreatureDefinition)
        asset = tools.create_asset(spec['asset'], '/Game/Creatures', unreal.CreatureDefinition, factory)
    assert asset
    values = dict(display_name=spec['name'], description=spec['description'], form=spec['form'],
                  base_max_health=spec['health'], base_attack=spec['attack'], base_capture_rate=spec['rate'],
                  move_speed=spec['speed'], attack_seconds=spec['seconds'], work_speed=spec['work'],
                  work_specialties=[spec['job']], palette_material=palette,
                  body_color=unreal.LinearColor(*spec['color'], 1), accent_color=unreal.LinearColor(*spec['accent'], 1))
    for key, value in values.items():
        asset.set_editor_property(key, value)
    assert editor_assets.save_loaded_asset(asset)
    definitions.append(asset)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
creatures = sorted([a for a in actors.get_all_level_actors() if isinstance(a, unreal.CreatureCharacter)], key=lambda a: a.get_actor_label())
assert len(creatures) >= 3
for creature, definition in zip(creatures, definitions):
    creature.set_editor_property('definition', definition)
    creature.set_actor_rotation(unreal.Rotator(0, 180, 0), False)

for job, position in [(unreal.WorkSpecialty.MINING, (-650, -350, 0)), (unreal.WorkSpecialty.CRAFTING, (-650, 350, 0))]:
    if not any(isinstance(a, unreal.WorkStation) and a.get_editor_property('required_specialty') == job for a in actors.get_all_level_actors()):
        station = actors.spawn_actor_from_class(unreal.WorkStation, unreal.Vector(*position))
        station.set_editor_property('required_specialty', job)
        station.set_actor_label(str(job) + ' Work Station')

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.FrontierEditorLibrary.prepare_prototype_navigation(world)
assert levels.save_current_level()
unreal.log('CF_SPECIES_ASSETS_OK: 3 definitions and specialty work stations')
