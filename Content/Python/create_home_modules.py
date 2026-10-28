import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
specs = [('DA_Floor', '木地板', unreal.HomeModuleKind.FLOOR, 8, (0.36, 0.23, 0.12)),
         ('DA_Wall', '木墙', unreal.HomeModuleKind.WALL, 6, (0.42, 0.3, 0.17)),
         ('DA_Doorway', '开放门框', unreal.HomeModuleKind.DOORWAY, 8, (0.3, 0.19, 0.1)),
         ('DA_Roof', '平屋顶', unreal.HomeModuleKind.ROOF, 10, (0.16, 0.25, 0.28)),
         ('DA_Lantern', '水晶灯', unreal.HomeModuleKind.LANTERN, 4, (0.72, 0.42, 0.12)),
         ('DA_Storage', '储物箱', unreal.HomeModuleKind.STORAGE, 6, (0.46, 0.28, 0.1))]
for name, label, kind, cost, color in specs:
    path = '/Game/Home/' + name
    asset = unreal.load_asset(path)
    if not asset:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.HomeModuleDefinition)
        asset = tools.create_asset(name, '/Game/Home', unreal.HomeModuleDefinition, factory)
    assert asset
    for key, value in dict(display_name=label, kind=kind, material_cost=cost,
                           color=unreal.LinearColor(*color, 1),
                           lantern_lumens=12000.0 if name == 'DA_Lantern' else 0.0,
                           extra_costs={unreal.FrontierItem.CRYSTAL: 2} if name == 'DA_Lantern' else {},
                           required_research=unreal.FrontierResearch.CRYSTAL_STUDY if name == 'DA_Lantern' else unreal.FrontierResearch.NONE).items():
        asset.set_editor_property(key, value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_HOME_ASSETS PASS')
