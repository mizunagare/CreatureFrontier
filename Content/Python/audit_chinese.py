"""只读核对现有资产的中文展示字段；不保存资产、地图或玩家存档。"""
import re
import unreal


def check_text(value, context, expected=None):
    text = str(value)
    assert re.search(r'[\u4e00-\u9fff]', text), '展示字段缺少中文：' + context
    assert not re.search(r'[A-Za-z]{2,}', text), '展示字段残留英文：' + context + '：' + text
    if expected is not None:
        assert text == expected, '中文名称不一致：' + context + '：' + text


groups = {
    'Creatures': [('Mosskip', '苔跳兽'), ('Cinderkit', '烬尾兽'), ('Craglet', '岩角兽')],
    'Resources': [('Brush', '再生灌木'), ('Stone', '石料矿床'), ('Salvage', '废料堆'),
                  ('Crystal', '高地水晶矿'), ('Fiber', '河岸纤维'), ('Ore', '锈岭矿料'),
                  ('Scrap', '废弃机械'), ('HollowScrap', '洞窟旧机件')],
    'Home': [('Floor', '木地板'), ('Wall', '木墙'), ('Doorway', '开放门框'),
             ('Roof', '平屋顶'), ('Lantern', '水晶灯'), ('Storage', '储物箱'),
             ('Door', '木门'), ('Window', '开放窗墙'), ('Fence', '庭院木栅栏'), ('Stairs', '木楼梯')],
}
special_names = {'Mosskip': '荆棘缠绕', 'Cinderkit': '余烬爆发', 'Craglet': '岩石守护'}
asset_count = 0
for folder, entries in groups.items():
    for key, expected in entries:
        path = '/Game/' + folder + '/DA_' + key
        asset = unreal.load_asset(path)
        assert asset, '缺少待审计资产：' + path
        check_text(asset.get_editor_property('display_name'), path, expected)
        if folder == 'Creatures':
            check_text(asset.get_editor_property('description'), path + '.description')
            check_text(asset.get_editor_property('special_name'), path + '.special_name', special_names[key])
        asset_count += 1

catalog = unreal.load_asset('/Game/Progression/DA_CraftingCatalog')
assert catalog, '缺少物品配方目录'
item_names = ['材料', '捕捉器', '药品', '食物', '水晶', '共鸣捕捉器', '纤维', '矿料', '机械废件', '种子']
items = list(catalog.get_editor_property('items'))
seen_items = set()
for item in items:
    index = int(item.get_editor_property('item').value)
    assert 0 <= index < len(item_names) and index not in seen_items, '物品身份未知或重复'
    seen_items.add(index)
    check_text(item.get_editor_property('display_name'), '物品：' + str(index), item_names[index])
assert len(seen_items) == len(item_names), '物品目录不完整'

recipe_names = {
    'Capture': '捕捉器', 'Medicine': '药品', 'Food': '食物', 'Spear': '开拓长矛',
    'Vest': '旅途护甲', 'StudyCrystal': '研究水晶', 'ResonanceDevice': '共鸣捕捉器',
    'WeaveSupplies': '编织建材', 'OreTools': '加工矿料工具',
    'ScrapResonator': '修复共鸣器', 'PlantingSeeds': '准备种子',
}
recipes = list(catalog.get_editor_property('recipes'))
seen_recipes = set()
for recipe in recipes:
    key = str(recipe.get_editor_property('id'))
    assert key in recipe_names and key not in seen_recipes, '配方身份未知或重复：' + key
    seen_recipes.add(key)
    check_text(recipe.get_editor_property('display_name'), '配方：' + key, recipe_names[key])
    check_text(recipe.get_editor_property('description'), '配方说明：' + key)
assert seen_recipes == set(recipe_names), '配方目录不完整'

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype'), '无法打开初始地图'
regions = {'MeadowGrove': '草甸林地', 'CrystalRise': '水晶高地', 'EmberHollow': '余烬洞窟'}
seen_regions = set()
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WildEncounter):
        key = str(actor.get_editor_property('encounter_id'))
        assert key in regions and key not in seen_regions, '栖息地身份未知或重复：' + key
        seen_regions.add(key)
        check_text(actor.get_editor_property('region_name'), '栖息地：' + key, regions[key])
assert seen_regions == set(regions), '栖息地不完整'
unreal.log(f'CF_CHINESE_AUDIT PASS readonly=1 assets={asset_count} items={len(items)} recipes={len(recipes)} regions={len(seen_regions)}')
