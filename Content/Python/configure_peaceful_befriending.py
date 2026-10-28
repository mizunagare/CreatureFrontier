"""仅配置现有物种的和平结交成本，不改身份、数值或外观。"""
import unreal
for name, cost in [('DA_Mosskip',3),('DA_Cinderkit',0),('DA_Craglet',0)]:
    asset = unreal.load_asset('/Game/Creatures/' + name)
    assert asset
    asset.set_editor_property('peaceful_food_cost', cost)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
    assert asset.get_editor_property('peaceful_food_cost') == cost
unreal.log('CF_BEFRIEND_ASSETS PASS')
