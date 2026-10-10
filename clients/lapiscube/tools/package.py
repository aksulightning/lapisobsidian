#!/usr/bin/env python3
"""Stage assets and assemble a redistributable client with all license notices."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def write_credits(destination):
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    content = (ROOT.parents[1]/'docs/credits.md').read_text()
    # Repository-relative links would break in the standalone release layout.
    base = 'https://github.com/aksulightning/lapisobsidian/blob/' + revision + '/'
    content = re.sub(r'\]\(\.\./([^)]*)\)', lambda match: '](' + base + match[1] + ')', content)
    destination.write_text(content)


def validate_binary(engine, platform):
    mode = 'native' if platform == 'linux' else 'windows'
    stamp = engine/'.lapiscube-build-mode'
    if not stamp.is_file() or stamp.read_text() != mode+'-release-v2':
        raise SystemExit(f'Package requires a {mode} build; run make {mode} first')
    binary = engine/('LapisCube.exe' if platform == 'windows' else 'LapisCube')
    if not binary.is_file():
        raise SystemExit(f'Build the client first: {binary}')
    with binary.open('rb') as stream:
        header = stream.read(4)
    if (platform == 'linux' and header != b'\x7fELF') or (platform == 'windows' and header[:2] != b'MZ'):
        raise SystemExit(f'Binary format does not match {platform}: {binary}')
    if platform == 'linux' and not binary.stat().st_mode & 0o111:
        raise SystemExit(f'Linux client is not executable: {binary}')
    return binary


def release_manifest(output, platform):
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    engine = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT/'engine', text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--', '.'], cwd=ROOT, text=True).strip())
    files = {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(output.rglob('*')) if p.is_file()}
    (output/'release.json').write_text(json.dumps({'schema_version': 1, 'platform': platform,
        'client_revision': revision, 'modified_client_sources': dirty, 'engine_revision': engine,
        'development_build': True, 'files': files}, indent=2)+'\n')


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
        mapping = json.loads((ROOT/'assets/sound-map.json').read_text())
        for clips in mapping['material'].values():
            for name in clips:
                pack.write(ROOT/f'assets/audio/{name}.wav',name+'.wav')
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage-only', action='store_true')
    parser.add_argument('--platform',choices=['linux','windows'],default='linux')
    args = parser.parse_args()
    engine = ROOT/'build/engine'
    if args.stage_only:
        stage(engine)
        return
    binary = validate_binary(engine, args.platform)
    output = ROOT/'build'/f'LapisCube-{args.platform}'
    if output.exists():
        shutil.rmtree(output)
    output.mkdir()
    shutil.copy2(binary, output/binary.name)
    stage(output)
    shutil.copytree(ROOT/'assets',output/'asset-sources')
    shutil.copytree(ROOT/'docs',output/'docs')
    write_credits(output/'docs/credits.md')
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
    (output/'options.txt').write_text('lapis-entity-effects=true\nfpslimit=Limit60FPS\nuse-chat-font=true\nsoundsvolume=70\nmusicvolume=0\nkey-LookUp=UP\nkey-LookDown=DOWN\nkey-LookLeft=LEFT\nkey-LookRight=RIGHT\n')
    if args.platform=='linux':
        launcher=output/'connect.sh'
        launcher.write_text('#!/bin/sh\nset -eu\ncd "$(dirname "$0")"\nexec ./LapisCube --lapis "$@"\n')
        launcher.chmod(0o755)
    else:
        (output/'connect.bat').write_text('@echo off\r\ncd /d "%~dp0"\r\nLapisCube.exe --lapis %*\r\n')
    release_manifest(output, args.platform)
    archive = shutil.make_archive(str(output),'zip',root_dir=output.parent,base_dir=output.name)
    print(archive)


if __name__=='__main__':main()
