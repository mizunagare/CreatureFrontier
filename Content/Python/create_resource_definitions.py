import unreal

# 幂等生成内容，玩家与伙伴运行时共享这些资源定义。
tools = unreal.AssetToolsHelpers.get_asset_tools()
specs = [
    ('DA_Brush', '再生灌木', unreal.WorkSpecialty.GATHERING, 24, 180.0, (0.12, 0.5, 0.16)),
    ('DA_Stone', '石料矿床', unreal.WorkSpecialty.MINING, 30, 0.0, (0.2, 0.38, 0.6)),
    ('DA_Salvage', '废料堆', unreal.WorkSpecialty.CRAFTING, 18, 0.0, (0.6, 0.28, 0.08)),
    ('DA_Crystal', '高地水晶矿', unreal.WorkSpecialty.MINING, 8, 0.0, (0.55, 0.15, 0.8)),
    ('DA_Fiber', '河岸纤维', unreal.WorkSpecialty.GATHERING, 18, 240.0, (0.65, 0.72, 0.20)),
    ('DA_Ore', '锈岭矿料', unreal.WorkSpecialty.MINING, 16, 0.0, (0.72, 0.28, 0.12)),
    ('DA_Scrap', '废弃机械', unreal.WorkSpecialty.CRAFTING, 12, 0.0, (0.25, 0.30, 0.34)),
]
for name, label, specialty, capacity, regrow, color in specs:
    path = '/Game/Resources/' + name
    asset = unreal.load_asset(path)
    if not asset:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ResourceDefinition)
        asset = tools.create_asset(name, '/Game/Resources', unreal.ResourceDefinition, factory)
    assert asset
    for key, value in dict(display_name=label, specialty=specialty, capacity=capacity,
                           work_seconds=3.0, harvest_interval=0.8, regrow_seconds=regrow,
                           color=unreal.LinearColor(*color, 1),
                           output_item={'DA_Crystal': unreal.FrontierItem.CRYSTAL, 'DA_Fiber': unreal.FrontierItem.FIBER,
                                        'DA_Ore': unreal.FrontierItem.ORE, 'DA_Scrap': unreal.FrontierItem.SCRAP}.get(name, unreal.FrontierItem.MATERIAL),
                           discovery=unreal.FrontierResearch.CRYSTAL_SAMPLE if name == 'DA_Crystal' else unreal.FrontierResearch.NONE).items():
        asset.set_editor_property(key, value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_RESOURCE_ASSETS PASS')
