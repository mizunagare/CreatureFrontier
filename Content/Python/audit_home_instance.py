"""在独立Editor世界检查临时门的材质实例；不保存场景或资产。"""
import json
import unreal

definition = unreal.load_asset('/Game/Home/DA_Door')
assert definition, '缺少木门定义'
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
door = editor.spawn_actor_from_class(unreal.HomeModule, unreal.Vector(0, 0, 2000), unreal.Rotator(), True)
assert door, '无法生成临时审计门'


def rgba(color):
    return [float(getattr(color, channel)) for channel in ('r', 'g', 'b', 'a')]


try:
    door.set_editor_property('definition', definition)
    rows = []
    for mesh in door.get_components_by_class(unreal.StaticMeshComponent):
        material = mesh.get_material(0)
        dynamic = isinstance(material, unreal.MaterialInstanceDynamic)
        rows.append(dict(part=mesh.get_name(), material=material.get_path_name() if material else '',
                         dynamic=dynamic, tint=rgba(material.get_vector_parameter_value('Tint')) if dynamic else None))
    unreal.log('CF_HOME_INSTANCE_AUDIT ' + json.dumps(
        dict(expected=rgba(definition.get_editor_property('color')), parts=rows), ensure_ascii=False))
    assert len(rows) == 4, '临时门零件数量异常'
    assert all(row['dynamic'] for row in rows), '存在未使用动态材质的零件'
    expected = rgba(definition.get_editor_property('color'))
    assert all(all(abs(actual - wanted) < 0.0001 for actual, wanted in zip(row['tint'], expected))
               for row in rows), '零件材质实例颜色与资产定义不一致'
    unreal.log('CF_HOME_INSTANCE_AUDIT PASS transient=1 saved=0 matched=4')
finally:
    assert editor.destroy_actor(door), '临时审计门销毁失败'
