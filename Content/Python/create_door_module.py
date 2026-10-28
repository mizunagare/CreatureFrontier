"""仅创建或更新追加的木门资产，不重写既有家园资产。"""
import unreal

path = '/Game/Home/DA_Door'
asset = unreal.load_asset(path)
if not asset:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.HomeModuleDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'DA_Door', '/Game/Home', unreal.HomeModuleDefinition, factory)
assert asset, '无法创建木门定义'
for key, value in dict(display_name='木门', kind=unreal.HomeModuleKind.DOOR,
                       material_cost=12, extra_costs={},
                       required_research=unreal.FrontierResearch.NONE,
                       color=unreal.LinearColor(0.30, 0.19, 0.10, 1),
                       lantern_lumens=0.0).items():
    asset.set_editor_property(key, value)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset), '木门定义保存失败'
unreal.log('CF_DOOR_ASSET PASS')
