import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
all_actors = actors.get_all_level_actors()
for actor in all_actors:
    if isinstance(actor, unreal.DirectionalLight):
        light = actor.get_component_by_class(unreal.DirectionalLightComponent)
        light.set_mobility(unreal.ComponentMobility.MOVABLE)
        light.set_editor_property('intensity', 30000.0)
        light.set_editor_property('atmosphere_sun_light', True)
    if isinstance(actor, unreal.SkyLight):
        light = actor.get_component_by_class(unreal.SkyLightComponent)
        light.set_mobility(unreal.ComponentMobility.MOVABLE)
        light.set_editor_property('real_time_capture', True)
    if isinstance(actor, unreal.CreatureCharacter):
        actor.set_editor_property('aggro_range', 650.0)

if not any(isinstance(a, unreal.SkyAtmosphere) for a in all_actors):
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
if not any(isinstance(a, unreal.ExponentialHeightFog) for a in all_actors):
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -50))
    fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property('fog_density', 0.008)

assert levels.save_current_level()
unreal.log('CF_LIGHTING_OK')
