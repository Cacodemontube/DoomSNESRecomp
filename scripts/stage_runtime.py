"""Stage portable runtime files only; dependencies are bundled separately."""
import argparse
import pathlib
import shutil
import subprocess
import zipfile

p=argparse.ArgumentParser()
p.add_argument('--build', default='build-release')
p.add_argument('--platform', required=True)
p.add_argument('--zip', action='store_true')
a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent
build=root/a.build
version=(root/'VERSION').read_text().strip()
name=f'DoomSNESRecomp-v{version}-{a.platform}'
stage=root/'dist'/name
if not a.zip:
    stage.mkdir(parents=True,exist_ok=False)
    exe='DoomSNESRecomp.exe' if a.platform.startswith('Windows') else 'DoomSNESRecomp'
    shutil.copy2(build/exe,stage/exe)
    for folder in ('assets','mods'):
        shutil.copytree(build/folder,stage/folder)
    for state in stage.rglob('state.toml*'):
        state.unlink()
    for filename in ('VERSION','CHANGELOG.md','LICENSE','LICENSE.txt','LICENSE.md'):
        if (root/filename).is_file():shutil.copy2(root/filename,stage/filename)
    tracked=subprocess.check_output(['git','ls-files','--recurse-submodules'],cwd=root,text=True)
    for filename in tracked.splitlines():
        path=pathlib.Path(filename)
        if path.name.upper().startswith(('LICENSE','COPYING','NOTICE')):
            dest=stage/'licenses'/path;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(root/path,dest)
    sdl=build/'_deps/sdl3-src/LICENSE.txt'
    if not sdl.is_file():raise RuntimeError('SDL3 license missing')
    (stage/'licenses/SDL3').mkdir(parents=True,exist_ok=True)
    shutil.copy2(sdl,stage/'licenses/SDL3/LICENSE.txt')
    revisions=[]
    for label,directory in [('game',root),('snesrecomp',root/'snesrecomp'),('recomp-ui',root/'recomp-ui')]:
        rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=directory,text=True).strip()
        revisions.append(f'{label}={rev}')
    (stage/'SOURCE_REVISIONS').write_text('\n'.join(revisions)+'\n')
    (stage/'README.txt').write_text(
        f'DoomSNESRecomp v{version} - {a.platform}\n\n'
        f'Extract the complete archive and run {exe}. On macOS or Linux run\n'
        './DoomSNESRecomp from a terminal in this folder. Keep assets/ and mods/\n'
        'beside the executable. Select your own verified Doom (USA).sfc ROM.\n'
        'No ROM is included. Enhancements are optional and off by default.\n'
        'See CHANGELOG.md for changes and remaining limitations.\n')
else:
    files=list(stage.rglob('*'))
    forbidden={'.sfc','.smc','.srm','.sav','.mp4','.fig'}
    personal={'config.ini','rom.cfg','keybinds.ini','input.ini','state.toml'}
    for path in files:
        if path.is_file() and (path.suffix.lower() in forbidden or path.name in personal):
            raise RuntimeError(f'Forbidden runtime file: {path}')
        if path.name=='gen' or path.name=='funcs.h':raise RuntimeError('Generated source in package')
    with zipfile.ZipFile(root/'dist'/f'{name}.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            if path.is_file():archive.write(path,path.relative_to(stage.parent))
    print(root/'dist'/f'{name}.zip')
