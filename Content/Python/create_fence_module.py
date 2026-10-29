"""只创建或更新庭院栅栏定义，不重建既有房屋资产和玩家进度。"""
import unreal

path = '/Game/Home/DA_Fence'
asset = unreal.load_asset(path)
if asset is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.HomeModuleDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'DA_Fence', '/Game/Home', unreal.HomeModuleDefinition, factory)
assert asset is not None
for key, value in dict(display_name='庭院木栅栏', kind=unreal.HomeModuleKind.FENCE,
                       material_cost=4, extra_costs={},
                       required_research=unreal.FrontierResearch.NONE,
                       color=unreal.LinearColor(.36, .23, .12, 1),
                       lantern_lumens=0.0).items():
    asset.set_editor_property(key, value)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_FENCE_ASSET PASS')
