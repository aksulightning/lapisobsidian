#!/usr/bin/env python3
"""Stage assets and assemble a redistributable client with all license notices."""
import argparse
import hashlib
import json
import pathlib
import shutil
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def stage(destination):
    manifest = json.loads((ROOT/'assets/manifest.json').read_text())
    for asset in manifest['assets']:
        path = ROOT/asset['destination_filename']
        if hashlib.sha256(path.read_bytes()).hexdigest() != asset['sha256']:
            raise SystemExit(f'Asset manifest hash mismatch: {path}')
    (destination/'texpacks').mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination/'texpacks/default.zip','w',zipfile.ZIP_DEFLATED) as pack:
        for path in sorted((ROOT/'assets').glob('*.png')):
            pack.write(path, path.name)
    (destination/'audio').mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination/'audio/default.zip','w',zipfile.ZIP_DEFLATED) as pack:
        for material in ('wood','gravel','grass','stone','metal','glass','cloth','sand','snow'):
            for kind in ('dig','step'):
                pack.write(ROOT/f'assets/audio/{material}.wav',f'{kind}_{material}1.wav')
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage-only', action='store_true')
    parser.add_argument('--platform',choices=['linux','windows'],default='linux')
    args = parser.parse_args()
    engine = ROOT/'build/engine'
    stage(engine)
    if args.stage_only:
        return
    binary = engine/('LapisCube.exe' if args.platform=='windows' else 'LapisCube')
    if not binary.exists():
        raise SystemExit(f'Build the client first: {binary}')
    output = ROOT/'build'/f'LapisCube-{args.platform}'
    if output.exists():
        shutil.rmtree(output)
    output.mkdir()
    shutil.copy2(binary, output/binary.name)
    stage(output)
    shutil.copytree(ROOT/'assets',output/'asset-sources')
    shutil.copytree(ROOT/'docs',output/'docs')
    for name in ('README.md','LICENSE','NOTICE.md'):
        shutil.copy2(ROOT/name,output/name)
    licenses = output/'licenses';licenses.mkdir()
    shutil.copy2(ROOT/'engine/license.txt',licenses/'ClassiCube.txt')
    shutil.copy2(ROOT/'engine/credits.txt',licenses/'ClassiCube-credits.txt')
    # Preserve upstream third-party license files, including BearSSL and FreeType.
    for path in (ROOT/'engine').rglob('*'):
        if path.is_file() and any(word in path.name.lower() for word in ('license','licence','copying','ftl.txt')):
            rel = path.relative_to(ROOT/'engine')
            dest = licenses/'upstream'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    (output/'options.txt').write_text('fpslimit=Limit60FPS\nuse-chat-font=true\nsoundsvolume=70\nmusicvolume=0\nkey-LookUp=UP\nkey-LookDown=DOWN\nkey-LookLeft=LEFT\nkey-LookRight=RIGHT\n')
    if args.platform=='linux':
        launcher=output/'connect.sh'
        launcher.write_text('#!/bin/sh\nset -eu\ncd "$(dirname "$0")"\nexec ./LapisCube --lapis "$@"\n')
        launcher.chmod(0o755)
    else:
        (output/'connect.bat').write_text('@echo off\r\ncd /d "%~dp0"\r\nLapisCube.exe --lapis %*\r\n')
    archive = shutil.make_archive(str(output),'zip',root_dir=output.parent,base_dir=output.name)
    print(archive)


if __name__=='__main__':main()
