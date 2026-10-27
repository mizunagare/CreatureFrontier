import unreal

# Idempotent content authoring; runtime player and AI both consume these definitions.
tools = unreal.AssetToolsHelpers.get_asset_tools()
specs = [
    ('DA_Brush', 'Renewable brush', unreal.WorkSpecialty.GATHERING, 24, 180.0, (0.12, 0.5, 0.16)),
    ('DA_Stone', 'Stone deposit', unreal.WorkSpecialty.MINING, 30, 0.0, (0.2, 0.38, 0.6)),
    ('DA_Salvage', 'Salvage pile', unreal.WorkSpecialty.CRAFTING, 18, 0.0, (0.6, 0.28, 0.08)),
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
                           color=unreal.LinearColor(*color, 1)).items():
        asset.set_editor_property(key, value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_RESOURCE_ASSETS PASS')
