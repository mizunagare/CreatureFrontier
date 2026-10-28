"""只创建窗墙定义，不重写既有家园资产。"""
import unreal

path = '/Game/Home/DA_Window'
asset = unreal.load_asset(path)
if not asset:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.HomeModuleDefinition)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'DA_Window', '/Game/Home', unreal.HomeModuleDefinition, factory)
assert asset, '无法创建窗墙定义'
for key, value in dict(display_name='开放窗墙', kind=unreal.HomeModuleKind.WINDOW,
                       material_cost=10, extra_costs={},
                       required_research=unreal.FrontierResearch.NONE,
                       color=unreal.LinearColor(0.42, 0.30, 0.17, 1),
                       lantern_lumens=0.0).items():
    asset.set_editor_property(key, value)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset), '窗墙定义保存失败'
unreal.log('CF_WINDOW_ASSET PASS')
