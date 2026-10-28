"""幂等生成南侧余烬洞窟；仅更新明确命名的洞窟Actor和两个旧边界Actor。"""
import math
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.EditorAssetLibrary
assert levels.load_level('/Game/Maps/L_Prototype')
all_actors = actors.get_all_level_actors()
existing = {a.get_actor_label(): a for a in all_actors}
cube = unreal.load_asset('/Engine/BasicShapes/Cube.Cube')
stone = unreal.load_asset('/Game/Environment/Materials/MI_Stone')
path_material = unreal.load_asset('/Game/Environment/Materials/MI_Path')
assert cube and stone and path_material

def block(name, position, scale, solid=True, material=None, yaw=0):
    label = 'CF_Hollow_' + name
    actor = existing.get(label)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    assert isinstance(actor, unreal.StaticMeshActor)
    actor.set_actor_label(label)
    actor.set_folder_path('Ember Hollow')
    actor.set_actor_location(unreal.Vector(*position),False,False)
    actor.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0,material or stone)
    actor.static_mesh_component.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
    return actor

# 把旧南墙保留为左段，右段使用新的命名Actor；中间留8米宽实际开口。
south = existing.get('CF_Valley_BoundaryWall3')
ridge = existing.get('CF_Valley_Ridge15')
assert south and ridge, '缺少已知边界，拒绝猜测或删除场景物体'
south.set_actor_location(unreal.Vector(-1200,-2100,200),False,False)
south.set_actor_scale3d(unreal.Vector(16,2,5))
block('SouthBoundaryRight',(1200,-2100,200),(16,2,5))
ridge.set_actor_location(unreal.Vector(1600,-2800,390),False,False)

# 地面顶面均为-50，与原山谷齐平，无台阶或空隙。
block('EntryFloor',(0,-2450,-100),(9,11,1))
block('ChamberFloor',(0,-3550,-100),(18,16,1))
block('EntryLeft',(-550,-2500,140),(2,12,3.8))
block('EntryRight',(550,-2500,140),(2,12,3.8))
block('WestWall',(-1000,-3550,140),(2,18,3.8))
block('EastWall',(1000,-3550,140),(2,18,3.8))
block('NorthWestWall',(-725,-2700,140),(5.5,2,3.8))
block('NorthEastWall',(725,-2700,140),(5.5,2,3.8))
block('EndWall',(0,-4400,140),(22,2,3.8))
block('Roof',(0,-3450,390),(22,22,1))
block('EntryRoof',(0,-2550,390),(13,9,1))

# 交错岩脊保留宽通道，形成视线转折而不是封死洞室。
block('BendWest',(-400,-3050,110),(10,2,3.2))
block('BendEast',(450,-3550,110),(9,2,3.2))
block('CacheCover',(-350,-3950,40),(2,2,1.8),yaw=25)

def trail(name,start,end,width=140):
    dx,dy=end[0]-start[0],end[1]-start[1]
    block(name,((start[0]+end[0])/2,(start[1]+end[1])/2,-48.5),(math.hypot(dx,dy)/100,width/100,.02),False,path_material,math.degrees(math.atan2(dy,dx)))
trail('ApproachPath',(-450,-700),(0,-1800))
trail('EntryPath',(0,-1800),(0,-2700))
trail('BendPath',(0,-2700),(260,-3200))
trail('SwitchbackPath',(260,-3200),(-260,-3350))
trail('ChamberPath',(-260,-3350),(-500,-3950))

for index,position in enumerate([(0,-2450,250),(400,-3250,250),(-450,-4000,250)]):
    label='CF_Hollow_Light%d' % index
    light=existing.get(label)
    if light is None:
        light=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(*position))
    assert isinstance(light,unreal.PointLight)
    light.set_actor_label(label)
    light.set_folder_path('Ember Hollow')
    light.set_actor_location(unreal.Vector(*position),False,False)
    light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.light_component.set_editor_property('intensity',1800.0)
    light.light_component.set_editor_property('attenuation_radius',850.0)
    light.light_component.set_light_color(unreal.LinearColor(1.0,.3,.08,1))

sites=[a for a in all_actors if isinstance(a,unreal.WildEncounter) and str(a.get_editor_property('encounter_id'))=='EmberHollow']
assert len(sites)<=1
site=sites[0] if sites else actors.spawn_actor_from_class(unreal.WildEncounter,unreal.Vector(-400,-3850,-50))
for key,value in dict(encounter_id='EmberHollow',region_name='余烬洞窟',definition=unreal.load_asset('/Game/Creatures/DA_Cinderkit'),creature_level=3,respawn_seconds=150.0,boss=False).items():
    site.set_editor_property(key,value)
site.set_actor_label('Ember Hollow Encounter')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.FrontierEditorLibrary.prepare_prototype_navigation(world)
assert levels.save_current_level()
unreal.log('CF_HOLLOW_ASSETS PASS')
