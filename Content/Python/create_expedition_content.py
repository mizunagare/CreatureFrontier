import runpy
import unreal

root = unreal.Paths.project_content_dir() + 'Python/'
for script in ('create_crafting_catalog.py', 'create_resource_definitions.py', 'create_home_modules.py'):
    runpy.run_path(root + script, run_name='__main__')
unreal.log('CF_EXPEDITION_ASSETS PASS')
