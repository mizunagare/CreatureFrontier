"""只读检查家园颜色定义和调色材质参数，不保存或修改资产。"""
import json
import unreal

palette = unreal.load_asset('/Game/Materials/M_CreaturePalette')
assert palette, '缺少家园共用调色材质'
parameters = [str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(palette)]
assert 'Tint' in parameters, '调色材质缺少Tint参数'
default_color = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(palette, 'Tint')


def rgba(color):
    return [float(getattr(color, channel)) for channel in ('r', 'g', 'b', 'a')]


rows = []
for name in ('DA_Floor', 'DA_Wall', 'DA_Doorway', 'DA_Roof', 'DA_Door'):
    asset = unreal.load_asset('/Game/Home/' + name)
    assert asset, '缺少家园定义：' + name
    rows.append(dict(asset=name, color=rgba(asset.get_editor_property('color'))))
unreal.log('CF_HOME_PALETTE_AUDIT ' + json.dumps(
    dict(parameters=parameters, default_color=rgba(default_color), definitions=rows), ensure_ascii=False))
unreal.log('CF_HOME_PALETTE_AUDIT PASS readonly=1')
