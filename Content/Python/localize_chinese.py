"""仅更新现有资产的中文展示字段，不重建资产或改变数值、路径及存档身份。"""
import unreal

assets = {
    '/Game/Creatures/DA_Mosskip': dict(display_name='苔跳兽', description='长着柔韧叶耳的食草生物，耐心的双爪擅长采集，也能帮忙照料农圃。', special_name='荆棘缠绕'),
    '/Game/Creatures/DA_Cinderkit': dict(display_name='烬尾兽', description='背鳍明亮的小生物，会用温暖的尾巴协助制作工具。', special_name='余烬爆发'),
    '/Game/Creatures/DA_Craglet': dict(display_name='岩角兽', description='长着水晶角的结实掘地生物，步伐缓慢，采矿时却能挥出有力的重击。', special_name='岩石守护'),
}
for key, name in [('Brush','再生灌木'),('Stone','石料矿床'),('Salvage','废料堆'),('Crystal','高地水晶矿'),('Fiber','河岸纤维'),('Ore','锈岭矿料'),('Scrap','废弃机械'),('HollowScrap','洞窟旧机件')]:
    assets['/Game/Resources/DA_' + key] = dict(display_name=name)
for key, name in [('Floor','木地板'),('Wall','木墙'),('Doorway','开放门框'),('Roof','平屋顶'),('Lantern','水晶灯'),('Storage','储物箱')]:
    assets['/Game/Home/DA_' + key] = dict(display_name=name)

for path, fields in assets.items():
    asset = unreal.load_asset(path)
    assert asset, '缺少资产：' + path
    for key, value in fields.items():
        asset.set_editor_property(key, value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset), '资产保存失败：' + path

catalog = unreal.load_asset('/Game/Progression/DA_CraftingCatalog')
assert catalog
item_names = ['材料','捕捉器','药品','食物','水晶','共鸣捕捉器','纤维','矿料','机械废件','种子']
items = list(catalog.get_editor_property('items'))
for item in items:
    index = int(item.get_editor_property('item').value)
    assert 0 <= index < len(item_names)
    item.set_editor_property('display_name', item_names[index])
catalog.set_editor_property('items', items)
recipes = {
    'Capture': ('捕捉器','用于一次捕捉尝试，先削弱野生生物更容易成功。'),
    'Medicine': ('药品','H 治疗自己，P 治疗瞄准的伙伴。'),
    'Food': ('食物','J 喂食伙伴，U 向食物设施存入食物。'),
    'Spear': ('开拓长矛','唯一武器，制作后自动装备，按6切换装备状态。'),
    'Vest': ('旅途护甲','唯一护甲，制作后自动装备，按7切换装备状态。'),
    'StudyCrystal': ('研究水晶','在初始基地研究实际采集的样本，解锁水晶工具与灯具。'),
    'ResonanceDevice': ('共鸣捕捉器','按0选择，右键捕捉。捕捉强度为普通的1.5倍，上限仍为95%，不会自动替换。'),
    'WeaveSupplies': ('编织建材','在基地将2份纤维和1份材料制成4份建材，净增3份材料。'),
    'OreTools': ('加工矿料工具','在基地将矿料加工为两个捕捉器。'),
    'ScrapResonator': ('修复共鸣器','完成水晶研究后，在基地用机械废件修复两个共鸣器。'),
    'PlantingSeeds': ('准备种子','在基地准备3份种子。G播种/照料/收获，F2存入农圃，F3取出预留种子。'),
}
entries = list(catalog.get_editor_property('recipes'))
assert len(entries) == len(recipes)
for entry in entries:
    key = str(entry.get_editor_property('id'))
    assert key in recipes, '未知配方：' + key
    name, description = recipes[key]
    entry.set_editor_property('display_name', name)
    entry.set_editor_property('description', description)
catalog.set_editor_property('recipes', entries)
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/Maps/L_Prototype')
regions = {'MeadowGrove':'草甸林地', 'CrystalRise':'水晶高地', 'EmberHollow':'余烬洞窟'}
seen = set()
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.WildEncounter):
        key = str(actor.get_editor_property('encounter_id'))
        assert key in regions, '未知栖息地：' + key
        actor.set_editor_property('region_name', regions[key])
        assert key not in seen, '栖息地身份重复：' + key
        seen.add(key)
assert seen == set(regions), '栖息地不完整'
assert levels.save_current_level()
unreal.log(f'CF_CHINESE_ASSETS PASS assets={len(assets)} recipes=11 items=10 regions={len(seen)}')
