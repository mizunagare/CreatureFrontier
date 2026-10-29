"""只创建木楼梯定义，不重新生成地图或改写既有模块。"""
import unreal

path = '/Game/Home/DA_Stairs'
asset = unreal.load_asset(path)
if asset is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.HomeModuleDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'DA_Stairs', '/Game/Home', unreal.HomeModuleDefinition, factory)
assert asset is not None, '无法创建木楼梯定义'
for key, value in dict(display_name='木楼梯', kind=unreal.HomeModuleKind.STAIRS,
                       material_cost=12, extra_costs={},
                       required_research=unreal.FrontierResearch.NONE,
                       color=unreal.LinearColor(.46, .28, .12, 1),
                       lantern_lumens=0.0).items():
    asset.set_editor_property(key, value)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset), '木楼梯定义保存失败'
unreal.log('CF_STAIRS_ASSET PASS')
