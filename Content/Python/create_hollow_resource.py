"""仅创建洞窟有限废件定义；不改旧资源的容量或余量。"""
import unreal
path='/Game/Resources/DA_HollowScrap'
asset=unreal.load_asset(path)
if not asset:
    factory=unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class',unreal.ResourceDefinition)
    asset=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_HollowScrap','/Game/Resources',unreal.ResourceDefinition,factory)
assert asset
for key,value in dict(display_name='洞窟旧机件',specialty=unreal.WorkSpecialty.CRAFTING,capacity=6,work_seconds=3.0,harvest_interval=.8,regrow_seconds=0.0,color=unreal.LinearColor(.55,.3,.12,1),output_item=unreal.FrontierItem.SCRAP,discovery=unreal.FrontierResearch.NONE).items():
    asset.set_editor_property(key,value)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_HOLLOW_RESOURCE_ASSET PASS')
