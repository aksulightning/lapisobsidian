#!/usr/bin/env python3
"""Package the compiled C browser client with local, licensed resources only."""
import json
import pathlib
import shutil
import subprocess
import package

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    engine = ROOT/'build/engine'
    if (engine/'.lapiscube-build-mode').read_text() != 'web-release-v2':
        raise SystemExit('Run make web first')
    if (engine/'LapisCube.wasm').read_bytes()[:4] != b'\0asm':
        raise SystemExit('Missing compiled WebAssembly module')
    output = ROOT/'build/webclient'
    shutil.rmtree(output, ignore_errors=True)
    shutil.copytree(ROOT/'web', output, ignore=shutil.ignore_patterns('build.mk'))
    shutil.copy2(ROOT/'assets/web-icon.svg',output/'icon.svg')
    for name in ('LapisCube.js', 'LapisCube.wasm'):
        shutil.copy2(engine/name, output/name)
    package.stage(output)
    shutil.move(output/'texpacks/default.zip', output/'default.zip')
    shutil.rmtree(output/'texpacks')
    shutil.rmtree(output/'audio')
    sounds = output/'sounds'; sounds.mkdir()
    mapping = json.loads((ROOT/'assets/sound-map.json').read_text())
    for clips in mapping['material'].values():
        for name in clips:
            shutil.copy2(ROOT/f'assets/audio/{name}.wav',sounds/(name+'.wav'))
    shutil.copytree(ROOT/'assets',output/'asset-sources')
    for name in ('README.md','NOTICE.md'):
        shutil.copy2(ROOT/name, output/name)
    shutil.copy2(ROOT/'LICENSE',output/'LICENSE.txt')
    shutil.copy2(ROOT/'docs/web.md',output/'web.md')
    package.write_credits(output/'credits.md')
    licenses = output/'licenses'; licenses.mkdir()
    for path in (ROOT/'engine').rglob('*'):
        if path.is_file() and any(word in path.name.lower() for word in ('license','licence','copying','ftl.txt')):
            dest=licenses/'upstream'/path.relative_to(ROOT/'engine')
            dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    shutil.copy2(ROOT/'engine/credits.txt',licenses/'ClassiCube-credits.txt')
    emcc=shutil.which('emcc')
    if not emcc: raise SystemExit('emcc must remain on PATH for compiler license packaging')
    compiler_license=pathlib.Path(emcc).resolve().parent/'LICENSE'
    shutil.copy2(compiler_license,licenses/'Emscripten.txt')
    (output/'toolchain.json').write_text(json.dumps({'emcc':subprocess.check_output(['emcc','--version'],text=True).splitlines()[0]},indent=2)+'\n')
    package.release_manifest(output,'web')
    archive=shutil.make_archive(str(ROOT/'build/LapisCube-web'),'zip',root_dir=output.parent,base_dir=output.name)
    print(archive)


if __name__=='__main__': main()
