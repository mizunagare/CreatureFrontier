import unreal

asset = unreal.load_asset('/Game/Creatures/DA_Mosskip')
assert asset
jobs = list(asset.get_editor_property('work_specialties'))
if unreal.WorkSpecialty.FARMING not in jobs:
    jobs.append(unreal.WorkSpecialty.FARMING)
asset.set_editor_property('work_specialties', jobs)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('CF_FARM_ASSET_OK: 已更新苔跳兽的耕作专长')
