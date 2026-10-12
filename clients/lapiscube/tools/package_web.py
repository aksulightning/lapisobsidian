#!/usr/bin/env python3
"""Package the compiled C browser client with local, licensed resources only."""
import json
import pathlib
import shutil
import subprocess
import package
import argparse

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--singleplayer',action='store_true')
    singleplayer=parser.parse_args().singleplayer
    engine = ROOT/'build/engine'
    if (engine/'.lapiscube-build-mode').read_text() != ('singleplayer' if singleplayer else 'web')+'-release-v2':
        raise SystemExit('Run make web first')
    if (engine/'LapisCube.wasm').read_bytes()[:4] != b'\0asm':
        raise SystemExit('Missing compiled WebAssembly module')
    output = ROOT/'build'/('singleplayer' if singleplayer else 'webclient')
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
    if singleplayer:
        for name in ('LapisServer.js','LapisServer.wasm'):
            shutil.copy2(ROOT/'build'/name,output/name)
        for path in (ROOT/'standalone').glob('*.js'):
            shutil.copy2(path,output/path.name)
        page=output/'index.html'
        page.write_text(page.read_text().replace('<script src="web.js">','<script src="singleplayer.js"></script>\n  <script src="web.js">')
                        .replace('<title>LapisCube</title>','<title>LapisCube Singleplayer</title>'))
        with (output/'web.css').open('a') as css:
            css.write('\n#singleplayer-save{position:fixed;top:8px;right:8px;z-index:20;font:12px sans-serif;color:white;background:#122c38dd;padding:6px;border-radius:6px} #singleplayer-save button{font:inherit;padding:5px;margin:0} #save-status{display:block;max-width:280px}\n')
        shutil.copy2(ROOT.parents[1]/'LICENSE',output/'LICENSE-server.txt')
        shutil.copy2(ROOT/'docs/singleplayer.md',output/'README.md')
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
        (output/'SOURCE-server.txt').write_text('GPL-3.0 corresponding source: https://github.com/aksulightning/lapisobsidian/tree/'+revision+'\nBuild: make -C clients/lapiscube singleplayer (Emscripten 4.0.23)\n')
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
    package.release_manifest(output,'web-singleplayer' if singleplayer else 'web')
    archive=shutil.make_archive(str(ROOT/'build'/('LapisCube-singleplayer' if singleplayer else 'LapisCube-web')),'zip',root_dir=output.parent,base_dir=output.name)
    print(archive)


if __name__=='__main__': main()
