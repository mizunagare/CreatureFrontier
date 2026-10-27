import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
path = '/Game/Progression/DA_CraftingCatalog'
asset = unreal.load_asset(path)
if not asset:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.CraftingCatalog)
    asset = tools.create_asset('DA_CraftingCatalog', '/Game/Progression', unreal.CraftingCatalog, factory)
assert asset
I, R, U = unreal.FrontierItem, unreal.RecipeReward, unreal.FrontierResearch
items = []
for kind, name, multiplier in [(I.MATERIAL, 'Materials', 0), (I.CAPTURE_DEVICE, 'Devices', 1),
                                (I.MEDICINE, 'Medicine', 0), (I.FOOD, 'Food', 0),
                                (I.CRYSTAL, 'Crystals', 0), (I.RESONANCE_DEVICE, 'Resonance devices', 1.5)]:
    item = unreal.FrontierItemDisplay()
    for key, value in dict(item=kind, display_name=name, capture_multiplier=multiplier).items():
        item.set_editor_property(key, value)
    items.append(item)
recipes = []
specs = [
    ('Capture', 'Capture device', {I.MATERIAL: 3}, R.ITEM, I.CAPTURE_DEVICE, U.NONE, False, 'One capture attempt. Injure a wild creature first.'),
    ('Medicine', 'Medicine', {I.MATERIAL: 5}, R.ITEM, I.MEDICINE, U.NONE, False, 'H heals you; P treats an aimed companion.'),
    ('Food', 'Food', {I.MATERIAL: 2}, R.ITEM, I.FOOD, U.NONE, False, 'J feeds a companion; U stocks a food store.'),
    ('Spear', 'Frontier Spear', {I.MATERIAL: 8}, R.SPEAR, I.MATERIAL, U.NONE, False, 'Unique weapon. Crafted equipped; 6 toggles it.'),
    ('Vest', 'Trail Vest', {I.MATERIAL: 12}, R.VEST, I.MATERIAL, U.NONE, False, 'Unique armor. Crafted equipped; 7 toggles it.'),
    ('StudyCrystal', 'Study crystal', {I.MATERIAL: 2, I.CRYSTAL: 1}, R.CRYSTAL_STUDY, I.MATERIAL, U.CRYSTAL_SAMPLE, True, 'At the original base, study a real harvested sample to unlock crystal tools and lanterns.'),
    ('ResonanceDevice', 'Resonance device', {I.MATERIAL: 3, I.CRYSTAL: 1}, R.ITEM, I.RESONANCE_DEVICE, U.CRYSTAL_STUDY, True, '0 selects this device for RMB: 1.5x capture power, still capped at 95%. Never auto-falls back.'),
]
for rid, name, costs, reward, output, required, base, description in specs:
    recipe = unreal.FrontierRecipe()
    for key, value in dict(id=rid, display_name=name, description=description, inputs=costs,
                           reward=reward, output=output, output_amount=1,
                           required_research=required, base_only=base).items():
        recipe.set_editor_property(key, value)
    recipes.append(recipe)
asset.set_editor_property('items', items)
asset.set_editor_property('recipes', recipes)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_CRAFTING_ASSETS PASS')
