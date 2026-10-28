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
for kind, name, multiplier in [(I.MATERIAL, '材料', 0), (I.CAPTURE_DEVICE, '捕捉器', 1),
                                (I.MEDICINE, '药品', 0), (I.FOOD, '食物', 0),
                                (I.CRYSTAL, '水晶', 0), (I.RESONANCE_DEVICE, '共鸣捕捉器', 1.5),
                                (I.FIBER, '纤维', 0), (I.ORE, '矿料', 0), (I.SCRAP, '机械废件', 0), (I.SEEDS, '种子', 0)]:
    item = unreal.FrontierItemDisplay()
    for key, value in dict(item=kind, display_name=name, capture_multiplier=multiplier).items():
        item.set_editor_property(key, value)
    items.append(item)
recipes = []
specs = [
    ('Capture', '捕捉器', {I.MATERIAL: 3}, R.ITEM, I.CAPTURE_DEVICE, U.NONE, False, '用于一次捕捉尝试，先削弱野生生物更容易成功。'),
    ('Medicine', '药品', {I.MATERIAL: 5}, R.ITEM, I.MEDICINE, U.NONE, False, 'H 治疗自己，P 治疗瞄准的伙伴。'),
    ('Food', '食物', {I.MATERIAL: 2}, R.ITEM, I.FOOD, U.NONE, False, 'J 喂食伙伴，U 向食物设施存入食物。'),
    ('Spear', '开拓长矛', {I.MATERIAL: 8}, R.SPEAR, I.MATERIAL, U.NONE, False, '唯一武器，制作后自动装备，按6切换装备状态。'),
    ('Vest', '旅途护甲', {I.MATERIAL: 12}, R.VEST, I.MATERIAL, U.NONE, False, '唯一护甲，制作后自动装备，按7切换装备状态。'),
    ('StudyCrystal', '研究水晶', {I.MATERIAL: 2, I.CRYSTAL: 1}, R.CRYSTAL_STUDY, I.MATERIAL, U.CRYSTAL_SAMPLE, True, '在初始基地研究实际采集的样本，解锁水晶工具与灯具。'),
    ('ResonanceDevice', '共鸣捕捉器', {I.MATERIAL: 3, I.CRYSTAL: 1}, R.ITEM, I.RESONANCE_DEVICE, U.CRYSTAL_STUDY, True, '按0选择，右键捕捉。捕捉强度为普通的1.5倍，上限仍为95%，不会自动替换。'),
]
for rid, name, costs, reward, output, required, base, description in specs:
    recipe = unreal.FrontierRecipe()
    for key, value in dict(id=rid, display_name=name, description=description, inputs=costs,
                           reward=reward, output=output, output_amount=1,
                           required_research=required, base_only=base).items():
        recipe.set_editor_property(key, value)
    recipes.append(recipe)
asset.set_editor_property('items', items)
for rid, name, costs, output, amount, required, description in [
    ('WeaveSupplies', '编织建材', {I.FIBER: 2, I.MATERIAL: 1}, I.MATERIAL, 4, U.NONE, '在基地将2份纤维和1份材料制成4份建材，净增3份材料。'),
    ('OreTools', '加工矿料工具', {I.ORE: 2, I.MATERIAL: 1}, I.CAPTURE_DEVICE, 2, U.NONE, '在基地将矿料加工为两个捕捉器。'),
    ('ScrapResonator', '修复共鸣器', {I.SCRAP: 2, I.CRYSTAL: 1}, I.RESONANCE_DEVICE, 2, U.CRYSTAL_STUDY, '完成水晶研究后，在基地用机械废件修复两个共鸣器。'),
    ('PlantingSeeds', '准备种子', {I.FIBER: 1, I.FOOD: 1}, I.SEEDS, 3, U.NONE, '在基地准备3份种子。G播种/照料/收获，F2存入农圃，F3取出预留种子。')]:
    recipe = unreal.FrontierRecipe()
    for key, value in dict(id=rid, display_name=name, description=description, inputs=costs,
                           reward=R.ITEM, output=output, output_amount=amount,
                           required_research=required, base_only=True).items():
        recipe.set_editor_property(key, value)
    recipes.append(recipe)
asset.set_editor_property('recipes', recipes)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_CRAFTING_ASSETS PASS')
