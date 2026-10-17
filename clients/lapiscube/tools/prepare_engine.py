#!/usr/bin/env python3
"""Stage the pinned engine and apply the small, reviewable integration patch."""
import pathlib
import shutil
import subprocess
import argparse
import hashlib
import json
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'engine'
DEST = ROOT / 'build' / 'engine'
PIN = 'd41c3f7eef2038f59702b58bdb373483fb0d28f9'


def stage_block_capacity(destination):
    # 745 world definitions plus 87 held item sprites exceed the upstream
    # 768-entry table. Keep Classic/CPE cells intact and allocate 1024 entries.
    path = destination/'src/BlockID.h'
    data = path.read_bytes()
    old = b'BLOCK_MAX_DEFINED = 0x2FF'
    if data.count(old) != 1:
        raise ValueError('Pinned block table layout changed')
    path.write_bytes(data.replace(old, b'BLOCK_MAX_DEFINED = 0x3FF'))


def stage_native_assets(destination):
    # Replace the upstream artwork only in the staged engine, never the submodule.
    manifest = json.loads((ROOT/'assets/manifest.json').read_text())
    expected = {'assets/native/LapisCube.ico', 'assets/native/LapisIcon_X11.h', 'assets/native/LapisFont.h'}
    if {a['destination_filename'] for a in manifest['assets'] if a['destination_filename'].startswith('assets/native/')} != expected:
        raise ValueError('Native asset manifest coverage mismatch')
    for asset in manifest['assets']:
        if asset['destination_filename'].startswith('assets/native/'):
            if hashlib.sha256((ROOT/asset['destination_filename']).read_bytes()).hexdigest() != asset['sha256']:
                raise ValueError('Native asset hash mismatch')
    native = ROOT/'assets/native'
    shutil.copy2(native/'LapisCube.ico', destination/'misc/CCicon.ico')
    shutil.copy2(native/'LapisIcon_X11.h', destination/'misc/x11/CCIcon_X11.h')
    shutil.copy2(native/'LapisFont.h', destination/'src/LapisFont.h')
    font_path = destination/'src/SystemFonts.c'
    font_text, count = re.subn(r'/\* 8x8 font bitmap.*?\n};',
        '/* Public-domain replacement; see assets/licenses/font8x8-README.txt. */\n#include "LapisFont.h"',
        font_path.read_text(), count=1, flags=re.S)
    if count != 1:
        raise ValueError('Pinned fallback font layout changed')
    font_path.write_text(font_text)
    # The upstream Windows rule omits its icon dependency.
    makefile = destination/'misc/makefiles/windows.mk'
    makefile.write_text(makefile.read_text().replace('$(ICON_OBJ): misc/windows/CCicon.rc',
        '$(ICON_OBJ): misc/windows/CCicon.rc misc/CCicon.ico'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['native', 'terminal', 'windows', 'web', 'singleplayer'], default='native')
    mode = parser.parse_args().mode
    head = subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise SystemExit('Unexpected ClassiCube revision; update the integration patch explicitly')
    if subprocess.check_output(['git', '-C', str(UPSTREAM), 'status', '--porcelain'], text=True).strip():
        raise SystemExit('Engine submodule has local changes; keep modifications in the integration patch')
    stamp = DEST / '.lapiscube-build-mode'
    build_key = mode + '-release-v2'
    if not stamp.exists() or stamp.read_text() != build_key:
        shutil.rmtree(DEST / 'build', ignore_errors=True)
        for name in ('LapisCube', 'LapisCube.exe'):
            (DEST / name).unlink(missing_ok=True)
    # Overwrite only source inputs, retaining build objects for incremental recompilation.
    shutil.copytree(UPSTREAM, DEST, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'build', 'ClassiCube', 'LapisCube'))
    for path in (ROOT / 'src').glob('*.[ch]'):
        shutil.copy2(path, DEST / 'src' / path.name)
    subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT / 'patches' / 'engine.patch')],
                   cwd=DEST, check=True)
    subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT/'patches/audio.patch')],
                   cwd=DEST, check=True)
    stage_native_assets(DEST)
    stage_block_capacity(DEST)
    subprocess.run(['python3', str(ROOT/'tools/embed_audio.py'), str(DEST/'src')], check=True)
    if mode in ('web', 'singleplayer'):
        subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT/'patches/web.patch')],
                       cwd=DEST, check=True)
        if mode == 'singleplayer':
            interop = DEST/'src/webclient/interop_web.js'
            data = interop.read_bytes()
            replacements = {
                b"new WebSocket(url, Module['lapisSocketURL'] ? 'LapisCube' : 'ClassiCube')": b"Module['lapisSocketFactory']()",
                b"var url  = 'default.zip';": b"var url  = Module['lapisAssetURL']('default.zip');",
                b"xhr.open('GET', 'sounds/' + name + '.wav', true);": b"xhr.open('GET', Module['lapisAssetURL']('sounds/' + name + '.wav'), true);",
            }
            for old,new in replacements.items():
                if data.count(old) != 1:
                    raise ValueError('Pinned singleplayer interop changed: '+repr(old))
                data=data.replace(old,new)
            interop.write_bytes(data)
    subprocess.run(['python3', str(ROOT/'tools/package.py'), '--stage-only'], check=True)
    stamp.write_text(build_key)


if __name__ == '__main__':
    main()
