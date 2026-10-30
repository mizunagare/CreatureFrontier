import unreal


MAP_PATH = "/Game/Maps/L_Prototype"


def spawn(actor_class, location, rotation=None):
    return unreal.EditorLevelLibrary.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*location),
        rotation or unreal.Rotator(),
    )


unreal.EditorLevelLibrary.new_level(MAP_PATH)

cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

floor = spawn(unreal.StaticMeshActor, (0, 0, -100))
floor.static_mesh_component.set_static_mesh(cube)
floor.set_actor_scale3d(unreal.Vector(40, 40, 1))
floor.set_actor_label("原型地面")

for index, location in enumerate(((800, 500, 100), (1000, -400, 100), (1600, 100, 100))):
    obstacle = spawn(unreal.StaticMeshActor, location)
    obstacle.static_mesh_component.set_static_mesh(cube)
    obstacle.set_actor_scale3d(unreal.Vector(1.5, 1.5, 2.0))
    obstacle.set_actor_label(f"掩体方块 {index + 1}")

player_start = spawn(unreal.PlayerStart, (0, 0, 120), unreal.Rotator(0, 0, 0))
player_start.set_actor_label("玩家出生点")

sun = spawn(unreal.DirectionalLight, (0, 0, 1000), unreal.Rotator(-45, -35, 0))
sun.set_actor_label("太阳光")
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property("intensity", 30000.0)
sun.light_component.set_editor_property("atmosphere_sun_light", True)

sky = spawn(unreal.SkyLight, (0, 0, 500))
sky.set_actor_label("天空光")
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property("intensity", 1.0)
sky.light_component.set_editor_property("real_time_capture", True)
spawn(unreal.SkyAtmosphere, (0, 0, 0))
fog = spawn(unreal.ExponentialHeightFog, (0, 0, -50))
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_density", 0.008)

for index, location in enumerate(((700, 0, 90), (1250, 450, 90), (1500, -550, 90))):
    creature = spawn(unreal.CreatureCharacter, location)
    creature.set_actor_label(f"野生生物 {index + 1}")
    creature.set_editor_property("aggro_range", 650.0)

unreal.EditorLevelLibrary.save_all_dirty_levels()
unreal.log(f"幻兽边境原型地图已创建：{MAP_PATH}")

