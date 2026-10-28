"""明确重建余烬洞窟内容，保留原场景及资产身份。"""
import runpy
import unreal
root=unreal.Paths.project_content_dir()+'Python/'
for script in ['create_hollow_resource.py','build_ember_hollow.py']:
    runpy.run_path(root+script,run_name='__main__')
