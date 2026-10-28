"""Idempotent, original geometric environment; updates only named CF_Valley actors."""
import math
import random
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assert levels.load_level('/Game/Maps/L_Prototype')
palette = unreal.load_asset('/Game/Materials/M_CreaturePalette')
assert palette
colors = {
    'Grass': (0.16, 0.29, 0.105), 'Meadow': (0.24, 0.42, 0.12),
    'Path': (0.40, 0.29, 0.16), 'Bark': (0.19, 0.09, 0.035),
    'Leaf': (0.065, 0.24, 0.09), 'LeafLight': (0.13, 0.36, 0.12),
    'Stone': (0.16, 0.22, 0.25), 'Crystal': (0.08, 0.52, 0.72),
    'Wood': (0.33, 0.17, 0.065), 'Tarp': (0.16, 0.33, 0.39),
    'Arena': (0.24, 0.30, 0.34), 'Marker': (0.72, 0.55, 0.20),
}
materials = {}
for name, color in colors.items():
    path = '/Game/Environment/Materials/MI_' + name
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if not material:
        material = tools.create_asset('MI_' + name, '/Game/Environment/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, palette)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(material, 'Tint', unreal.LinearColor(*color, 1))
    assert assets.save_loaded_asset(material)
    materials[name] = material

meshes = {key: unreal.load_asset('/Engine/BasicShapes/' + key + '.' + key) for key in ('Cube', 'Cone', 'Cylinder', 'Sphere')}
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors() if a.get_actor_label().startswith('CF_Valley_')}
used = set()

def mesh(name, shape, location, scale, material, solid=False, yaw=0, pitch=0):
    label = 'CF_Valley_' + name
    assert label not in used
    used.add(label)
    actor = existing.get(label)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    actor.set_actor_label(label)
    actor.set_folder_path('Starter Valley')
    actor.tags = [unreal.Name('CF_Valley')]
    component = actor.static_mesh_component
    component.set_static_mesh(meshes[shape])
    component.set_material(0, materials[material])
    component.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
    actor.set_actor_location(unreal.Vector(*location), False, False)
    actor.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw, roll=0), False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor

def path(name, start, end, width=180):
    dx, dy = end[0]-start[0], end[1]-start[1]
    mesh(name, 'Cube', ((start[0]+end[0])/2, (start[1]+end[1])/2, -48.4),
         (math.hypot(dx,dy)/100, width/100, .012), 'Path', yaw=math.degrees(math.atan2(dy,dx)))

for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'Prototype Floor':
        actor.static_mesh_component.set_material(0, materials['Grass'])
    if actor.get_actor_label().startswith('Cover Block '):
        actor.static_mesh_component.set_material(0, materials['Stone'])
        position = actor.get_actor_location()
        mesh('CoverFoot_' + actor.get_actor_label()[-1], 'Cube', (position.x,position.y,-25), (1.5,1.5,.5), 'Stone')
    if isinstance(actor, unreal.WorkStation):
        actor.get_component_by_class(unreal.StaticMeshComponent).set_material(0,materials['Wood'])
    if isinstance(actor, unreal.CreatureCharacter):
        actor.set_actor_rotation(unreal.Rotator(yaw=180),False)

# 绘制式地表层不参与碰撞；原平面仍是唯一承重地面。
mesh('BaseClearing', 'Cylinder', (-450,0,-49), (18,18,.01), 'Path')
mesh('MeadowClearing', 'Cylinder', (-1500,1400,-48.9), (10,10,.012), 'Meadow')
mesh('CrystalClearing', 'Cylinder', (1500,1400,-48.8), (9,9,.012), 'Arena')
path('PathToGrove', (-450,200), (-1500,1400))
path('PathToCrystalA', (0,0), (400,900))
path('PathToCrystalB', (400,900), (1500,1400))

# 带顶棚的共享工作区，包含参与碰撞的立柱和抬高的屋顶。
mesh('WorkshopRoof', 'Cube', (-650,0,300), (7.6,11,.16), 'Tarp', True)
# 立柱保持在既有工作站占地范围内，避免阻挡新的建造地块。
for index, (x,y) in enumerate(((-730,-350),(-570,-350),(-730,350),(-570,350))):
    mesh('WorkshopPost%d' % index, 'Cylinder', (x,y,120), (.32,.32,3.4), 'Wood', True)
for index, (x,y) in enumerate(((-1650,0),(750,0),(-450,1200),(-450,-1200))):
    mesh('BaseBoundary%d' % index, 'Cone', (x,y,-5), (.35,.35,.9), 'Marker')

trees = [(-1850,1000),(-1750,1750),(-1200,1800),(-950,1350),(-1100,1150),(-1850,1500),(-700,1700)]
for index, (x,y) in enumerate(trees):
    height = 430 + (index % 3)*55
    mesh('Tree%d_Trunk' % index, 'Cylinder', (x,y,80), (.42,.42,2.6), 'Bark', True)
    for tier in range(3):
        width = 3.4-tier*.7
        mesh('Tree%d_Crown%d' % (index,tier), 'Cone', (x,y,140+tier*95), (width,width,(height-130)/100), 'LeafLight' if tier % 2 else 'Leaf')

crystals = [(1800,1000),(1850,1600),(1200,1850),(1000,1550),(1250,1000)]
for index, (x,y) in enumerate(crystals):
    height = 220 + index % 3*65
    mesh('Crystal%d_Base' % index, 'Cube', (x,y,10), (1.25,1.25,1.2), 'Stone', True, yaw=25+index*31)
    mesh('Crystal%d_Tip' % index, 'Cone', (x,y,70+height/2), (1.45,1.45,height/100), 'Crystal', True, yaw=index*37)
    mesh('Crystal%d_Shard' % index, 'Cone', (x+90,y-30,30), (.6,.6,1.6), 'Crystal', yaw=index*21,pitch=18)

# 自然形态的边界岩台防止玩家跌出小型初始地图。
for index, (location, scale) in enumerate((((2100,0,200),(2,44,5)),((-2100,0,200),(2,44,5)),((0,2100,200),(40,2,5)),((0,-2100,200),(40,2,5)))):
    mesh('BoundaryWall%d' % index, 'Cube', location, scale, 'Stone', True)
for index in range(20):
    angle = index*math.tau/20
    radius = 2600/max(abs(math.cos(angle)),abs(math.sin(angle)))
    x, y = math.cos(angle)*radius, math.sin(angle)*radius
    mesh('Ridge%d' % index, 'Cube', (x,y,390), (7.5,6,3+index%3), 'Stone', yaw=index*41,pitch=(index%3-1)*10)

rng = random.Random(4107)
for index in range(32):
    angle, radius = rng.uniform(0,math.tau), rng.uniform(280,430)
    x, y = -1500+math.cos(angle)*radius, 1400+math.sin(angle)*radius
    mesh('MeadowTuft%d' % index, 'Cone', (x,y,-30), (.18,.22,.4), 'Meadow', yaw=index*61,pitch=12)

# 栖息地标牌朝向从基地进入的方向，而非树林背面。
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WildEncounter):
        position = actor.get_actor_location()
        label = actor.get_component_by_class(unreal.TextRenderComponent)
        label.set_world_rotation(unreal.Rotator(yaw=math.degrees(math.atan2(-position.y,-450-position.x))),False,False)

assert not (set(existing)-used), 'Old managed environment actors need an explicit migration, not silent deletion'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.FrontierEditorLibrary.prepare_prototype_navigation(world)
assert levels.save_current_level()
unreal.log('CF_VALLEY_ASSETS_OK: %d managed environment actors and %d materials' % (len(used),len(materials)))

# 山谷边界重建后同步恢复已明确扩展的洞口，避免后续重建把支线封死。
import runpy
runpy.run_path(unreal.Paths.project_content_dir()+'Python/build_hollow_content.py',run_name='__main__')
